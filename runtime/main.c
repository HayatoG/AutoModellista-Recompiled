/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Executável da runtime.
 *
 *   automodellista                       autoteste do M3: liga o C gerado e confere o dispatcher (usado no CI)
 *   automodellista --sys <dir> [opções]  M4: carrega o DOL de <dir> (sys/ do dtk disc extract) e roda até main
 *   automodellista --sys <dir> --frames <n>  M5: roda o jogo (disco, RELs) até completar n quadros do laço principal
 *
 * Opções do boot (docs/M4_BOOT.md §6):
 *   --trace <arquivo>          grava o trace de chamadas (formato de scripts/am_trace_lib.py)
 *   --stop-at <hex>            endereço de parada (padrão: main, 0x800719AC)
 *   --max-steps <n>            limite de voltas do chassi (padrão: sem limite)
 *   --dump-boot-state <pref>   grava <pref>.mem1.bin e <pref>.json no entry point e encerra
 *   --console devkit|retail    tipo de console em 0x8000002C (padrão devkit, como o Dolphin)
 *   --rtc <segundos>           relógio inicial desde 2000-01-01 (padrão 0)
 *   --trace-max-mb <n>         teto do trace em MiB (padrão 256); passou disso é am_fatal
 *   --max-seconds <n>          watchdog de tempo real (padrão 120; 0 = sem limite)
 *   --same-pc-limit <n>        watchdog: mesmo pc despachado n vezes seguidas sem chamada nova (padrão 2000000)
 *   --frames <n>               M5: para depois de n quadros de fn_80051034 (sem parada em main)
 *   --disc <dir>               raiz da árvore extraída (sys/ e files/); padrão: <--sys>/..
 *   --pad-script <roteiro>     botões do controle 1 por quadro, "200:D_LEFT:8,240:A:8" (am/pad_script.h)
 *   --events <arquivo>         log de eventos do M5 (main, OSLink, leituras do DVD) para comparar com o Dolphin
 *   --gfx soft|off             M6: GX em software (padrão) ou só contar comandos, como no M5
 *   --shot <q:arq.png,...>     M6: no início do quadro q grava o XFB em PNG (o mesmo que dolphin_trace.py --shots)
 *   --window <escala>          M6: janela SDL3 com o XFB que o VI mostra (1, 2...); com --shot grava também
 *                              <arq>_janela.png, lido de volta do que a janela desenhou. M7: sem --pad-script, o
 *                              controle vem do teclado e do gamepad (docs/M7_MENUS.md §3)
 *   --audio-dump <arq.wav>     M7: grava a saída da DMA de áudio (32028 Hz, o mesmo formato do dspdump.wav do Dolphin),
 *                              no ritmo do tempo emulado: igual em qualquer velocidade e arquitetura
 *   --audio-dump-max-mb <n>    teto do --audio-dump em MiB (padrão 256); passou disso é am_fatal
 *   --audio on|off             M7: som pelo dispositivo padrão (padrão: on com --window, off sem janela)
 *   --realtime on|off          M7: segura o tempo emulado no ritmo do relógio (padrão: on com --window)
 *   --verbose                  log de depuração (acessos MMIO, UART)
 *
 * Caminhos relativos de --trace/--dump-boot-state são resolvidos em $AM_LOG_DIR quando definido (os scripts usam
 * /Volumes/AMRecomp/logs): saída de execução nunca vai para o repositório. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am/boot.h"
#include "am/bus.h"
#include "am/chassis.h"
#include "am/dispatch.h"
#include "am/dol.h"
#include "am/exception.h"
#include "am/exi.h"
#include "am/game.h"
#include "am/game_run.h"
#include "am/guest_cpu.h"
#include "am/hook.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/platform.h"
#include "am/ppc_sys.h"
#include "am/recomp.h"
#include "am/timebase.h"
#include "am/trace.h"

typedef struct Options {
    const char *sys_dir;
    const char *trace_path;
    const char *dump_prefix;
    GuestAddr stop_at;
    u64 max_steps;
    u32 console_type;
    u64 rtc_seconds;
    u64 trace_max_mb;
    u64 max_seconds;
    u64 same_pc_limit;
    u64 frames;
    const char *disc_root;
    const char *events;
    const char *pad_script;
    const char *shots;
    u64 window_scale;
    const char *audio_dump;
    u64 audio_dump_max_mb;
    int audio;    /* -1 = padrão */
    int realtime; /* -1 = padrão */
    bool gfx_off;
    bool stop_given;
    bool verbose;
} Options;

