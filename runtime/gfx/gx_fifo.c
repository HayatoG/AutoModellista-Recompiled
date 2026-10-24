/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/gx_fifo.h"

#include <string.h>

#include "am/gx_efb.h"
#include "am/gx_raster.h"
#include "am/gx_setup.h"
#include "am/gx_tex.h"
#include "am/gx_xf.h"
#include "am/log.h"
#include "am/mem.h"

#define GX_HEADER_MAX  8U
#define GX_PAYLOAD_MAX 256U /* um vértice (até ~130 bytes) ou 16 palavras de XF */
#define GX_XF_WORDS    16U

typedef enum GxFifoPhase {
    GX_PHASE_OPCODE,
    GX_PHASE_HEADER,  /* juntando os bytes fixos depois do opcode */
    GX_PHASE_SKIP,    /* pulando a carga útil (desenho sem renderização) */
    GX_PHASE_PAYLOAD, /* juntando um vértice ou as palavras de XF */
} GxFifoPhase;

typedef struct GxFifoParser {
    GxFifoPhase phase;
    u8 opcode;
    u8 header[GX_HEADER_MAX];
    u32 header_len;
    u32 header_need;
    u64 skip;
    u8 payload[GX_PAYLOAD_MAX];
    u32 payload_len;
    u32 payload_need;
    u32 vertices_left; /* desenho: vértices que ainda faltam */
    bool in_display_list;
    bool dl_pending; /* CALL_DL lido: o chamador executa a lista antes de seguir */
    u32 dl_addr;
    u32 dl_size;
} GxFifoParser;

static struct {
    GxFifoParser main;
    GxFifoSink sink;
    bool render;
    bool skip; /* quadro pulado (M7): desenhos e cópias para o XFB não são executados */
    GxFifoStats stats;
} s_gx;

void gx_fifo_reset(const GxFifoSink *sink) {
    memset(&s_gx, 0, sizeof(s_gx));
    if (sink != NULL) {
        s_gx.sink = *sink;
    }
    gx_vtx_reset();
    gx_xf_reset();
    gx_bp_reset();
    gx_efb_reset();
    gx_setup_reset();
    gx_raster_reset();
    gx_tex_cache_clear();
}

void gx_fifo_set_render(bool enabled) {
    s_gx.render = enabled;
}

void gx_fifo_set_skip(bool skip) {
    s_gx.skip = skip;
}

bool gx_fifo_render_enabled(void) {
    return s_gx.render;
}

const GxFifoStats *gx_fifo_stats(void) {
    return &s_gx.stats;
}

u32 gx_fifo_vertex_size(u32 vat) {
    return gx_vtx_size(vat);
}

static u32 be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void load_bp(u32 value) {
    s_gx.stats.bp_writes++;
    switch (gx_bp_write((u8)(value >> 24), value & 0xFFFFFFU)) {
    case GX_BP_EVENT_DRAW_DONE:
        s_gx.stats.draw_done++;
        if (s_gx.sink.draw_done != NULL) {
            s_gx.sink.draw_done();
        }
        return;
    case GX_BP_EVENT_TOKEN:
    case GX_BP_EVENT_TOKEN_INT:
        s_gx.stats.tokens++;
        if (s_gx.sink.token != NULL) {
            s_gx.sink.token((u16)value, (value >> 24) == GX_BP_PE_TOKEN_INT);
        }
        return;
    case GX_BP_EVENT_COPY:
        s_gx.stats.copies++;
        if (s_gx.render && !(s_gx.skip && gx_efb_copy_is_xfb())) {
            gx_raster_flush(); /* a cópia lê o EFB: nenhum triângulo pode ficar para trás */
            gx_efb_copy();
        }
        return;
    default:
        return;
    }
}

