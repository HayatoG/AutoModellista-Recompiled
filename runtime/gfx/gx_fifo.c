/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/gx_fifo.h"

#include <string.h>

#include "am/gx_efb.h"
#include "am/gx_gpu.h"
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
    bool skip;       /* quadro pulado (M7): desenhos e cópias para o XFB não são executados */
    bool render_cpu; /* os mesmos dois, como a thread da CPU os pediu (com a thread do GX, chegam em ordem pela fila) */
    bool skip_cpu;
    GxFifoStats stats;
} s_gx;

static void thread_stop(void);

void gx_fifo_reset(const GxFifoSink *sink) {
    thread_stop();
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

static void queue_flags(bool render, bool skip);

void gx_fifo_set_render(bool enabled) {
    queue_flags(enabled, s_gx.skip_cpu);
    s_gx.render_cpu = enabled;
    if (!gx_fifo_threaded()) {
        s_gx.render = enabled;
    }
}

void gx_fifo_set_skip(bool skip) {
    queue_flags(s_gx.render_cpu, skip);
    s_gx.skip_cpu = skip;
    if (!gx_fifo_threaded()) {
        s_gx.skip = skip;
    }
}

bool gx_fifo_render_enabled(void) {
    return s_gx.render_cpu;
}

const GxFifoStats *gx_fifo_stats(void) {
    gx_fifo_sync();
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
            if (gx_gpu_active()) {
                gx_gpu_copy(); /* M8: o EFB da GPU vem para a CPU, que faz a cópia; a limpeza fica na GPU */
            } else {
                gx_efb_copy();
            }
        }
        return;
    default:
        return;
    }
}

