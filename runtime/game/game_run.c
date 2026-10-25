/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/game_run.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am/ai.h"
#include "am/audio_dump.h"
#include "am/ax_mixer.h"
#include "am/ax_voice.h"
#include "am/di.h"
#include "am/disc.h"
#include "am/dsp.h"
#include "am/dsp_hle.h"
#include "am/exception.h"
#include "am/exi_card.h"
#include "am/game.h"
#include "am/game_diag.h"
#include "am/gather_pipe.h"
#include "am/guest_cpu.h"
#include "am/gx_efb.h"
#include "am/gx_fifo.h"
#include "am/gx_gpu.h"
#include "am/gx_raster.h"
#include "am/gx_setup.h"
#include "am/gx_tex.h"
#include "am/hook.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/os_link.h"
#include "am/os_threads.h"
#include "am/pad_input.h"
#include "am/pad_script.h"
#include "am/pi.h"
#include "am/platform.h"
#include "am/platform_gpu.h"
#include "am/recomp.h"
#include "am/si.h"
#include "am/timebase.h"
#include "am/vi.h"

#define GAME_RUN_REL_CACHE 16U
#define GAME_RUN_SHOT_MAX  32U
/* Ritmo em tempo real: a emulação pode andar até 60 ms na frente do relógio (é a reserva da fila de áudio); se ficar
 * mais de 250 ms atrás, desiste de alcançar e recomeça a contagem dali. */
#define GAME_RUN_PACE_AHEAD_NS  60000000ULL
#define GAME_RUN_PACE_BEHIND_NS 250000000ULL
#define GAME_RUN_CPU_HZ         486000000ULL
/* Pulo de quadros (M7): atrasado mais que um campo de VI, o próximo quadro não é desenhado (no máximo 4 seguidos), a
 * menos que o jogo tenha copiado o EFB para textura nos últimos 120 quadros. */
#define GAME_RUN_SKIP_BEHIND_NS 16683350LL
#define GAME_RUN_SKIP_MAX       4U
/* M8: com a GPU o quadro desenhado custa pouco mais que o pulado (~21 ms contra ~12 ms na corrida, arm64): pular um
 * de cada vez já segura o ritmo e desenha o dobro de quadros que a sequência de 4 do software. */
#define GAME_RUN_SKIP_MAX_GPU   1U
#define GAME_RUN_SKIP_TEX_GUARD 120U
/* M8 parte 2b: com a thread do GX, pular o desenho só tira trabalho do GX; se a CPU não esperou o GX pelo menos isto
 * no último quadro, o atraso é da CPU emulada e pular só tiraria quadros da tela. */
#define GAME_RUN_SKIP_GX_WAIT_NS 2000000ULL

typedef struct Shot {
    u64 frame;
    char path[512];
} Shot;

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
    FILE *timebase;
    PadScript pad;
    bool live_input;     /* sem --pad-script e com janela: teclado e gamepad (M7) */
    u64 fields_at_frame; /* campos de VI quando o último quadro começou */
    Shot shots[GAME_RUN_SHOT_MAX];
    u32 shot_count;
    bool window;
    bool window_closed;
    bool audio_out;
    bool pace_started;
    u64 pace_cycles0;
    u64 pace_wall0;
    u64 pace_rebases; /* vezes em que a emulação ficou para trás do relógio */
    bool skipping;    /* o quadro em andamento não está sendo desenhado */
    u32 skipped_in_row;
    u64 skipped;
    u64 tex_copies_seen;
    u64 gx_wait_seen;   /* espera da CPU pelo GX (DrawDone e tokens) até o último quadro */
    u64 tex_copy_frame; /* último quadro com cópia do EFB para textura */
    u64 audio_frames;
    u64 log_ms;      /* relógio na última linha de progresso (quadros/s de verdade) */
    u64 log_skipped; /* quadros pulados até a última linha de progresso */
    u8 rgba[GAME_XFB_WIDTH * GAME_XFB_HEIGHT * 4U];
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
    if (s_run.timebase != NULL) {
        (void)fprintf(s_run.timebase, "read 0x%09" PRIX64 " 0x%X %" PRIu64 "\n", offset, length, timebase_guest_last());
    }
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