/* Bytes fixos depois de cada opcode; 0 = comando de 1 byte. */
static u32 header_size(u8 opcode, const GxFifoParser *p) {
    if (opcode >= GX_CMD_DRAW_FIRST && opcode < 0xC0U) {
        return 2U;
    }
    switch (opcode) {
    case GX_CMD_NOP:
    case GX_CMD_UNKNOWN_METRIC:
    case GX_CMD_INVL_VC:
        return 0U;
    case GX_CMD_LOAD_CP:
        return 5U;
    case GX_CMD_LOAD_XF:
    case GX_CMD_LOAD_BP:
    case GX_CMD_LOAD_INDX_A:
    case GX_CMD_LOAD_INDX_B:
    case GX_CMD_LOAD_INDX_C:
    case GX_CMD_LOAD_INDX_D:
        return 4U;
    case GX_CMD_CALL_DL:
        return 8U;
    default:
        am_fatal("GX: opcode 0x%02X desconhecido na FIFO%s depois de %llu bytes (%llu comandos)", opcode,
                 p->in_display_list ? " (display list)" : "", (unsigned long long)s_gx.stats.bytes,
                 (unsigned long long)s_gx.stats.commands);
    }
}

/* Desenho: com renderização, cada vértice é juntado e entregue; sem, a carga útil é pulada. */
static void start_draw(GxFifoParser *p, u32 count) {
    const u8 op = p->opcode;
    const u32 size = gx_vtx_size(op & 7U);
    s_gx.stats.draws++;
    s_gx.stats.vertices += count;
    p->phase = GX_PHASE_OPCODE;
    if (count == 0U || size == 0U) {
        return;
    }
    if (!s_gx.render || s_gx.skip) {
        p->skip = (u64)count * size;
        p->phase = GX_PHASE_SKIP;
        return;
    }
    AM_ASSERT(size <= GX_PAYLOAD_MAX, "vértice de %u bytes", size);
    gx_setup_begin((op >> 3) & 7U, op & 7U);
    p->vertices_left = count;
    p->payload_len = 0;
    p->payload_need = size;
    p->phase = GX_PHASE_PAYLOAD;
}

/* Comando com cabeçalho completo: executa e define a próxima fase. */
static void execute(GxFifoParser *p) {
    const u8 op = p->opcode;
    s_gx.stats.commands++;
    p->phase = GX_PHASE_OPCODE;
    if (op >= GX_CMD_DRAW_FIRST) {
        start_draw(p, ((u32)p->header[0] << 8) | p->header[1]);
        return;
    }
    switch (op) {
    case GX_CMD_LOAD_CP:
        s_gx.stats.cp_writes++;
        gx_vtx_load_cp(p->header[0], be32(&p->header[1]));
        return;
    case GX_CMD_LOAD_XF: {
        const u32 words = ((be32(p->header) >> 16) & 0xFU) + 1U; /* OpcodeDecoding: ((cmd2 >> 16) & 15) + 1 */
        s_gx.stats.xf_words += words;
        p->vertices_left = 0;
        p->payload_len = 0;
        p->payload_need = words * 4U;
        p->phase = GX_PHASE_PAYLOAD;
        return;
    }
    case GX_CMD_LOAD_BP:
        load_bp(be32(p->header));
        return;
    case GX_CMD_LOAD_INDX_A:
    case GX_CMD_LOAD_INDX_B:
    case GX_CMD_LOAD_INDX_C:
    case GX_CMD_LOAD_INDX_D: {
        /* índice nos bits 16-31, (tamanho - 1) nos 12-15, endereço do XF nos 0-11; arrays 12..15 */
        const u32 v = be32(p->header);
        gx_xf_load_indexed(GX_ARRAY_XF_BASE + ((op - GX_CMD_LOAD_INDX_A) >> 3), v >> 16, v & 0xFFFU,
                           ((v >> 12) & 0xFU) + 1U);
        return;
    }
    case GX_CMD_CALL_DL:
        if (p->in_display_list) {
            am_fatal("GX: display list chamada de dentro de outra display list (Dolphin ignora com aviso)");
        }
        p->dl_pending = true;
        p->dl_addr = be32(p->header);
        p->dl_size = be32(&p->header[4]);
        return;
    default:
        return; /* NOP, métrica, invalidação do cache de vértices */
    }
}