typedef struct LoadedFile {
    u8 *data;
    size_t size;
} LoadedFile;

typedef struct SysFiles {
    LoadedFile boot_bin;
    LoadedFile bi2;
    LoadedFile fst;
    LoadedFile dol;
} SysFiles;

static bool check_entry(const char *what, GuestAddr addr) {
    if (dispatch_lookup(addr) == NULL) {
        AM_LOG_ERROR("%s (0x%08X) não está no dispatcher", what, addr);
        return false;
    }
    return true;
}

static bool verify_generated(void) {
    bool ok = true;
    for (size_t i = 0; i < recomp_module_count(); i++) {
        AM_LOG_INFO("módulo %-9s entrada 0x%08X  trechos %zu", recomp_module_name(i), recomp_module_entry(i),
                    recomp_module_chunks(i));
        ok = check_entry(recomp_module_name(i), recomp_module_entry(i)) && ok;
    }
    ok = check_entry("OSInit", GAME_ADDR_OS_INIT) && ok;
    ok = check_entry("main", GAME_ADDR_MAIN) && ok;
    return ok;
}

static bool parse_u64(const char *text, u64 *out) {
    char *end = NULL;
    const unsigned long long v = strtoull(text, &end, 0);
    if (end == text || *end != '\0') {
        return false;
    }
    *out = (u64)v;
    return true;
}

static bool parse_on_off(const char *text, int *out) {
    if (strcmp(text, "on") == 0 || strcmp(text, "off") == 0) {
        *out = strcmp(text, "on") == 0 ? 1 : 0;
        return true;
    }
    return false;
}

static bool parse_option(Options *o, const char *name, const char *value) {
    u64 n = 0;
    if (strcmp(name, "--sys") == 0) {
        o->sys_dir = value;
    } else if (strcmp(name, "--trace") == 0) {
        o->trace_path = value;
    } else if (strcmp(name, "--dump-boot-state") == 0) {
        o->dump_prefix = value;
    } else if (strcmp(name, "--stop-at") == 0 && parse_u64(value, &n) && n <= 0xFFFFFFFFU) {
        o->stop_at = (GuestAddr)n;
        o->stop_given = true;
    } else if (strcmp(name, "--frames") == 0 && parse_u64(value, &n) && n > 0U) {
        o->frames = n;
    } else if (strcmp(name, "--disc") == 0) {
        o->disc_root = value;
    } else if (strcmp(name, "--events") == 0) {
        o->events = value;
    } else if (strcmp(name, "--pad-script") == 0) {
        o->pad_script = value;
    } else if (strcmp(name, "--shot") == 0) {
        o->shots = value;
    } else if (strcmp(name, "--window") == 0 && parse_u64(value, &n) && n > 0U && n <= 8U) {
        o->window_scale = n;
    } else if (strcmp(name, "--gfx") == 0 && (strcmp(value, "soft") == 0 || strcmp(value, "off") == 0)) {
        o->gfx_off = strcmp(value, "off") == 0;
    } else if (strcmp(name, "--audio-dump") == 0) {
        o->audio_dump = value;
    } else if (strcmp(name, "--audio-dump-max-mb") == 0 && parse_u64(value, &n) && n > 0U) {
        o->audio_dump_max_mb = n;
    } else if (strcmp(name, "--audio") == 0 && parse_on_off(value, &o->audio)) {
    } else if (strcmp(name, "--realtime") == 0 && parse_on_off(value, &o->realtime)) {
    } else if (strcmp(name, "--max-steps") == 0 && parse_u64(value, &n)) {
        o->max_steps = n;
    } else if (strcmp(name, "--trace-max-mb") == 0 && parse_u64(value, &n) && n > 0U) {
        o->trace_max_mb = n;
    } else if (strcmp(name, "--max-seconds") == 0 && parse_u64(value, &n)) {
        o->max_seconds = n;
    } else if (strcmp(name, "--same-pc-limit") == 0 && parse_u64(value, &n) && n > 0U) {
        o->same_pc_limit = n;
    } else if (strcmp(name, "--rtc") == 0 && parse_u64(value, &n)) {
        o->rtc_seconds = n;
    } else if (strcmp(name, "--console") == 0 && strcmp(value, "devkit") == 0) {
        o->console_type = BOOT_CONSOLE_LATEST_DEVKIT;
    } else if (strcmp(name, "--console") == 0 && strcmp(value, "retail") == 0) {
        o->console_type = BOOT_CONSOLE_RETAIL;
    } else {
        return false;
    }
    return true;
}

