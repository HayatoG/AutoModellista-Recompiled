/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/gx_fifo.h"

#include <string.h>

#include "am/log.h"
#include "am/mem.h"

#define GX_VAT_COUNT  8U
#define GX_HEADER_MAX 8U
/* Tamanho de componente por formato (u8, s8, u16, s16, f32; Dolphin VertexLoader: GetElementSize). */
#define GX_FORMAT_COUNT 5U

typedef enum GxFifoPhase {
    GX_PHASE_OPCODE,
    GX_PHASE_HEADER, /* juntando os bytes fixos depois do opcode */
    GX_PHASE_SKIP,   /* pulando a carga útil (dados de XF, vértices) */
} GxFifoPhase;

typedef struct GxFifoParser {
    GxFifoPhase phase;
    u8 opcode;
    u8 header[GX_HEADER_MAX];
    u32 header_len;
    u32 header_need;
    u64 skip;
    bool in_display_list;
    bool dl_pending; /* CALL_DL lido: o chamador executa a lista antes de seguir */
    u32 dl_addr;
    u32 dl_size;
} GxFifoParser;

static struct {
    GxFifoParser main;
    GxFifoSink sink;
    u32 vcd_lo;
    u32 vcd_hi;
    u32 vat_a[GX_VAT_COUNT];
    u32 vat_b[GX_VAT_COUNT];
    u32 vat_c[GX_VAT_COUNT];
    GxFifoStats stats;
} s_gx;

void gx_fifo_reset(const GxFifoSink *sink) {
    memset(&s_gx, 0, sizeof(s_gx));
    if (sink != NULL) {
        s_gx.sink = *sink;
    }
}

const GxFifoStats *gx_fifo_stats(void) {
    return &s_gx.stats;
}

static u32 be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u32 component_size(u32 format) {
    static const u8 s_sizes[GX_FORMAT_COUNT] = {1, 1, 2, 2, 4};
    if (format >= GX_FORMAT_COUNT) {
        am_fatal("GX: formato de componente %u inválido no VAT", format);
    }
    return s_sizes[format];
}

/* Tipo de atributo no VCD: 0 ausente, 1 direto, 2 índice de 8 bits, 3 índice de 16 bits. */
static u32 attr_size(u32 type, u32 direct_size) {
    switch (type) {
    case 1U:
        return direct_size;
    case 2U:
        return 1U;
    case 3U:
        return 2U;
    default:
        return 0U;
    }
}

/* Cor direta: RGB565, RGB888, RGB888x, RGBA4444, RGBA6666, RGBA8888. */
static u32 color_size(u32 comp) {
    static const u8 s_sizes[6] = {2, 3, 4, 2, 3, 4};
    if (comp >= 6U) {
        am_fatal("GX: formato de cor %u inválido no VAT", comp);
    }
    return s_sizes[comp];
}

/* Elementos e formato da coordenada de textura n no VAT (Dolphin CPMemory.h: UVAT_group0/1/2). */
static void tex_format(u32 vat, u32 n, u32 *elements, u32 *format) {
    const u32 a = s_gx.vat_a[vat];
    const u32 b = s_gx.vat_b[vat];
    const u32 c = s_gx.vat_c[vat];
    static const u8 s_shift_b[3] = {0, 9, 18};  /* Tex1..Tex3 em VAT_B */
    static const u8 s_shift_c[3] = {5, 14, 23}; /* Tex5..Tex7 em VAT_C */
    if (n == 0U) {
        *elements = (a >> 21) & 1U;
        *format = (a >> 22) & 7U;
    } else if (n <= 3U) {
        *elements = (b >> s_shift_b[n - 1U]) & 1U;
        *format = (b >> (s_shift_b[n - 1U] + 1U)) & 7U;
    } else if (n == 4U) {
        *elements = (b >> 27) & 1U;
        *format = (b >> 28) & 7U;
    } else {
        *elements = (c >> s_shift_c[n - 5U]) & 1U;
        *format = (c >> (s_shift_c[n - 5U] + 1U)) & 7U;
    }
}