/* Carga útil completa: um vértice do desenho ou as palavras do LOAD_XF. */
static void payload_done(GxFifoParser *p) {
    if (p->opcode == GX_CMD_LOAD_XF) {
        const u32 addr = be32(p->header) & 0xFFFFU;
        for (u32 i = 0; i < p->payload_need / 4U; i++) {
            gx_xf_write(addr + i, be32(&p->payload[(size_t)i * 4U]));
        }
        p->phase = GX_PHASE_OPCODE;
        return;
    }
    gx_setup_vertex(p->payload);
    p->payload_len = 0;
    if (--p->vertices_left == 0U) {
        p->phase = GX_PHASE_OPCODE;
        gx_setup_end();
    }
}

/* Consome bytes até o fim de `data` ou até um CALL_DL (devolve quantos usou). */
static u32 parse(GxFifoParser *p, const u8 *data, u32 size) {
    u32 i = 0;
    while (i < size && !p->dl_pending) {
        if (p->phase == GX_PHASE_SKIP) {
            const u64 take = p->skip < (u64)(size - i) ? p->skip : (u64)(size - i);
            p->skip -= take;
            i += (u32)take;
            if (p->skip == 0U) {
                p->phase = GX_PHASE_OPCODE;
            }
            continue;
        }
        if (p->phase == GX_PHASE_PAYLOAD) {
            const u32 take = p->payload_need - p->payload_len < size - i ? p->payload_need - p->payload_len : size - i;
            memcpy(&p->payload[p->payload_len], &data[i], take);
            p->payload_len += take;
            i += take;
            if (p->payload_len == p->payload_need) {
                payload_done(p);
            }
            continue;
        }
        if (p->phase == GX_PHASE_OPCODE) {
            p->opcode = data[i++];
            p->header_len = 0;
            p->header_need = header_size(p->opcode, p);
            p->phase = GX_PHASE_HEADER;
        } else {
            p->header[p->header_len++] = data[i++];
        }
        if (p->header_len == p->header_need) {
            execute(p);
        }
    }
    return i;
}

/* A display list é lida da memória do guest (endereço físico, 32 bytes alinhado). */
static void run_display_list(u32 addr, u32 size) {
    GxFifoParser dl;
    memset(&dl, 0, sizeof(dl));
    dl.phase = GX_PHASE_OPCODE;
    dl.in_display_list = true;
    u8 chunk[256];
    s_gx.stats.display_lists++;
    const GuestAddr base = MEM_CACHED_BASE | (addr & 0x03FFFFFFU);
    for (u32 off = 0; off < size; off += (u32)sizeof(chunk)) {
        const u32 n = size - off < (u32)sizeof(chunk) ? size - off : (u32)sizeof(chunk);
        for (u32 k = 0; k < n; k++) {
            chunk[k] = mem_read8(NULL, base + off + k);
        }
        (void)parse(&dl, chunk, n); /* dentro de uma lista, CALL_DL é fatal em execute: nunca para no meio */
    }
    if (dl.phase != GX_PHASE_OPCODE) {
        am_fatal("GX: display list em 0x%08X (%u bytes) termina no meio de um comando (opcode 0x%02X)", addr, size,
                 dl.opcode);
    }
}

void gx_fifo_feed(const u8 *data, u32 size) {
    s_gx.stats.bytes += size;
    while (size > 0U) {
        const u32 used = parse(&s_gx.main, data, size);
        data += used;
        size -= used;
        if (s_gx.main.dl_pending) {
            s_gx.main.dl_pending = false;
            run_display_list(s_gx.main.dl_addr, s_gx.main.dl_size);
        }
    }
}