static bool parse_args(int argc, char **argv, Options *o) {
    *o = (Options){.stop_at = GAME_ADDR_MAIN,
                   .console_type = BOOT_CONSOLE_LATEST_DEVKIT,
                   .trace_max_mb = 256U,
                   .audio_dump_max_mb = 256U,
                   .audio = -1,
                   .realtime = -1,
                   .max_seconds = 120U,
                   .same_pc_limit = CHASSIS_DEFAULT_SAME_PC_LIMIT};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0) {
            o->verbose = true;
        } else if (i + 1 < argc && parse_option(o, argv[i], argv[i + 1])) {
            i++;
        } else {
            AM_LOG_ERROR("opção inválida: %s (veja o comentário no topo de runtime/main.c)", argv[i]);
            return false;
        }
    }
    if (o->frames != 0U && !o->stop_given) {
        o->stop_at = 0; /* M5: o jogo segue depois de main */
    }
    return true;
}

/* Caminho relativo vai para $AM_LOG_DIR, se definido. `buf` precisa durar enquanto o caminho for usado. */
static const char *log_path(const char *path, char *buf, size_t size) {
    const char *dir = getenv("AM_LOG_DIR");
    if (path == NULL || path[0] == '/' || dir == NULL || dir[0] == '\0') {
        return path;
    }
    const int n = snprintf(buf, size, "%s/%s", dir, path);
    return n > 0 && (size_t)n < size ? buf : path;
}

/* --shot: cada caminho relativo vai para $AM_LOG_DIR, como em log_path. */
static const char *log_path_list(const char *list, char *buf, size_t size) {
    const char *dir = getenv("AM_LOG_DIR");
    if (list == NULL || dir == NULL || dir[0] == '\0') {
        return list;
    }
    size_t used = 0;
    buf[0] = '\0';
    for (const char *p = list; *p != '\0';) {
        const char *colon = strchr(p, ':');
        const char *comma = strchr(p, ',');
        const size_t item = comma != NULL ? (size_t)(comma - p) : strlen(p);
        if (colon == NULL || (comma != NULL && colon > comma)) {
            return list; /* formato inválido: game_run reclama */
        }
        const size_t head = (size_t)(colon - p) + 1U;
        const bool relative = colon[1] != '/';
        const int n = snprintf(buf + used, size - used, "%s%.*s%s%s%.*s", used != 0U ? "," : "", (int)head, p,
                               relative ? dir : "", relative ? "/" : "", (int)(item - head), colon + 1);
        if (n < 0 || (size_t)n >= size - used) {
            return list;
        }
        used += (size_t)n;
        p += item + (comma != NULL ? 1U : 0U);
    }
    return buf;
}

static bool load_one(const char *dir, const char *name, LoadedFile *out) {
    char path[1024];
    const int n = snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        AM_LOG_ERROR("caminho longo demais: %s/%s", dir, name);
        return false;
    }
    out->data = platform_load_file(path, &out->size);
    if (out->data == NULL) {
        AM_LOG_ERROR("não consegui ler %s: %s", path, platform_last_error());
        return false;
    }
    return true;
}

static bool load_sys(const char *dir, SysFiles *f) {
    return load_one(dir, "boot.bin", &f->boot_bin) && load_one(dir, "bi2.bin", &f->bi2) &&
           load_one(dir, "fst.bin", &f->fst) && load_one(dir, "main.dol", &f->dol);
}

static void free_sys(SysFiles *f) {
    platform_free_file(f->boot_bin.data);
    platform_free_file(f->bi2.data);
    platform_free_file(f->fst.data);
    platform_free_file(f->dol.data);
}

