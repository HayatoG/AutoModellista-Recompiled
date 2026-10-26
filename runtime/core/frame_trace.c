/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/frame_trace.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am/log.h"

#define WORST_FRAMES 10U

static struct {
    bool on;
    u64 first;
    u64 last; /* 0 = sem fim */
    char path[512];
    FrameTraceRow *rows;
    size_t count;
    size_t cap;
} s_ft;

bool frame_trace_open(const char *spec) {
    memset(&s_ft, 0, sizeof(s_ft));
    const char *file = spec;
    unsigned long long q0 = 0;
    unsigned long long q1 = 0;
    int used = 0;
    if (sscanf(spec, "%llu:%llu:%n", &q0, &q1, &used) == 2 && used > 0) {
        file = spec + used;
        s_ft.first = (u64)q0;
        s_ft.last = (u64)q1;
    }
    if (file[0] == '\0' || strlen(file) >= sizeof(s_ft.path) - 8U) {
        return false;
    }
    (void)strncpy(s_ft.path, file, sizeof(s_ft.path) - 1U);
    s_ft.on = true;
    return true;
}

bool frame_trace_on(void) {
    return s_ft.on;
}

bool frame_trace_wants(u64 frame) {
    return s_ft.on && frame >= s_ft.first && (s_ft.last == 0U || frame <= s_ft.last);
}

void frame_trace_add(const FrameTraceRow *row) {
    if (!s_ft.on) {
        return;
    }
    if (s_ft.count == s_ft.cap) {
        const size_t cap = s_ft.cap != 0U ? s_ft.cap * 2U : 4096U;
        FrameTraceRow *rows = realloc(s_ft.rows, cap * sizeof(*rows));
        if (rows == NULL) {
            AM_LOG_WARN("--frame-trace: sem memória para %zu quadros; o rastro para aqui", cap);
            s_ft.on = false;
            return;
        }
        s_ft.rows = rows;
        s_ft.cap = cap;
    }
    s_ft.rows[s_ft.count++] = *row;
}

