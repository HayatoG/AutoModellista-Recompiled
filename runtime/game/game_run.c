/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/game_run.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am/ai.h"
#include "am/di.h"
#include "am/disc.h"
#include "am/dsp.h"
#include "am/dsp_hle.h"
#include "am/exception.h"
#include "am/game.h"
#include "am/gather_pipe.h"
#include "am/guest_cpu.h"
#include "am/gx_fifo.h"
#include "am/hook.h"
#include "am/log.h"
#include "am/os_link.h"
#include "am/os_threads.h"
#include "am/pad_script.h"
#include "am/pi.h"
#include "am/platform.h"
#include "am/recomp.h"
#include "am/si.h"
#include "am/timebase.h"
#include "am/vi.h"

#define GAME_RUN_REL_CACHE 16U

typedef struct RelFile {
    char name[16];
    u8 *data;
    size_t size;
} RelFile;

static struct {
    GameRunConfig config;
    u64 frames;
    bool target_reached;
    RelFile rels[GAME_RUN_REL_CACHE];
    u32 rel_count;
    FILE *events;
    PadScript pad;
    u64 fields_at_frame; /* campos de VI quando o último quadro começou */
} s_run;

static void event_line(const char *fmt, ...) AM_PRINTF(1, 2);

static void event_line(const char *fmt, ...) {
    if (s_run.events == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    (void)vfprintf(s_run.events, fmt, ap);
    va_end(ap);
    (void)fputc('\n', s_run.events);
    (void)fflush(s_run.events); /* poucas linhas; um timeout não pode levar o que já aconteceu */
}

static void on_di_read(u64 offset, u32 length, u32 dst) {
    event_line("di_read frame=%" PRIu64 " offset=0x%09" PRIX64 " length=0x%X dst=0x%08X", s_run.frames, offset, length,
               dst & 0x03FFFFFFU);
}

static void on_link_event(const OsLinkEvent *e, void *user) {
    (void)user;
    event_line("%s frame=%" PRIu64 " id=%u real=0x%08X", e->unlink ? "osunlink" : "oslink", s_run.frames, e->id,
               e->real);
}

static HookAction on_main(CPUState *cpu, void *user) {
    (void)cpu;
    (void)user;
    event_line("main");
    return HOOK_CONTINUE;
}

/* files/module/<nome>.rel da árvore extraída, lido uma vez. */
static bool load_rel(const char *name, const u8 **data, size_t *size, void *user) {
    (void)user;
    for (u32 i = 0; i < s_run.rel_count; i++) {
        if (strcmp(s_run.rels[i].name, name) == 0) {
            *data = s_run.rels[i].data;
            *size = s_run.rels[i].size;
            return true;
        }
    }
    char path[1024];
    const int n = snprintf(path, sizeof(path), "%s/files/module/%s.rel", s_run.config.disc_root, name);
    if (n < 0 || (size_t)n >= sizeof(path) || s_run.rel_count == GAME_RUN_REL_CACHE) {
        return false;
    }
    RelFile *f = &s_run.rels[s_run.rel_count];
    f->data = platform_load_file(path, &f->size);
    if (f->data == NULL) {
        return false;
    }
    (void)snprintf(f->name, sizeof(f->name), "%s", name);
    s_run.rel_count++;
    *data = f->data;
    *size = f->size;
    return true;
}

/* fn_80051034 chamada pelo laço de fn_8004838C = um quadro. Para depois de `frames` quadros completos. */
static HookAction on_frame(CPUState *cpu, void *user) {
    (void)user;
    if (cpu->lr != GAME_ADDR_FRAME_RETURN) {
        return HOOK_CONTINUE;
    }
    if (s_run.config.frames != 0U && s_run.frames >= s_run.config.frames) {
        s_run.target_reached = true;
        return HOOK_STOP;
    }
    s_run.frames++;
    s_run.fields_at_frame = vi_field_count();
    si_set_pad_buttons(pad_script_buttons(&s_run.pad, s_run.frames));
    if (s_run.frames == 1U || s_run.frames % 60U == 0U) {
        AM_LOG_INFO("quadro %" PRIu64 ": %" PRIu64 " campos de VI, %" PRIu64 " ciclos, %" PRIu64 " leituras de DVD",
                    s_run.frames, vi_field_count(), timebase_cycles(cpu), di_stats()->reads);
    }
    return HOOK_CONTINUE;
}

/* "game_1" -> nome "game", id 1. O DOL ("dol") não entra. */
static u32 build_modules(OsLinkModule *out) {
    u32 count = 0;
    for (size_t i = 0; i < recomp_module_count() && count < OS_LINK_MAX_MODULES; i++) {
        const char *full = recomp_module_name(i);
        const char *sep = strrchr(full, '_');
        if (sep == NULL || strcmp(full, "dol") == 0) {
            continue;
        }
        OsLinkModule *m = &out[count++];
        memset(m, 0, sizeof(*m));
        const size_t len = (size_t)(sep - full) < sizeof(m->name) - 1U ? (size_t)(sep - full) : sizeof(m->name) - 1U;
        memcpy(m->name, full, len);
        m->id = (u32)strtoul(sep + 1, NULL, 10);
        m->code_start = recomp_module_code_start(i);
        m->prolog = recomp_module_entry(i);
    }
    return count;
}

bool game_run_init(const GameRunConfig *config) {
    memset(&s_run, 0, sizeof(s_run));
    s_run.config = *config;
    if (config->pad_script != NULL && !pad_script_parse(config->pad_script, &s_run.pad)) {
        AM_LOG_ERROR("roteiro de botões inválido: %s", config->pad_script);
        return false;
    }
    const DiscResult d = disc_open(config->disc_root);
    if (d != DISC_OK) {
        AM_LOG_ERROR("disco em %s: %s", config->disc_root, disc_result_name(d));
        return false;
    }
    AM_LOG_INFO("disco: %u regiões de %s", disc_stats()->regions, config->disc_root);
    OsLinkModule modules[OS_LINK_MAX_MODULES];
    const OsLinkConfig link = {.os_link = GAME_ADDR_OS_LINK,
                               .os_link_fixed = GAME_ADDR_OS_LINK_FIXED,
                               .os_unlink = GAME_ADDR_OS_UNLINK,
                               .modules = modules,
                               .module_count = build_modules(modules),
                               .load_rel = load_rel,
                               .on_event = on_link_event};
    if (config->events != NULL) {
        s_run.events = fopen(config->events, "w");
        if (s_run.events == NULL) {
            AM_LOG_ERROR("não consegui criar %s", config->events);
            return false;
        }
        (void)fprintf(s_run.events, "# am-events v1 source=runtime\n");
        di_set_read_observer(on_di_read);
    }
    if (!os_link_init(&link) || !hook_register(GAME_ADDR_FRAME, "quadro (fn_80051034)", on_frame, NULL) ||
        !hook_register(GAME_ADDR_MAIN, "main", on_main, NULL)) {
        AM_LOG_ERROR("não consegui registrar os ganchos do M5");
        return false;
    }
    return true;
}

void game_run_shutdown(void) {
    if (s_run.events != NULL) {
        (void)fclose(s_run.events);
        s_run.events = NULL;
    }
    di_set_read_observer(NULL);
    for (u32 i = 0; i < s_run.rel_count; i++) {
        platform_free_file(s_run.rels[i].data);
    }
    s_run.rel_count = 0;
    disc_close();
}

void game_run_tick(const CPUState *cpu) {
    if (s_run.frames == 0U || vi_field_count() - s_run.fields_at_frame < GAME_RUN_STALL_FIELDS) {
        return;
    }
    os_threads_dump(cpu, 12);
    AM_LOG_INFO("PI: causa 0x%05X máscara 0x%05X; MSR 0x%08X; VI DI0 0x%08X DI1 0x%08X", pi_cause(), pi_mask(),
                cpu->msr, (u32)vi_read(0x30U, 4), (u32)vi_read(0x34U, 4));
    AM_LOG_INFO("interrupções entregues por causa: DI %" PRIu64 " SI %" PRIu64 " EXI %" PRIu64 " AI %" PRIu64
                " DSP %" PRIu64 " VI %" PRIu64 " PE token %" PRIu64 " PE fim %" PRIu64 " CP %" PRIu64,
                pi_delivery_count(2), pi_delivery_count(3), pi_delivery_count(4), pi_delivery_count(5),
                pi_delivery_count(6), pi_delivery_count(8), pi_delivery_count(9), pi_delivery_count(10),
                pi_delivery_count(11));
    AM_LOG_INFO("VI: VTR 0x%04X DCR 0x%04X HTR0 0x%08X VTO 0x%08X VTE 0x%08X clock %u, meia-linha %u",
                (u32)vi_read(0x00U, 2), (u32)vi_read(0x02U, 2), (u32)vi_read(0x04U, 4), (u32)vi_read(0x0CU, 4),
                (u32)vi_read(0x10U, 4), (u32)vi_read(0x6CU, 2), vi_half_line());
    am_fatal("laço de quadros parado: quadro %" PRIu64 " começou há %" PRIu64 " campos de VI (%" PRIu64
             " leituras de DVD até aqui); threads acima",
             s_run.frames, vi_field_count() - s_run.fields_at_frame, di_stats()->reads);
}

u64 game_run_frames(void) {
    return s_run.frames;
}

bool game_run_frame_target_reached(void) {
    return s_run.target_reached;
}

void game_run_report(void) {
    for (u32 i = 0; i < os_link_event_count(); i++) {
        const OsLinkEvent *e = os_link_event(i);
        AM_LOG_INFO("  %s %-7s id %2u real 0x%08X virtual 0x%08X (ciclo %" PRIu64 ")",
                    e->unlink ? "OSUnlink" : "OSLink", e->name, e->id, e->real, e->virt, e->cycle);
    }
    const DiStats *di = di_stats();
    const DiscStats *disc = disc_stats();
    AM_LOG_INFO("DVD: %" PRIu64 " comandos, %" PRIu64 " leituras (%" PRIu64 " bytes, %" PRIu64
                " de enchimento), %" PRIu64 " AudioBufferConfig",
                di->commands, di->reads, di->read_bytes, disc->padding_bytes, di->audio_buffer_config);
    AM_LOG_INFO("VI: %" PRIu64 " campos, XFB 0x%08X; SI: %" PRIu64
                " leituras do controle; AI: streaming ligado %" PRIu64 " vez(es)",
                vi_field_count(), vi_xfb_top(), si_poll_count(), ai_stream_starts());
    const DspHleStats *dsp = dsp_hle_stats();
    AM_LOG_INFO("DSP: microcódigo 0x%08X (%u bytes de 0x%08X), %" PRIu64 " listas do AX, %" PRIu64
                " interrupções de DMA de áudio",
                dsp->ucode_crc, dsp->last_boot_len, dsp->last_boot_addr, dsp->command_lists,
                dsp_audio_dma_interrupts());
    const GxFifoStats *gx = gx_fifo_stats();
    AM_LOG_INFO("GX: %" PRIu64 " bytes no gather pipe, %" PRIu64 " comandos, %" PRIu64 " desenhos (%" PRIu64
                " vértices), %" PRIu64 " BP, %" PRIu64 " DrawDone, %" PRIu64 " tokens, %" PRIu64 " cópias; descartado",
                gather_pipe_total_bytes(), gx->commands, gx->draws, gx->vertices, gx->bp_writes, gx->draw_done,
                gx->tokens, gx->copies);
    AM_LOG_INFO("exceções: externa %" PRIu64 ", decrementer %" PRIu64 ", FP indisponível %" PRIu64,
                exception_count_of(EXCEPTION_NUM_EXTERNAL), exception_count_of(EXCEPTION_NUM_DECREMENTER),
                exception_count_of(EXCEPTION_NUM_FP_UNAVAILABLE));
}