/* Mesmo formato de scripts/dolphin_trace.py boot-state, para scripts/compare_boot_state.py. */
static bool dump_boot_state(const CPUState *cpu, const char *prefix) {
    char path[1024];
    (void)snprintf(path, sizeof(path), "%s.mem1.bin", prefix);
    FILE *f = fopen(path, "wb");
    const size_t mem1_size = (size_t)MEM_MEM1_SIZE;
    if (f == NULL || fwrite(mem_mem1_data(), 1, mem1_size, f) != mem1_size) {
        AM_LOG_ERROR("não consegui gravar %s", path);
        if (f != NULL) {
            (void)fclose(f);
        }
        return false;
    }
    (void)fclose(f);
    (void)snprintf(path, sizeof(path), "%s.json", prefix);
    f = fopen(path, "w");
    if (f == NULL) {
        return false;
    }
    (void)fprintf(f, "{\n \"source\": \"runtime\",\n \"registers\": {\n");
    for (u32 i = 0; i < 32U; i++) {
        (void)fprintf(f, "  \"r%u\": \"0x%08X\",\n", i, cpu->gpr[i]);
    }
    (void)fprintf(
        f,
        "  \"pc\": \"0x%08X\",\n  \"msr\": \"0x%08X\",\n  \"cr\": \"0x%08X\",\n  \"lr\": \"0x%08X\",\n"
        "  \"ctr\": \"0x%08X\",\n  \"xer\": \"0x%08X\",\n  \"fpscr\": \"0x%08X\",\n  \"pvr\": \"0x%08X\",\n"
        "  \"srr0\": \"0x%08X\",\n  \"srr1\": \"0x%08X\",\n  \"tbl\": \"0x%08X\",\n  \"tbu\": \"0x%08X\",\n"
        "  \"dec\": \"0x%08X\",\n  \"hid0\": \"0x%08X\",\n  \"hid1\": \"0x%08X\",\n  \"hid2\": \"0x%08X\",\n"
        "  \"sprg0\": \"0x%08X\",\n  \"sprg1\": \"0x%08X\",\n  \"sprg2\": \"0x%08X\",\n  \"sprg3\": \"0x%08X\"\n"
        " }\n}\n",
        cpu->pc, cpu->msr, cpu->cr, cpu->lr, cpu->ctr, cpu->xer, cpu->fpscr, PPC_GEKKO_PVR, cpu->srr0, cpu->srr1,
        (u32)timebase_read(cpu), (u32)(timebase_read(cpu) >> 32), timebase_dec_read(cpu), ppc_sys_spr(PPC_SPR_HID0),
        ppc_sys_spr(PPC_SPR_HID1), cpu->hid2, ppc_sys_spr(PPC_SPR_SPRG0), ppc_sys_spr(PPC_SPR_SPRG0 + 1U),
        ppc_sys_spr(PPC_SPR_SPRG0 + 2U), ppc_sys_spr(PPC_SPR_SPRG0 + 3U));
    (void)fclose(f);
    AM_LOG_INFO("estado no entry point gravado em %s.{mem1.bin,json}", prefix);
    return true;
}

static bool load_dol(const SysFiles *f, DolImage *dol) {
    DolResult r = dol_parse(f->dol.data, f->dol.size, dol);
    if (r == DOL_OK) {
        r = dol_load(dol, f->dol.data, f->dol.size);
    }
    if (r != DOL_OK) {
        AM_LOG_ERROR("main.dol: %s", dol_result_name(r));
        return false;
    }
    if (recomp_module_count() == 0U || dol->entry != recomp_module_entry(0)) {
        AM_LOG_ERROR("entry point do DOL (0x%08X) não é o do C gerado (0x%08X): DOL diferente do recompilado?",
                     dol->entry, recomp_module_count() != 0U ? recomp_module_entry(0) : 0U);
        return false;
    }
    /* Toda seção de texto é código recompilado: um icbi sobre ela com conteúdo diferente do DOL é fatal. */
    for (u32 i = 0; i < DOL_TEXT_COUNT; i++) {
        const DolSection *s = &dol->sections[i];
        if (s->size != 0U) {
            AM_ASSERT(ppc_sys_protect_code(s->addr, s->size, f->dol.data + s->offset), "faixas de código demais");
        }
    }
    return true;
}