u32 gx_fifo_vertex_size(u32 vat) {
    AM_ASSERT(vat < GX_VAT_COUNT, "VAT %u", vat);
    const u32 lo = s_gx.vcd_lo;
    const u32 hi = s_gx.vcd_hi;
    const u32 a = s_gx.vat_a[vat];
    u32 size = 0;
    for (u32 bit = 0; bit < 9U; bit++) { /* PosNrmMtxIdx e Tex0..7MtxIdx: 1 byte cada, sempre diretos */
        size += (lo >> bit) & 1U;
    }
    const u32 pos_elems = (a & 1U) != 0U ? 3U : 2U;
    size += attr_size((lo >> 9) & 3U, pos_elems * component_size((a >> 1) & 7U));
    const u32 nrm_type = (lo >> 11) & 3U;
    const bool nbt = ((a >> 9) & 1U) != 0U;
    const bool index3 = ((a >> 31) & 1U) != 0U;
    if (nbt && index3 && nrm_type >= 2U) {
        size += 3U * attr_size(nrm_type, 0U); /* NBT com um índice por vetor */
    } else {
        size += attr_size(nrm_type, (nbt ? 9U : 3U) * component_size((a >> 10) & 7U));
    }
    size += attr_size((lo >> 13) & 3U, color_size((a >> 14) & 7U));
    size += attr_size((lo >> 15) & 3U, color_size((a >> 18) & 7U));
    for (u32 n = 0; n < 8U; n++) {
        u32 elements = 0;
        u32 format = 0;
        tex_format(vat, n, &elements, &format);
        size += attr_size((hi >> (n * 2U)) & 3U, (elements != 0U ? 2U : 1U) * component_size(format));
    }
    return size;
}

static void load_cp(u8 reg, u32 value) {
    s_gx.stats.cp_writes++;
    const u32 vat = reg & 7U;
    switch (reg & 0xF0U) {
    case GX_CP_VCD_LO:
        s_gx.vcd_lo = value;
        return;
    case GX_CP_VCD_HI:
        s_gx.vcd_hi = value;
        return;
    case GX_CP_VAT_A:
        s_gx.vat_a[vat] = value;
        return;
    case GX_CP_VAT_B:
        s_gx.vat_b[vat] = value;
        return;
    case GX_CP_VAT_C:
        s_gx.vat_c[vat] = value;
        return;
    default:
        return; /* matrizes de índice, bases e passos de arrays: só importam para desenhar (M6) */
    }
}

static void load_bp(u32 value) {
    s_gx.stats.bp_writes++;
    const u32 reg = value >> 24;
    if (reg == GX_BP_SETDRAWDONE && (value & 0xFFU) == 0x02U) {
        s_gx.stats.draw_done++;
        if (s_gx.sink.draw_done != NULL) {
            s_gx.sink.draw_done();
        }
    } else if (reg == GX_BP_PE_TOKEN || reg == GX_BP_PE_TOKEN_INT) {
        s_gx.stats.tokens++;
        if (s_gx.sink.token != NULL) {
            s_gx.sink.token((u16)value, reg == GX_BP_PE_TOKEN_INT);
        }
    } else if (reg == GX_BP_COPY_EXECUTE) {
        s_gx.stats.copies++;
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
    case 0x28U:
    case 0x30U:
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

/* Comando com cabeçalho completo: executa e diz quantos bytes de carga útil pular. */
static u64 execute(GxFifoParser *p) {
    const u8 op = p->opcode;
    s_gx.stats.commands++;
    if (op >= GX_CMD_DRAW_FIRST) {
        const u32 count = ((u32)p->header[0] << 8) | p->header[1];
        s_gx.stats.draws++;
        s_gx.stats.vertices += count;
        return (u64)count * gx_fifo_vertex_size(op & 7U);
    }
    switch (op) {
    case GX_CMD_LOAD_CP:
        load_cp(p->header[0], be32(&p->header[1]));
        return 0;
    case GX_CMD_LOAD_XF: {
        const u32 words = ((be32(p->header) >> 16) & 0xFU) + 1U; /* OpcodeDecoding: ((cmd2 >> 16) & 15) + 1 */
        s_gx.stats.xf_words += words;
        return (u64)words * 4U;
    }
    case GX_CMD_LOAD_BP:
        load_bp(be32(p->header));
        return 0;
    case GX_CMD_CALL_DL:
        if (p->in_display_list) {
            am_fatal("GX: display list chamada de dentro de outra display list (Dolphin ignora com aviso)");
        }
        p->dl_pending = true;
        p->dl_addr = be32(p->header);
        p->dl_size = be32(&p->header[4]);
        return 0;
    default:
        return 0; /* LOAD_INDX_*: carrega matrizes de XF a partir da memória (M6) */
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
        if (p->phase == GX_PHASE_OPCODE) {
            p->opcode = data[i++];
            p->header_len = 0;
            p->header_need = header_size(p->opcode, p);
            p->phase = GX_PHASE_HEADER;
        } else {
            p->header[p->header_len++] = data[i++];
        }
        if (p->header_len == p->header_need) {
            p->skip = execute(p);
            p->phase = p->skip != 0U ? GX_PHASE_SKIP : GX_PHASE_OPCODE;
        }
    }
    return i;
}

/* A display list é lida da memória do guest (endereço físico, 32 bytes alinhado). */
static void run_display_list(u32 addr, u32 size) {
    GxFifoParser dl = {.phase = GX_PHASE_OPCODE, .in_display_list = true};
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