/* __DVDInterruptHandler: com --timebase-out, o instante de cada interrupção do DVD (o mesmo do oráculo: o último
 * mftb do jogo, am/timebase.h). */
static HookAction on_dvd_irq(CPUState *cpu, void *user) {
    (void)cpu;
    (void)user;
    (void)fprintf(s_run.timebase, "dvdirq %" PRIu64 "\n", timebase_guest_last());
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

/* "quadro:arquivo.png,...": o caminho vai até a vírgula seguinte. */
static bool parse_shots(const char *text) {
    const char *p = text;
    while (*p != '\0') {
        char *end = NULL;
        const unsigned long long frame = strtoull(p, &end, 10);
        if (end == p || *end != ':' || s_run.shot_count == GAME_RUN_SHOT_MAX) {
            return false;
        }
        const char *path = end + 1;
        const char *comma = strchr(path, ',');
        const size_t len = comma != NULL ? (size_t)(comma - path) : strlen(path);
        Shot *sh = &s_run.shots[s_run.shot_count++];
        if (len == 0U || len >= sizeof(sh->path)) {
            return false;
        }
        sh->frame = (u64)frame;
        memcpy(sh->path, path, len);
        sh->path[len] = '\0';
        p = comma != NULL ? comma + 1 : path + len;
    }
    return true;
}

/* XFB (YUYV na memória do guest) em RGBA no buffer da execução. */
static void xfb_to_rgba(GuestAddr addr) {
    gx_efb_xfb_to_rgba(addr, GAME_XFB_WIDTH, GAME_XFB_HEIGHT, GAME_XFB_WIDTH * 2U, s_run.rgba);
}

/* Início do quadro: atualiza a janela com o XFB que o VI está mostrando e grava as capturas pedidas (o XFB que
 * VISetNextFrameBuffer recebeu por último, como o oráculo); com janela, a captura também grava o que a janela
 * desenhou (<arquivo>_janela.png). Devolve false se a janela foi fechada. */
static bool capture_frame(CPUState *cpu, bool refresh) {
    const Shot *shot = NULL;
    for (u32 i = 0; i < s_run.shot_count; i++) {
        if (s_run.shots[i].frame == s_run.frames) {
            shot = &s_run.shots[i];
        }
    }
    bool open = true;
    if (s_run.window) {
        char window_png[600];
        if (shot != NULL) {
            (void)snprintf(window_png, sizeof(window_png), "%.*s_janela.png", (int)(strlen(shot->path) - 4U),
                           shot->path);
        }
        const u32 shown = vi_xfb_top();
        if (shown != 0U && refresh) { /* depois de um quadro pulado, o XFB é velho: fica a última imagem */
            if (gx_fifo_present_xfb(shown, GAME_XFB_WIDTH, GAME_XFB_HEIGHT, GAME_XFB_WIDTH * 2U)) {
                /* thread do GX: ele converte quando chegar aqui; a janela mostra a última pronta, sem esperar */
                (void)gx_fifo_xfb_latest(s_run.rgba, GAME_XFB_WIDTH, GAME_XFB_HEIGHT);
            } else {
                xfb_to_rgba(shown);
            }
        }
        const PlatformWindowResult r =
            platform_window_present(s_run.rgba, GAME_XFB_WIDTH, GAME_XFB_HEIGHT, shot != NULL ? window_png : NULL);
        if (r == PLATFORM_WINDOW_ERROR) {
            am_fatal("janela: %s", platform_last_error());
        }
        open = r == PLATFORM_WINDOW_OK;
    }
    if (shot != NULL) {
        const GuestAddr xfb = mem_read32(cpu, GAME_ADDR_VI_NEXT_FB);
        if (xfb == 0U) {
            AM_LOG_WARN("quadro %" PRIu64 ": o jogo ainda não definiu um XFB; captura %s ignorada", s_run.frames,
                        shot->path);
            return open;
        }
        gx_fifo_sync();
        xfb_to_rgba(xfb);
        if (!platform_save_png(shot->path, s_run.rgba, GAME_XFB_WIDTH, GAME_XFB_HEIGHT)) {
            am_fatal("não consegui gravar %s: %s", shot->path, platform_last_error());
        }
        AM_LOG_INFO("quadro %" PRIu64 ": XFB 0x%08X -> %s", s_run.frames, xfb, shot->path);
    }
    return open;
}

/* Saída da DMA de áudio (hw/dsp.c): para o WAV do --audio-dump e para o dispositivo de som. */
static void on_audio(const s16 *lr, u32 frames, void *user) {
    (void)user;
    s_run.audio_frames += frames;
    audio_dump_write(lr, frames);
    if (s_run.audio_out) {
        platform_audio_push(lr, frames);
    }
}

/* Segura a emulação no ritmo do relógio: o tempo emulado são os ciclos estimados da CPU a 486 MHz. Devolve quanto a
 * emulação está atrás do relógio (ns; negativo = adiantada). */
static s64 pace(void) {
    const u64 cycles = timebase_cycles(NULL);
    const u64 now = platform_ticks_ns();
    if (!s_run.pace_started) {
        s_run.pace_started = true;
        s_run.pace_cycles0 = cycles;
        s_run.pace_wall0 = now;
        return 0;
    }
    const u64 emu_ns = (cycles - s_run.pace_cycles0) * 1000000000ULL / GAME_RUN_CPU_HZ;
    const u64 wall_ns = now - s_run.pace_wall0;
    if (emu_ns > wall_ns + GAME_RUN_PACE_AHEAD_NS) {
        platform_sleep_ns(emu_ns - wall_ns - GAME_RUN_PACE_AHEAD_NS);
        return 0;
    }
    if (wall_ns > emu_ns + GAME_RUN_PACE_BEHIND_NS) {
        s_run.pace_cycles0 = cycles;
        s_run.pace_wall0 = now;
        s_run.pace_rebases++;
        return 0;
    }
    return (s64)wall_ns - (s64)emu_ns;
}

/* Decide se o próximo quadro é desenhado (só com --realtime e --frameskip). */
static void decide_skip(s64 behind) {
    const u64 tex_copies = gx_efb_stats()->texture_copies;
    if (tex_copies != s_run.tex_copies_seen) {
        s_run.tex_copies_seen = tex_copies;
        s_run.tex_copy_frame = s_run.frames;
    }
    const bool guard = s_run.tex_copy_frame != 0U && s_run.frames - s_run.tex_copy_frame < GAME_RUN_SKIP_TEX_GUARD;
    bool gx_late = true;
    if (gx_fifo_threaded()) {
        u64 syncs = 0;
        u64 waits = 0;
        u64 wait_ns[3] = {0};
        gx_fifo_thread_counters(&syncs, &waits, wait_ns);
        const u64 waited = wait_ns[0] + wait_ns[1];
        gx_late = waited - s_run.gx_wait_seen >= GAME_RUN_SKIP_GX_WAIT_NS;
        s_run.gx_wait_seen = waited;
    }
    const bool skip = s_run.config.frameskip && behind > GAME_RUN_SKIP_BEHIND_NS && gx_late &&
                      s_run.skipped_in_row < (s_run.config.gpu ? GAME_RUN_SKIP_MAX_GPU : GAME_RUN_SKIP_MAX) && !guard;
    s_run.skipping = skip;
    s_run.skipped_in_row = skip ? s_run.skipped_in_row + 1U : 0U;
    s_run.skipped += skip ? 1U : 0U;
    gx_fifo_set_skip(skip);
}

/* Controle do quadro: o roteiro (--pad-script) tem prioridade; sem ele, com janela, o teclado e o gamepad. */
static void update_pad(void) {
    SiPadState pad = si_pad_neutral();
    if (s_run.live_input) {
        PlatformInput in;
        platform_input_read(&in);
        pad_input_map(&in, &pad);
    } else {
        pad.buttons = pad_script_buttons(&s_run.pad, s_run.frames);
    }
    si_set_pad(&pad);
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
    if (s_run.timebase != NULL) {
        (void)fprintf(s_run.timebase, "%" PRIu64 " %" PRIu64 "\n", s_run.frames, timebase_guest_last());
    }
    game_diag_frame(s_run.frames);
    const bool last_skipped = s_run.skipping;
    if (s_run.config.realtime) {
        decide_skip(pace());
    }
    update_pad();
    if (!capture_frame(cpu, !last_skipped)) {
        s_run.window_closed = true;
        return HOOK_STOP;
    }
    if (s_run.frames == 1U || s_run.frames % 60U == 0U) {
        const u64 now = platform_ticks_ms();
        const u64 dt = now - s_run.log_ms;
        s_run.log_ms = now;
        AM_LOG_INFO("quadro %" PRIu64 ": %" PRIu64 " campos de VI, %" PRIu64 " ciclos, %" PRIu64
                    " leituras de DVD; %.1f quadros/s, %" PRIu64 " desenhado(s)",
                    s_run.frames, vi_field_count(), timebase_cycles(cpu), di_stats()->reads,
                    s_run.frames > 1U && dt > 0U ? 60000.0 / (double)dt : 0.0,
                    60U - (s_run.skipped - s_run.log_skipped));
        s_run.log_skipped = s_run.skipped;
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

/* Roteiro e capturas: texto das opções validado antes de tudo. */
static bool init_scripts(const GameRunConfig *config) {
    if (config->pad_script != NULL && !pad_script_parse(config->pad_script, &s_run.pad)) {
        AM_LOG_ERROR("roteiro de botões inválido: %s", config->pad_script);
        return false;
    }
    if (config->shots != NULL && !parse_shots(config->shots)) {
        AM_LOG_ERROR("--shot inválido (\"quadro:arquivo.png,...\", até %u): %s", GAME_RUN_SHOT_MAX, config->shots);
        return false;
    }
    for (u32 i = 0; i < s_run.shot_count; i++) {
        const size_t len = strlen(s_run.shots[i].path);
        if (len < 5U || strcmp(&s_run.shots[i].path[len - 4U], ".png") != 0) {
            AM_LOG_ERROR("--shot: o arquivo tem de terminar em .png: %s", s_run.shots[i].path);
            return false;
        }
    }
    return true;
}

/* GX (trabalhadores, cache), janela e controle. */
/* config é o s_run.config: sem GPU utilizável, o desenho cai no software e config->gpu fica false. */
static bool init_video(GameRunConfig *config) {
    gx_fifo_set_render(config->render);
    gx_tex_set_cache(config->gx_tex_cache);
    if (config->render) {
        const u32 workers = config->gx_threads != 0U ? config->gx_threads : platform_cpu_count();
        if (workers > 1U && platform_parallel_init(workers)) {
            gx_raster_set_parallel(platform_parallel_run, workers);
        }
        AM_LOG_INFO("GX: rasterizador com %u trabalhador(es)", workers > 1U ? workers : 1U);
    }
    if (config->render && config->gpu) {
        if (platform_gpu_init((PlatformGpuShader)config->gpu_shader)) {
            gx_gpu_reset();
            gx_gpu_set_backend(platform_gpu_run);
            AM_LOG_INFO("GX: desenho na GPU (driver %s, ubershader TEV)", platform_gpu_name());
        } else {
            AM_LOG_WARN("--gfx gpu: sem GPU utilizável (%s); o GX desenha em software", platform_gpu_error());
            config->gpu = false;
        }
    }
    if (config->render && config->gx_thread) {
        static const GxFifoThreadApi s_api = {platform_thread_start, platform_thread_join,  platform_lock_create,
                                              platform_lock_destroy, platform_lock,         platform_unlock,
                                              platform_cond_create,  platform_cond_destroy, platform_cond_wait,
                                              platform_cond_signal,  platform_ticks_ns};
        /* O software amostra texturas da memória durante o desenho, fora da visão da época: espera em cada evento. */
        const u32 depth = config->gpu ? config->gx_depth : 0U;
        if (!gx_fifo_thread_start(&s_api, depth)) {
            AM_LOG_ERROR("GX: não consegui criar a thread do GX: %s", platform_last_error());
            return false;
        }
        AM_LOG_INFO("GX: numa thread própria (a CPU emulada só lê o fluxo por cima), até %u evento(s) atrás", depth);
    }
    if (config->window_scale == 0U) {
        return true;
    }
    if (!platform_window_open("Auto Modellista Recompiled", GAME_XFB_WIDTH, GAME_XFB_HEIGHT, config->window_scale)) {
        AM_LOG_ERROR("janela: %s", platform_last_error());
        return false;
    }
    s_run.window = true;
    s_run.live_input = config->pad_script == NULL;
    AM_LOG_INFO("controle: %s", s_run.live_input ? "teclado e gamepad da janela (docs/M7_MENUS.md §3)"
                                                 : "roteiro --pad-script (a janela não lê a entrada)");
    return true;
}

/* --audio-dump e som. */
static bool init_audio(const GameRunConfig *config) {
    if (config->audio_dump != NULL &&
        !audio_dump_open(config->audio_dump, DSP_AUDIO_RATE_HZ, config->audio_dump_max_mb * 1024ULL * 1024ULL)) {
        AM_LOG_ERROR("--audio-dump: não consegui criar %s", config->audio_dump);
        return false;
    }
    if (config->audio_out) {
        s_run.audio_out = platform_audio_open(DSP_AUDIO_RATE_HZ);
        if (!s_run.audio_out) {
            AM_LOG_WARN("áudio: sem dispositivo de som (%s); seguindo sem som", platform_last_error());
        }
    }
    dsp_set_audio_sink(on_audio, NULL);
    return true;
}

/* --events e --timebase-out. */
static bool init_logs(const GameRunConfig *config) {
    if (config->events != NULL) {
        s_run.events = fopen(config->events, "w");
        if (s_run.events == NULL) {
            AM_LOG_ERROR("não consegui criar %s", config->events);
            return false;
        }
        (void)fprintf(s_run.events, "# am-events v1 source=runtime\n");
        di_set_read_observer(on_di_read);
    }
    if (config->timebase_out == NULL) {
        return true;
    }
    s_run.timebase = fopen(config->timebase_out, "w");
    if (s_run.timebase == NULL) {
        AM_LOG_ERROR("não consegui criar %s", config->timebase_out);
        return false;
    }
    di_set_read_observer(on_di_read);
    return hook_register(GAME_ADDR_DVD_IRQ_HANDLER, "__DVDInterruptHandler", on_dvd_irq, NULL);
}

bool game_run_init(const GameRunConfig *config) {
    memset(&s_run, 0, sizeof(s_run));
    s_run.config = *config;
    if (!init_scripts(config) || !init_video(&s_run.config) || !init_audio(config)) {
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
    if (!init_logs(config) || !game_diag_init(config->profile, config->probes, s_run.timebase)) {
        return false;
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
        /* Rodapé no formato do cabeçalho do Dolphin (scripts/compare_events.py lê frames=); um am_fatal não chega
         * aqui, então log sem rodapé = execução que não terminou. */
        (void)fprintf(s_run.events, "# fim frames=%" PRIu64 " end=%s\n", s_run.frames,
                      s_run.target_reached ? "frames" : "parada");
        (void)fclose(s_run.events);
        s_run.events = NULL;
    }
    di_set_read_observer(NULL);
    game_diag_shutdown();
    if (s_run.timebase != NULL) {
        (void)fclose(s_run.timebase);
        s_run.timebase = NULL;
    }
    dsp_set_audio_sink(NULL, NULL);
    gx_fifo_thread_stop();
    gx_raster_set_parallel(NULL, 1);
    platform_parallel_shutdown();
    if (gx_gpu_active()) {
        gx_gpu_set_backend(NULL);
        platform_gpu_shutdown();
    }
    audio_dump_close();
    if (s_run.audio_out) {
        platform_audio_close();
        s_run.audio_out = false;
    }
    if (s_run.window) {
        platform_window_close();
        s_run.window = false;
    }
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

bool game_run_window_closed(void) {
    return s_run.window_closed;
}

bool game_run_frame_target_reached(void) {
    return s_run.target_reached;
}

static void report_audio(void);
static void report_gx(void);
static void report_gx_features(const GxSetupStats *su, const GxEfbStats *efb);

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
    if (exi_card_present()) {
        const ExiCardStats *mc = exi_card_stats();
        AM_LOG_INFO("memory card: %" PRIu64 " comandos; lido %" PRIu64 " bytes em %" PRIu64 " DMA(s) e %" PRIu64
                    " byte(s) avulsos; gravado %" PRIu64 " bytes em %" PRIu64 " DMA(s) e %" PRIu64
                    " PageProgram (%" PRIu64 " bytes por escrita imediata); %" PRIu64 " setores apagados",
                    mc->commands, mc->dma_read_bytes, mc->dma_reads, mc->array_bytes, mc->dma_write_bytes,
                    mc->dma_writes, mc->page_programs, mc->program_bytes, mc->sector_erases);
    }
    AM_LOG_INFO("VI: %" PRIu64 " campos, XFB 0x%08X; SI: %" PRIu64
                " leituras do controle; AI: streaming ligado %" PRIu64 " vez(es)",
                vi_field_count(), vi_xfb_top(), si_poll_count(), ai_stream_starts());
    const DspHleStats *dsp = dsp_hle_stats();
    AM_LOG_INFO("DSP: microcódigo 0x%08X (%u bytes de 0x%08X), %" PRIu64 " listas do AX, %" PRIu64
                " interrupções de DMA de áudio",
                dsp->ucode_crc, dsp->last_boot_len, dsp->last_boot_addr, dsp->command_lists,
                dsp_audio_dma_interrupts());
    report_audio();
    report_gx();
    AM_LOG_INFO("exceções: externa %" PRIu64 ", decrementer %" PRIu64 ", FP indisponível %" PRIu64,
                exception_count_of(EXCEPTION_NUM_EXTERNAL), exception_count_of(EXCEPTION_NUM_DECREMENTER),
                exception_count_of(EXCEPTION_NUM_FP_UNAVAILABLE));
}

static void report_audio(void) {
    const AxVoiceStats *ax = ax_voice_stats();
    const AxMixerStats *mix = ax_mixer_stats();
    AM_LOG_INFO("AX: %" PRIu64 " listas, %" PRIu64 " comandos (usados 0x%05X), %" PRIu64
                " quadros de 5 ms na MRAM, %" PRIu64 " amostras saturadas; vozes: até %" PRIu64 " por quadro, %" PRIu64
                " amostras lidas, %" PRIu64 " fins, %" PRIu64 " laços",
                mix->lists, mix->commands, mix->used_mask, mix->outputs, mix->clipped, ax->max_voices, ax->samples,
                ax->ended, ax->loops);
    AM_LOG_INFO("áudio: %" PRIu64 " quadros estéreo pela DMA (%.1f s a %u Hz)%s; tempo real: %" PRIu64
                " atraso(s) recomeçado(s), %" PRIu64 " quadro(s) pulado(s)",
                s_run.audio_frames, (double)s_run.audio_frames / DSP_AUDIO_RATE_HZ, DSP_AUDIO_RATE_HZ,
                audio_dump_frames() != 0U ? ", gravados no --audio-dump" : "", s_run.pace_rebases, s_run.skipped);
}

static void report_gx(void) {
    const GxFifoStats *gx = gx_fifo_stats();
    AM_LOG_INFO("GX: %" PRIu64 " bytes no gather pipe, %" PRIu64 " comandos, %" PRIu64 " desenhos (%" PRIu64
                " vértices), %" PRIu64 " BP, %" PRIu64 " DrawDone, %" PRIu64 " tokens, %" PRIu64 " cópias",
                gather_pipe_total_bytes(), gx->commands, gx->draws, gx->vertices, gx->bp_writes, gx->draw_done,
                gx->tokens, gx->copies);
    const GxSetupStats *su = gx_setup_stats();
    const GxRasterStats *ra = gx_raster_stats();
    const GxEfbStats *efb = gx_efb_stats();
    AM_LOG_INFO("GX render %s: %" PRIu64 " desenhos rasterizados, %" PRIu64 " triângulos (%" PRIu64
                " descartados pela face, %" PRIu64 " fora da tela), %" PRIu64 " pixels no TEV; %" PRIu64
                " desenhos em faixas paralelas",
                gx_fifo_render_enabled() ? "ligado" : "desligado", su->draws, ra->triangles, su->culled, su->rejected,
                ra->pixels, ra->parallel_draws);
    AM_LOG_INFO("EFB: %" PRIu64 " cópias para o XFB (última 0x%08X, %ux%u), %" PRIu64
                " para textura (formatos 0x%04X), %" PRIu64 " limpezas",
                efb->xfb_copies, efb->last_xfb_addr, efb->last_xfb_width, efb->last_xfb_height, efb->texture_copies,
                efb->copy_formats, efb->clears);
    AM_LOG_INFO("TEV: até %u estágios num desenho; %" PRIu64 " desenhos com textura de Z, %" PRIu64
                " com neblina, %" PRIu64 " com estágios indiretos; %" PRIu64 " cópias de Z para textura",
                su->max_stages, su->ztex_draws, su->fog_draws, su->indirect_draws, efb->depth_copies);
    report_gx_features(su, efb);
    if (gx_gpu_active()) {
        const GxGpuStats *g = gx_gpu_stats();
        AM_LOG_INFO("GX GPU: %" PRIu64 " lotes (%" PRIu64 " parciais), %" PRIu64 " desenhos, %" PRIu64
                    " triângulos; %" PRIu64 " texturas enviadas (%" PRIu64
                    " texels), buffer de texels recomeçado %" PRIu64 " vez(es); sem suporte: %" PRIu64
                    " desenhos com indireto, %" PRIu64 " com zfreeze",
                    g->batches, g->partial_batches, g->draws, g->triangles, g->texture_uploads, g->uploaded_texels,
                    g->arena_resets, g->indirect_draws, g->zfreeze_draws);
        u64 readbacks = 0;
        const u64 wait_us = platform_gpu_wait_us(&readbacks);
        AM_LOG_INFO("GX GPU: %" PRIu64 " leituras do EFB, espera média pela GPU de %" PRIu64 " µs", readbacks,
                    readbacks != 0U ? wait_us / readbacks : 0U);
    }
    if (gx_fifo_threaded()) {
        u64 syncs = 0;
        u64 waits = 0;
        u64 wait_ns[3] = {0};
        gx_fifo_thread_counters(&syncs, &waits, wait_ns);
        AM_LOG_INFO("GX thread: %" PRIu64 " sincronizações, %" PRIu64 " com o GX ainda trabalhando; espera de %" PRIu64
                    " ms no DrawDone, %" PRIu64 " ms nos tokens, %" PRIu64 " ms nas leituras do XFB; %" PRIu64
                    " páginas copiadas na escrita",
                    syncs, waits, wait_ns[0] / 1000000U, wait_ns[1] / 1000000U, wait_ns[2] / 1000000U,
                    mem_cow_copies());
    }
}

static void report_gx_features(const GxSetupStats *su, const GxEfbStats *efb) {
    const GxTexCacheStats *tc = gx_tex_cache_stats();
    AM_LOG_INFO("GX cache de texturas: %" PRIu64 " acertos, %" PRIu64 " decodificações (%" PRIu64 " texels)", tc->hits,
                tc->misses, tc->decoded_texels);
    AM_LOG_INFO("GX recursos: formatos de textura 0x%04X, TLUT 0x%X, até %u coordenadas; blend %" PRIu64
                ", lógica %" PRIu64 ", subtração %" PRIu64
                " desenhos; testes de alfa 0x%02X; neblina 0x%02X; estágios 0x%04X",
                su->tex_formats, su->tlut_formats, su->max_texgens, su->blend_draws, su->logic_draws,
                su->subtract_draws, su->alpha_tests, su->fog_types, su->stage_counts);
    for (u32 i = 0; i < efb->kind_count; i++) {
        const GxEfbCopyKind *k = &efb->kinds[i];
        AM_LOG_INFO("  cópia para textura: formato %u, %ux%u%s%s, %" PRIu64 " vez(es)", k->format, k->width, k->height,
                    k->half ? ", meia escala" : "", k->depth ? ", de Z" : "", k->count);
    }
}
