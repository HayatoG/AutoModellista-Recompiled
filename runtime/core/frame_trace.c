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
    (void)fprintf(out, "%-28s P50 %7.2f  P95 %7.2f  P99 %7.2f  máx %7.2f ms\n", name, (double)pct(scratch, s_ft.count, 50U) / ms,
                  (double)pct(scratch, s_ft.count, 95U) / ms, (double)pct(scratch, s_ft.count, 99U) / ms,
                  (double)scratch[s_ft.count - 1U] / ms);
}

static void write_csv(void) {
    FILE *f = fopen(s_ft.path, "w");
    if (f == NULL) {
        AM_LOG_WARN("--frame-trace: não consegui gravar %s", s_ft.path);
        return;
    }
    (void)fprintf(f, "quadro,parede_ns,ciclos,campos_vi,pulado,apresentado,unico,apresentacao_ns,sono_ns,"
                     "espera_gx_ns,gx_ocupado_ns,espera_gpu_us,leituras_efb,paginas_copiadas\n");
    for (size_t i = 0; i < s_ft.count; i++) {
        const FrameTraceRow *r = &s_ft.rows[i];
        (void)fprintf(f, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u,%d,%d,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                         ",%" PRIu64 ",%u,%u\n",
                      r->frame, r->wall_ns, r->emu_cycles, r->vi_fields, r->skipped ? 1 : 0, r->presented ? 1 : 0,
                      r->unique ? 1 : 0, r->present_ns, r->pace_sleep_ns, r->gx_wait_ns, r->gx_busy_ns, r->gpu_wait_us,
                      r->readbacks, r->cow_copies);
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
    for (size_t i = 0; i < s_ft.count; i++) {
        const FrameTraceRow *r = &s_ft.rows[i];
        wall += r->wall_ns;
        unique += r->unique ? 1U : 0U;
        presented += r->presented ? 1U : 0U;
        skipped += r->skipped ? 1U : 0U;
        fields += r->vi_fields;
    }
    const double secs = (double)wall / 1e9;
    (void)fprintf(out, "Rastro por quadro: %zu quadros (%" PRIu64 " a %" PRIu64 "), %.2f s de relógio\n", s_ft.count,
                  s_ft.rows[0].frame, s_ft.rows[s_ft.count - 1U].frame, secs);
    (void)fprintf(out, "lógica: %.2f quadros/s; campos de VI: %.2f/s\n", (double)s_ft.count / secs, (double)fields / secs);
    (void)fprintf(out, "janela: %" PRIu64 " atualizações (%.2f/s), %" PRIu64 " com imagem nova (%.2f/s); %" PRIu64
                       " quadros pulados pelo frameskip\n\n",
                  presented, (double)presented / secs, unique, (double)unique / secs, skipped);
    report_field(out, "quadro (relógio)", f_wall, scratch);
    report_field(out, "CPU emulada (sem esperas)", f_cpu, scratch);
    report_field(out, "GX ocupado", f_gx_busy, scratch);
    report_field(out, "CPU esperando o GX", f_gx_wait, scratch);
    report_field(out, "apresentação da janela", f_present, scratch);
    report_field(out, "sono do ritmo", f_sleep, scratch);
    report_field(out, "espera pela GPU (EFB)", f_gpu_wait, scratch);
    (void)fprintf(out, "\nPiores quadros (relógio):\n");
    for (size_t i = 0; i < s_ft.count; i++) {
        scratch[i] = (s_ft.rows[i].wall_ns << 20) | (u64)(i & 0xFFFFFU);
    }
    qsort(scratch, s_ft.count, sizeof(*scratch), cmp_u64);
    for (size_t k = 0; k < WORST_FRAMES && k < s_ft.count; k++) {
        const FrameTraceRow *r = &s_ft.rows[scratch[s_ft.count - 1U - k] & 0xFFFFFU];
        (void)fprintf(out, "  quadro %" PRIu64 ": %.2f ms (CPU %.2f, GX ocupado %.2f, esperando o GX %.2f, janela %.2f, "
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