static bool prepare(CPUState *cpu, const Options *o, const SysFiles *f, DolImage *dol) {
    ppc_sys_reset();
    bus_reset();
    if (!load_dol(f, dol)) {
        return false;
    }
    const BootFiles files = {f->boot_bin.data, f->boot_bin.size, f->bi2.data, f->bi2.size, f->fst.data, f->fst.size};
    const BootConfig config = {.console_type = o->console_type, .rtc_seconds = o->rtc_seconds};
    BootLayout layout;
    const BootResult r = boot_prepare_memory(&files, &config, &layout);
    if (r != BOOT_OK) {
        AM_LOG_ERROR("boot: %s", boot_result_name(r));
        return false;
    }
    boot_prepare_cpu(cpu, dol->entry, &layout);
    exi_set_rtc((u32)o->rtc_seconds);
    const ExceptionLayout exc = {GAME_ADDR_OS_EV_START,        GAME_ADDR_OS_EV_END,   GAME_ADDR_OS_EV_SET_NUMBER,
                                 GAME_ADDR_OS_DEFAULT_HANDLER, GAME_ADDR_OS_SC_START, GAME_ADDR_OS_SC_END};
    exception_init(&exc);
    AM_LOG_INFO("DOL carregado: entry 0x%08X; FST em 0x%08X, bi2 em 0x%08X; console 0x%08X", dol->entry,
                layout.fst_addr, layout.bi2_addr, o->console_type);
    return true;
}

static void report(const CPUState *cpu, ChassisStop stop, const ChassisStats *st) {
    const PpcSysStats *ps = ppc_sys_stats();
    AM_LOG_INFO("chassi: %" PRIu64 " voltas, %" PRIu64 " interrupção(ões), %" PRIu64 " sc, %" PRIu64
                " ciclos estimados, trace %" PRIu64 " chamadas",
                st->steps, st->interrupts, exception_system_call_count(), timebase_cycles(NULL), trace_event_count());
    AM_LOG_INFO("cache: dcbst %" PRIu64 ", dcbf %" PRIu64 ", dcbi %" PRIu64 ", icbi %" PRIu64 " (%" PRIu64
                " sobre código intacto, %" PRIu64 " fora do código)",
                ps->cache_ops[PPC_SYS_CACHE_DCBST], ps->cache_ops[PPC_SYS_CACHE_DCBF],
                ps->cache_ops[PPC_SYS_CACHE_DCBI], ps->cache_ops[PPC_SYS_CACHE_ICBI], ps->icbi_in_code,
                ps->icbi_elsewhere);
    for (u32 i = 0; i < ps->icbi_first_count; i++) {
        AM_LOG_DEBUG("  icbi[%u] bloco 0x%08X", i, ps->icbi_first[i]);
    }
    game_run_report();
    if (stop == CHASSIS_STOP_ADDRESS) {
        AM_LOG_INFO("parada em 0x%08X (lr=0x%08X)%s", cpu->pc, cpu->lr,
                    cpu->pc == GAME_ADDR_MAIN ? ": M4: main alcançado" : "");
    } else if (stop == CHASSIS_STOP_HOOK && game_run_window_closed()) {
        AM_LOG_INFO("parada em 0x%08X: janela fechada no quadro %" PRIu64, cpu->pc, game_run_frames());
    } else if (stop == CHASSIS_STOP_HOOK && game_run_frame_target_reached()) {
        AM_LOG_INFO("parada em 0x%08X: M5: %" PRIu64 " quadros completos do laço fn_80051034", cpu->pc,
                    game_run_frames());
    } else {
        AM_LOG_INFO("parada por limite de voltas em pc=0x%08X", cpu->pc);
    }
}

/* Todo gancho tem de ser alcançável pelo chassi (am/hook.h): confere os desvios diretos do texto do DOL. */
static bool check_hooks(const SysFiles *f, const DolImage *dol) {
    HookCodeRange ranges[DOL_TEXT_COUNT];
    u32 count = 0;
    for (u32 i = 0; i < DOL_TEXT_COUNT; i++) {
        const DolSection *s = &dol->sections[i];
        if (s->size != 0U) {
            ranges[count++] = (HookCodeRange){s->addr, s->size, f->dol.data + s->offset};
        }
    }
    if (!hook_check_reachable(ranges, count)) {
        AM_LOG_ERROR("há ganchos que o chassi não veria; ver o log acima");
        return false;
    }
    return true;
}

static bool open_trace(const char *path, const Options *o) {
    char chunks[1024];
    const int n = snprintf(chunks, sizeof(chunks), "%s.chunks", path);
    return n > 0 && (size_t)n < sizeof(chunks) && trace_write_chunks(chunks) &&
           trace_open(path, "dolphin_equiv=2609a", o->trace_max_mb * 1024ULL * 1024ULL);
}