/* Bytes fixos depois de cada opcode; 0 = comando de 1 byte. */
static u32 header_size(u8 opcode, bool in_display_list) {
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
                 in_display_list ? " (display list)" : "", (unsigned long long)s_gx.stats.bytes,
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
            p->header_need = header_size(p->opcode, p->in_display_list);
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

static void feed_direct(const u8 *data, u32 size) {
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

/* ---- M8: o GX numa thread própria (como o "dual core" do Dolphin) ----
 *
 * A thread da CPU emulada só lê o fluxo por cima (scan_*): opcodes, cabeçalhos, CP (para o tamanho dos vértices),
 * BP (para os eventos) e as display lists, pulando vértices e palavras de XF. Os bytes vão para uma fila de blocos;
 * a thread do GX os consome com o leitor completo (feed_direct), que faz todo o resto (vertex loader, XF, montagem,
 * rasterização ou GPU, cópias do EFB). Os eventos que o jogo enxerga (DrawDone, tokens) são disparados pela thread da
 * CPU no mesmo byte de antes, depois de esperar a thread do GX terminar tudo o que veio antes deles: o tempo emulado
 * não muda, e quando o jogo reaproveita memória (o que só faz depois desses eventos) o GX já a leu. Quem lê o que o
 * GX escreveu fora deles (o XFB da janela e das capturas, as estatísticas) chama gx_fifo_sync antes. */

#define GX_Q_BLOCK  32768U
#define GX_Q_BLOCKS 32U /* 1 MiB em voo, no máximo */

typedef struct GxQBlock {
    u32 size;
    bool render;
    bool skip;
    u8 data[GX_Q_BLOCK];
} GxQBlock;

typedef struct GxScan {
    GxFifoPhase phase;
    u8 opcode;
    u8 header[GX_HEADER_MAX];
    u32 header_len;
    u32 header_need;
    u64 skip;
    bool in_display_list;
} GxScan;

static struct {
    bool on;
    bool quit;
    GxFifoThreadApi api;
    void *thread;
    void *lock;
    void *wake; /* a thread do GX espera bloco */
    void *done; /* a thread da CPU espera a fila andar */
    GxFifoSink sink;
    u32 head;  /* com a trava: primeiro bloco publicado */
    u32 count; /* com a trava: blocos publicados ainda não terminados (inclui o que o GX está lendo) */
    u32 tail;  /* só a CPU: bloco sendo enchido */
    u64 syncs;
    u64 waits;      /* sincronizações em que o GX ainda tinha trabalho */
    u64 wait_ns[3]; /* tempo de relógio esperando, por motivo (DrawDone, token, leitura de fora) */
    u32 reason;
    GxScan scan;
    u32 vcd_lo;
    u32 vcd_hi;
    u32 vat_a[GX_VAT_COUNT];
    u32 vat_b[GX_VAT_COUNT];
    u32 vat_c[GX_VAT_COUNT];
    u32 bp[256];
    u32 bp_mask;
    GxQBlock blocks[GX_Q_BLOCKS];
} s_q;

bool gx_fifo_threaded(void) {
    return s_q.on;
}

static int gx_thread_main(void *user) {
    (void)user;
    s_q.api.lock(s_q.lock);
    for (;;) {
        while (s_q.count == 0U && !s_q.quit) {
            s_q.api.cond_wait(s_q.wake, s_q.lock);
        }
        if (s_q.count == 0U) {
            break;
        }
        GxQBlock *b = &s_q.blocks[s_q.head];
        s_q.api.unlock(s_q.lock);
        s_gx.render = b->render;
        s_gx.skip = b->skip;
        feed_direct(b->data, b->size);
        s_q.api.lock(s_q.lock);
        s_q.head = (s_q.head + 1U) % GX_Q_BLOCKS;
        s_q.count--;
        s_q.api.cond_signal(s_q.done);
    }
    s_q.api.unlock(s_q.lock);
    return 0;
}

/* Entrega o bloco que a CPU está enchendo e começa outro (espera se a fila estiver cheia). */
static void queue_publish(void) {
    GxQBlock *cur = &s_q.blocks[s_q.tail];
    const bool render = cur->render;
    const bool skip = cur->skip;
    s_q.api.lock(s_q.lock);
    while (s_q.count >= GX_Q_BLOCKS - 1U) {
        s_q.api.cond_wait(s_q.done, s_q.lock);
    }
    s_q.count++;
    s_q.api.cond_signal(s_q.wake);
    s_q.api.unlock(s_q.lock);
    s_q.tail = (s_q.tail + 1U) % GX_Q_BLOCKS;
    GxQBlock *next = &s_q.blocks[s_q.tail];
    next->size = 0;
    next->render = render;
    next->skip = skip;
}

static void queue_flags(bool render, bool skip) {
    if (!s_q.on) {
        return;
    }
    GxQBlock *cur = &s_q.blocks[s_q.tail];
    if (cur->render == render && cur->skip == skip) {
        return;
    }
    if (cur->size != 0U) {
        queue_publish();
        cur = &s_q.blocks[s_q.tail];
    }
    cur->render = render;
    cur->skip = skip;
}

static void queue_push(const u8 *data, u32 size) {
    while (size > 0U) {
        GxQBlock *cur = &s_q.blocks[s_q.tail];
        if (cur->size == GX_Q_BLOCK) {
            queue_publish();
            continue;
        }
        const u32 take = GX_Q_BLOCK - cur->size < size ? GX_Q_BLOCK - cur->size : size;
        memcpy(&cur->data[cur->size], data, take);
        cur->size += take;
        data += take;
        size -= take;
    }
}

void gx_fifo_sync(void) {
    if (!s_q.on) {
        return;
    }
    s_q.syncs++;
    if (s_q.blocks[s_q.tail].size != 0U) {
        queue_publish();
    }
    s_q.api.lock(s_q.lock);
    if (s_q.count != 0U) {
        s_q.waits++;
        const u64 t0 = s_q.api.ticks_ns();
        while (s_q.count != 0U) {
            s_q.api.cond_wait(s_q.done, s_q.lock);
        }
        s_q.wait_ns[s_q.reason] += s_q.api.ticks_ns() - t0;
    }
    s_q.reason = 2U;
    s_q.api.unlock(s_q.lock);
}

void gx_fifo_thread_counters(u64 *syncs, u64 *waits, u64 wait_ns[3]) {
    *syncs = s_q.syncs;
    *waits = s_q.waits;
    memcpy(wait_ns, s_q.wait_ns, sizeof(s_q.wait_ns));
}

/* BP na cópia da CPU: a mesma máscara de gx_bp_write, só para achar os eventos. */
static void scan_bp(u32 value) {
    const u8 reg = (u8)(value >> 24);
    const u32 newval = (s_q.bp[reg] & ~s_q.bp_mask) | (value & s_q.bp_mask & 0xFFFFFFU);
    if (reg != GX_BP_MASK) {
        s_q.bp_mask = 0xFFFFFFU;
    }
    s_q.bp[reg] = newval;
    if (reg == GX_BP_MASK) {
        s_q.bp_mask = newval & 0xFFFFFFU;
    } else if (reg == GX_BP_SETDRAWDONE && (newval & 0xFFU) == 0x02U) {
        s_q.reason = 0U;
        gx_fifo_sync();
        if (s_q.sink.draw_done != NULL) {
            s_q.sink.draw_done();
        }
    } else if (reg == GX_BP_PE_TOKEN || reg == GX_BP_PE_TOKEN_INT) {
        s_q.reason = 1U;
        gx_fifo_sync();
        if (s_q.sink.token != NULL) {
            s_q.sink.token((u16)value, reg == GX_BP_PE_TOKEN_INT);
        }
    }
}

static void scan_cp(u8 reg, u32 value) {
    const u32 low = reg & 7U;
    switch (reg & 0xF0U) {
    case GX_CP_VCD_LO:
        s_q.vcd_lo = value;
        return;
    case GX_CP_VCD_HI:
        s_q.vcd_hi = value;
        return;
    case GX_CP_VAT_A:
        s_q.vat_a[low] = value;
        return;
    case GX_CP_VAT_B:
        s_q.vat_b[low] = value;
        return;
    case GX_CP_VAT_C:
        s_q.vat_c[low] = value;
        return;
    default:
        return;
    }
}

static void scan_display_list(u32 addr, u32 size);

static void scan_execute(GxScan *p) {
    const u8 op = p->opcode;
    p->phase = GX_PHASE_OPCODE;
    if (op >= GX_CMD_DRAW_FIRST) {
        const u32 count = ((u32)p->header[0] << 8) | p->header[1];
        const u32 vat = op & 7U;
        const u32 size = gx_vtx_size_of(s_q.vcd_lo, s_q.vcd_hi, s_q.vat_a[vat], s_q.vat_b[vat], s_q.vat_c[vat]);
        p->skip = (u64)count * size;
        p->phase = p->skip != 0U ? GX_PHASE_SKIP : GX_PHASE_OPCODE;
        return;
    }
    switch (op) {
    case GX_CMD_LOAD_CP:
        scan_cp(p->header[0], be32(&p->header[1]));
        return;
    case GX_CMD_LOAD_XF:
        p->skip = (u64)(((be32(p->header) >> 16) & 0xFU) + 1U) * 4U;
        p->phase = GX_PHASE_SKIP;
        return;
    case GX_CMD_LOAD_BP:
        scan_bp(be32(p->header));
        return;
    case GX_CMD_CALL_DL:
        if (p->in_display_list) {
            am_fatal("GX: display list chamada de dentro de outra display list (Dolphin ignora com aviso)");
        }
        scan_display_list(be32(p->header), be32(&p->header[4]));
        return;
    default:
        return;
    }
}

static void scan_byte(GxScan *p, u8 b) {
    if (p->phase == GX_PHASE_OPCODE) {
        p->opcode = b;
        p->header_len = 0;
        p->header_need = header_size(b, p->in_display_list);
        p->phase = GX_PHASE_HEADER;
    } else {
        p->header[p->header_len++] = b;
    }
    if (p->header_len == p->header_need) {
        scan_execute(p);
    }
}

static void scan_display_list(u32 addr, u32 size) {
    GxScan dl;
    memset(&dl, 0, sizeof(dl));
    dl.phase = GX_PHASE_OPCODE;
    dl.in_display_list = true;
    const GuestAddr base = MEM_CACHED_BASE | (addr & 0x03FFFFFFU);
    u32 off = 0;
    while (off < size) {
        if (dl.phase == GX_PHASE_SKIP) {
            const u64 take = dl.skip < (u64)(size - off) ? dl.skip : (u64)(size - off);
            dl.skip -= take;
            off += (u32)take;
            if (dl.skip == 0U) {
                dl.phase = GX_PHASE_OPCODE;
            }
            continue;
        }
        scan_byte(&dl, mem_read8(NULL, base + off));
        off++;
    }
}

static void scan_feed(const u8 *data, u32 size) {
    GxScan *p = &s_q.scan;
    u32 i = 0;
    while (i < size) {
        if (p->phase == GX_PHASE_SKIP) {
            const u64 take = p->skip < (u64)(size - i) ? p->skip : (u64)(size - i);
            p->skip -= take;
            i += (u32)take;
            if (p->skip == 0U) {
                p->phase = GX_PHASE_OPCODE;
            }
            continue;
        }
        scan_byte(p, data[i++]);
    }
}

void gx_fifo_feed(const u8 *data, u32 size) {
    if (!s_q.on) {
        feed_direct(data, size);
        return;
    }
    queue_push(data, size); /* antes do scan: um evento no meio do bloco espera o GX ler até ele */
    scan_feed(data, size);
}

bool gx_fifo_thread_start(const GxFifoThreadApi *api) {
    AM_ASSERT(!s_q.on && s_gx.stats.bytes == 0U, "thread do GX ligada depois de o fluxo começar");
    memset(&s_q, 0, sizeof(s_q));
    s_q.api = *api;
    s_q.bp_mask = 0xFFFFFFU;
    s_q.reason = 2U;
    s_q.scan.phase = GX_PHASE_OPCODE;
    s_q.lock = api->lock_create();
    s_q.wake = api->cond_create();
    s_q.done = api->cond_create();
    s_q.sink = s_gx.sink;
    s_q.blocks[0].render = s_gx.render_cpu;
    s_q.blocks[0].skip = s_gx.skip_cpu;
    if (s_q.lock != NULL && s_q.wake != NULL && s_q.done != NULL) {
        s_q.thread = api->thread_start("gx-fifo", gx_thread_main, NULL);
    }
    if (s_q.thread == NULL) {
        thread_stop();
        return false;
    }
    memset(&s_gx.sink, 0, sizeof(s_gx.sink)); /* os eventos saem da thread da CPU (scan_bp) */
    s_q.on = true;
    return true;
}

static void thread_stop(void) {
    if (s_q.thread != NULL) {
        gx_fifo_sync();
        s_q.api.lock(s_q.lock);
        s_q.quit = true;
        s_q.api.cond_signal(s_q.wake);
        s_q.api.unlock(s_q.lock);
        s_q.api.thread_join(s_q.thread);
        s_gx.sink = s_q.sink;
    }
    if (s_q.done != NULL) {
        s_q.api.cond_destroy(s_q.done);
    }
    if (s_q.wake != NULL) {
        s_q.api.cond_destroy(s_q.wake);
    }
    if (s_q.lock != NULL) {
        s_q.api.lock_destroy(s_q.lock);
    }
    s_q.on = false;
    s_q.thread = NULL;
    s_q.lock = NULL;
    s_q.wake = NULL;
    s_q.done = NULL;
}

void gx_fifo_thread_stop(void) {
    thread_stop();
}