static int cmp_u64(const void *a, const void *b) {
    const u64 x = *(const u64 *)a;
    const u64 y = *(const u64 *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* Percentil p (0..100) de v ordenado (índice mais próximo, para cima). */
static u64 pct(const u64 *v, size_t n, u32 p) {
    if (n == 0U) {
        return 0;
    }
    size_t i = (size_t)(((u64)n * p + 99U) / 100U);
    i = i == 0U ? 0U : i - 1U;
    return v[i < n ? i : n - 1U];
}

typedef u64 (*Field)(const FrameTraceRow *r);

static u64 f_wall(const FrameTraceRow *r) {
    return r->wall_ns;
}
static u64 f_cpu(const FrameTraceRow *r) {
    const u64 waits = r->present_ns + r->pace_sleep_ns + r->gx_wait_ns;
    return r->wall_ns > waits ? r->wall_ns - waits : 0U;
}
static u64 f_gx_busy(const FrameTraceRow *r) {
    return r->gx_busy_ns;
}
static u64 f_gx_wait(const FrameTraceRow *r) {
    return r->gx_wait_ns;
}
static u64 f_present(const FrameTraceRow *r) {
    return r->present_ns;
}
static u64 f_sleep(const FrameTraceRow *r) {
    return r->pace_sleep_ns;
}
static u64 f_gpu_wait(const FrameTraceRow *r) {
    return r->gpu_wait_us * 1000U;
}

static void report_field(FILE *out, const char *name, Field f, u64 *scratch) {
    for (size_t i = 0; i < s_ft.count; i++) {
        scratch[i] = f(&s_ft.rows[i]);
    }
    qsort(scratch, s_ft.count, sizeof(*scratch), cmp_u64);
    const double ms = 1e6;
    (void)fprintf(out, "%-28s P50 %7.2f  P95 %7.2f  P99 %7.2f  máx %7.2f ms\n", name,
                  (double)pct(scratch, s_ft.count, 50U) / ms, (double)pct(scratch, s_ft.count, 95U) / ms,
                  (double)pct(scratch, s_ft.count, 99U) / ms, (double)scratch[s_ft.count - 1U] / ms);
}

static u64 f_wait_draw(const FrameTraceRow *r) {
    return r->wait_draw_ns;
}
static u64 f_wait_token(const FrameTraceRow *r) {
    return r->wait_token_ns;
}
static u64 f_wait_read(const FrameTraceRow *r) {
    return r->wait_read_ns;
}
static u64 f_wait_full(const FrameTraceRow *r) {
    return r->wait_full_ns;
}
static u64 f_cow(const FrameTraceRow *r) {
    return r->cow_ns;
}
static u64 f_feed(const FrameTraceRow *r) {
    return r->feed_ns;
}
static u64 f_dl(const FrameTraceRow *r) {
    return r->dl_scan_ns;
}
static u64 f_xfb_conv(const FrameTraceRow *r) {
    return r->xfb_conv_ns;
}

/* A conversão do XFB para a janela, na thread do GX: só existe com janela (e a thread do GX ligada). */
static void report_xfb_conv(FILE *out, u64 *scratch) {
    u64 ns = 0;
    u64 calls = 0;
    for (size_t i = 0; i < s_ft.count; i++) {
        ns += s_ft.rows[i].xfb_conv_ns;
        calls += s_ft.rows[i].xfb_conv_calls;
    }
    if (calls == 0U) {
        (void)fprintf(out, "%-28s sem valor: só há conversão com janela (e a thread do GX ligada)\n",
                      "GX: conversão do XFB (janela)");
        return;
    }
    report_field(out, "GX: conversão do XFB (janela)", f_xfb_conv, scratch);
    (void)fprintf(out, "  %" PRIu64 " conversões, %.2f ms cada em média, %.2f ms por quadro\n", calls,
                  (double)ns / (double)calls / 1e6, (double)ns / (double)s_ft.count / 1e6);
}

/* Médias por quadro do que a CPU gastou no caminho do GX, e o custo por chamada (a soma das três é o que a thread da
 * CPU paga por ter o GX ligado, fora as esperas). Nada a mostrar sem a thread do GX. */
static void report_cpu_cost(FILE *out) {
    u64 ns[3] = {0};
    u64 calls[3] = {0};
    for (size_t i = 0; i < s_ft.count; i++) {
        const FrameTraceRow *r = &s_ft.rows[i];
        ns[0] += r->cow_ns;
        ns[1] += r->feed_ns;
        ns[2] += r->dl_scan_ns;
        calls[0] += r->cow_calls;
        calls[1] += r->feed_calls;
        calls[2] += r->dl_calls;
    }
    if (ns[0] + ns[1] + ns[2] == 0U) {
        return;
    }
    static const char *const names[3] = {"cópia na escrita", "leitura do fluxo", "scan de display lists"};
    const double frames = (double)s_ft.count;
    (void)fprintf(out, "CPU no caminho do GX, média por quadro (sem esperas):\n");
    for (size_t k = 0; k < 3U; k++) {
        (void)fprintf(out, "  %-22s %6.2f ms, %8.0f chamadas, %6.0f ns cada\n", names[k], (double)ns[k] / frames / 1e6,
                      (double)calls[k] / frames, calls[k] != 0U ? (double)ns[k] / (double)calls[k] : 0.0);
    }
    (void)fprintf(out, "  %-22s %6.2f ms\n", "total", (double)(ns[0] + ns[1] + ns[2]) / frames / 1e6);
    (void)fprintf(out,
                  "  (chamadas: contagem exata; a leitura do fluxo é estimada medindo 1 chamada em cada poucas, e\n"
                  "   o custo de ler o relógio, de resolução ~100 ns no Windows, já está descontado)\n");
}

/* Contagens (não tempos): épocas de atraso e blocos em voo. */
static void report_count(FILE *out, const char *name, bool lag, u64 *scratch) {
    for (size_t i = 0; i < s_ft.count; i++) {
        scratch[i] = lag ? s_ft.rows[i].gx_lag_max : s_ft.rows[i].queue_max;
    }
    qsort(scratch, s_ft.count, sizeof(*scratch), cmp_u64);
    (void)fprintf(out, "%-28s P50 %7" PRIu64 "  P95 %7" PRIu64 "  P99 %7" PRIu64 "  máx %7" PRIu64 "\n", name,
                  pct(scratch, s_ft.count, 50U), pct(scratch, s_ft.count, 95U), pct(scratch, s_ft.count, 99U),
                  scratch[s_ft.count - 1U]);
}

/* Intervalo entre apresentações que mostraram imagem (só os quadros com um anterior). */
static void report_gap(FILE *out, u64 *scratch) {
    size_t n = 0;
    for (size_t i = 0; i < s_ft.count; i++) {
        if (s_ft.rows[i].present_gap_ns != 0U) {
            scratch[n++] = s_ft.rows[i].present_gap_ns;
        }
    }
    if (n == 0U) {
        return;
    }
    qsort(scratch, n, sizeof(*scratch), cmp_u64);
    (void)fprintf(out, "%-28s P50 %7.2f  P95 %7.2f  P99 %7.2f  máx %7.2f ms\n", "intervalo entre imagens",
                  (double)pct(scratch, n, 50U) / 1e6, (double)pct(scratch, n, 95U) / 1e6,
                  (double)pct(scratch, n, 99U) / 1e6, (double)scratch[n - 1U] / 1e6);
}

static void write_csv(void) {
    FILE *f = fopen(s_ft.path, "w");
    if (f == NULL) {
        AM_LOG_WARN("--frame-trace: não consegui gravar %s", s_ft.path);
        return;
    }
    (void)fprintf(f, "quadro,parede_ns,ciclos,campos_vi,pulado,apresentado,unico,apresentacao_ns,sono_ns,"
                     "espera_gx_ns,gx_ocupado_ns,espera_gpu_us,leituras_efb,paginas_copiadas,"
                     "espera_draw_ns,espera_token_ns,espera_leitura_ns,espera_fila_ns,atraso_gx_epocas,fila_blocos,"
                     "mostrado,intervalo_apres_ns,gx_desenhos,gx_triangulos,gx_texturas,gx_recomecos_texels,"
                     "gx_texels,cow_ns,cow_chamadas,feed_ns,feed_chamadas,dl_scan_ns,dl_chamadas,"
                     "xfb_conv_ns,xfb_conv_chamadas\n");
    for (size_t i = 0; i < s_ft.count; i++) {
        const FrameTraceRow *r = &s_ft.rows[i];
        (void)fprintf(f,
                      "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,%d,%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                      ",%" PRIu64 ",%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,%u,%d,%" PRIu64
                      ",%u,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%u,%" PRIu64 ",%u,%" PRIu64 ",%u,%" PRIu64 ",%u\n",
                      r->frame, r->wall_ns, r->emu_cycles, r->vi_fields, r->skipped ? 1 : 0, r->presented ? 1 : 0,
                      r->unique ? 1 : 0, r->present_ns, r->pace_sleep_ns, r->gx_wait_ns, r->gx_busy_ns, r->gpu_wait_us,
                      r->readbacks, r->cow_copies, r->wait_draw_ns, r->wait_token_ns, r->wait_read_ns, r->wait_full_ns,
                      r->gx_lag_max, r->queue_max, r->shown ? 1 : 0, r->present_gap_ns, r->gx_draws, r->gx_triangles,
                      r->gx_tex_uploads, r->gx_arena_resets, r->gx_texels, r->cow_ns, r->cow_calls, r->feed_ns,
                      r->feed_calls, r->dl_scan_ns, r->dl_calls, r->xfb_conv_ns, r->xfb_conv_calls);
    }
    (void)fclose(f);
}

static void write_report(void) {
    char path[sizeof(s_ft.path) + 8U];
    (void)snprintf(path, sizeof(path), "%s.txt", s_ft.path);
    FILE *out = fopen(path, "w");
    u64 *scratch = malloc(s_ft.count * sizeof(*scratch));
    if (out == NULL || scratch == NULL) {
        AM_LOG_WARN("--frame-trace: não consegui gravar o relatório %s", path);
        if (out != NULL) {
            (void)fclose(out);
        }
        free(scratch);
        return;
    }
    u64 wall = 0;
    u64 unique = 0;
    u64 presented = 0;
    u64 skipped = 0;
    u64 fields = 0;
    u64 shown = 0;
    u64 uploads = 0;
    u64 texels = 0;
    u64 resets = 0;
    for (size_t i = 0; i < s_ft.count; i++) {
        const FrameTraceRow *r = &s_ft.rows[i];
        shown += r->shown ? 1U : 0U;
        uploads += r->gx_tex_uploads;
        texels += r->gx_texels;
        resets += r->gx_arena_resets;
        wall += r->wall_ns;
        unique += r->unique ? 1U : 0U;
        presented += r->presented ? 1U : 0U;
        skipped += r->skipped ? 1U : 0U;
        fields += r->vi_fields;
    }
    const double secs = (double)wall / 1e9;
    (void)fprintf(out, "Rastro por quadro: %zu quadros (%" PRIu64 " a %" PRIu64 "), %.2f s de relógio\n", s_ft.count,
                  s_ft.rows[0].frame, s_ft.rows[s_ft.count - 1U].frame, secs);
    (void)fprintf(out, "lógica: %.2f quadros/s; campos de VI: %.2f/s\n", (double)s_ft.count / secs,
                  (double)fields / secs);
    (void)fprintf(out,
                  "janela: %" PRIu64 " atualizações (%.2f/s), %" PRIu64 " com imagem nova (%.2f/s); %" PRIu64
                  " quadros pulados pelo frameskip\n\n",
                  presented, (double)presented / secs, unique, (double)unique / secs, skipped);
    (void)fprintf(out,
                  "quadros: %zu de lógica, %zu desenhados (sem pulo), %" PRIu64 " mostrados de fato (%" PRIu64
                  " descartados pelo swapchain), %" PRIu64 " com imagem nova\n",
                  s_ft.count, s_ft.count - (size_t)skipped, shown, presented - shown, unique);
    (void)fprintf(out,
                  "GX na GPU: %" PRIu64 " texturas enviadas (%" PRIu64 " texels), buffer de texels recomeçado %" PRIu64
                  " vez(es); contados quando o GX conclui, não quando o jogo pede\n\n",
                  uploads, texels, resets);
    report_field(out, "quadro (relógio)", f_wall, scratch);
    report_field(out, "CPU emulada (sem esperas)", f_cpu, scratch);
    report_field(out, "GX ocupado", f_gx_busy, scratch);
    report_field(out, "CPU esperando o GX", f_gx_wait, scratch);
    report_field(out, "apresentação da janela", f_present, scratch);
    report_field(out, "sono do ritmo", f_sleep, scratch);
    report_field(out, "espera pela GPU (EFB)", f_gpu_wait, scratch);
    /* A SDL3 GPU não tem timestamp portável: não há tempo de GPU de verdade. A linha acima é só quanto o GX ficou
     * bloqueado na fence da leitura do EFB. */
    report_field(out, "  espera: DrawDone", f_wait_draw, scratch);
    report_field(out, "  espera: token", f_wait_token, scratch);
    report_field(out, "  espera: leitura de fora", f_wait_read, scratch);
    report_field(out, "  espera: fila cheia", f_wait_full, scratch);
    report_count(out, "atraso do GX (épocas)", true, scratch);
    report_count(out, "blocos em voo na fila", false, scratch);
    report_gap(out, scratch);
    report_field(out, "CPU: cópia na escrita", f_cow, scratch);
    report_field(out, "CPU: leitura do fluxo", f_feed, scratch);
    report_field(out, "CPU: scan de display lists", f_dl, scratch);
    report_cpu_cost(out);
    report_xfb_conv(out, scratch);
    (void)fprintf(out, "\nPiores quadros (relógio):\n");
    for (size_t i = 0; i < s_ft.count; i++) {
        scratch[i] = (s_ft.rows[i].wall_ns << 20) | (u64)(i & 0xFFFFFU);
    }
    qsort(scratch, s_ft.count, sizeof(*scratch), cmp_u64);
    for (size_t k = 0; k < WORST_FRAMES && k < s_ft.count; k++) {
        const FrameTraceRow *r = &s_ft.rows[scratch[s_ft.count - 1U - k] & 0xFFFFFU];
        (void)fprintf(out,
                      "  quadro %" PRIu64 ": %.2f ms (CPU %.2f, GX ocupado %.2f, esperando o GX %.2f, janela %.2f, "
                      "GPU %.2f)\n",
                      r->frame, (double)r->wall_ns / 1e6, (double)f_cpu(r) / 1e6, (double)r->gx_busy_ns / 1e6,
                      (double)r->gx_wait_ns / 1e6, (double)r->present_ns / 1e6, (double)r->gpu_wait_us / 1e3);
    }
    (void)fclose(out);
    free(scratch);
    AM_LOG_INFO("--frame-trace: %zu quadros em %s (relatório em %s); janela com %.2f imagens novas/s", s_ft.count,
                s_ft.path, path, (double)unique / secs);
}

void frame_trace_close(void) {
    if (s_ft.path[0] != '\0' && s_ft.count != 0U) {
        write_csv();
        write_report();
    }
    free(s_ft.rows);
    memset(&s_ft, 0, sizeof(s_ft));
}