static int run_boot(CPUState *cpu, const Options *o) {
    if (!recomp_is_generated()) {
        AM_LOG_ERROR("--sys exige o C gerado (compile com AM_WITH_GENERATED=ON)");
        return EXIT_FAILURE;
    }
    SysFiles files = {0};
    DolImage dol;
    int status = EXIT_FAILURE;
    char disc_buf[1024];
    char events_buf[1024];
    char shots_buf[4096];
    char audio_buf[1024];
    const bool window = o->window_scale != 0U;
    (void)snprintf(disc_buf, sizeof(disc_buf), "%s/..", o->sys_dir);
    const GameRunConfig run = {.disc_root = o->disc_root != NULL ? o->disc_root : disc_buf,
                               .frames = o->frames,
                               .events = log_path(o->events, events_buf, sizeof(events_buf)),
                               .pad_script = o->pad_script,
                               .render = !o->gfx_off,
                               .shots = log_path_list(o->shots, shots_buf, sizeof(shots_buf)),
                               .window_scale = (u32)o->window_scale,
                               .audio_dump = log_path(o->audio_dump, audio_buf, sizeof(audio_buf)),
                               .audio_dump_max_mb = o->audio_dump_max_mb,
                               .audio_out = o->audio >= 0 ? o->audio == 1 : window,
                               .realtime = o->realtime >= 0 ? o->realtime == 1 : window};
    if (load_sys(o->sys_dir, &files) && prepare(cpu, o, &files, &dol) && game_run_init(&run) &&
        check_hooks(&files, &dol)) {
        char dump_buf[1024];
        char trace_buf[1024];
        const char *dump = log_path(o->dump_prefix, dump_buf, sizeof(dump_buf));
        const char *trace = log_path(o->trace_path, trace_buf, sizeof(trace_buf));
        if (dump != NULL) {
            status = dump_boot_state(cpu, dump) ? EXIT_SUCCESS : EXIT_FAILURE;
        } else if (trace != NULL && !open_trace(trace, o)) {
            AM_LOG_ERROR("não consegui abrir o trace %s", trace);
        } else {
            const ChassisConfig cfg = {.stop_at = o->stop_at,
                                       .max_steps = o->max_steps,
                                       .same_pc_limit = o->same_pc_limit,
                                       .max_wall_ms = o->max_seconds * 1000U,
                                       .now_ms = platform_ticks_ms,
                                       .on_tick = o->frames != 0U ? game_run_tick : NULL};
            ChassisStats stats = {0};
            const ChassisStop stop = chassis_run(cpu, &cfg, &stats);
            trace_close();
            report(cpu, stop, &stats);
            const bool frames_ok = stop == CHASSIS_STOP_HOOK && game_run_frame_target_reached();
            status = stop == CHASSIS_STOP_ADDRESS || frames_ok ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
    game_run_shutdown();
    hook_clear();
    free_sys(&files);
    return status;
}

static int run_selftest(void) {
    if (!recomp_is_generated() || verify_generated()) {
        AM_LOG_INFO("M3: esqueleto OK");
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

int main(int argc, char **argv) {
    Options opts;
    if (!parse_args(argc, argv, &opts)) {
        return EXIT_FAILURE;
    }
    am_log_set_level(opts.verbose ? AM_LOG_LEVEL_DEBUG : AM_LOG_LEVEL_INFO);
    if (!platform_init()) {
        AM_LOG_ERROR("SDL: %s", platform_last_error());
        return EXIT_FAILURE;
    }
    const int sdl = platform_sdl_version();
    AM_LOG_INFO("SDL %d.%d.%d", sdl / 1000000, (sdl / 1000) % 1000, sdl % 1000);
    if (!mem_init()) {
        AM_LOG_ERROR("sem memória para MEM1/ARAM");
        platform_shutdown();
        return EXIT_FAILURE;
    }
    int status = EXIT_FAILURE;
    CPUState cpu;
    if (guest_cpu_create(&cpu)) {
        am_fatal_set_cpu(&cpu);
        const size_t chunks = recomp_register_all();
        AM_LOG_INFO("C gerado: %s; %zu trecho(s) no dispatcher", recomp_is_generated() ? "ligado" : "ausente (stub)",
                    chunks);
        status = opts.sys_dir != NULL ? run_boot(&cpu, &opts) : run_selftest();
        am_fatal_set_cpu(NULL);
        guest_cpu_destroy(&cpu);
    }
    dispatch_clear();
    mem_shutdown();
    platform_shutdown();
    return status;
}
