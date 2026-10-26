/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/platform.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "platform_internal.h"
#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <stdlib.h>
#include <windows.h>
#endif

static struct {
    bool ready;
    char error[256];
    SDL_AudioStream *audio;
    float volume; /* ganho da saída; vale 1 até platform_audio_set_volume */
    bool volume_set;
} s_platform;

/* Conjunto de trabalhadores: cada execução tem uma geração; os trabalhadores acordam com ela, pegam índices num
 * contador atômico e o último a terminar avisa quem chamou. */
static struct {
    SDL_Thread *threads[PLATFORM_MAX_WORKERS];
    u32 ids[PLATFORM_MAX_WORKERS]; /* argumento de cada thread: o número dela */
    u32 count;                     /* inclui quem chama */
    SDL_Mutex *lock;
    SDL_Condition *start;
    SDL_Condition *done;
    u64 generation;
    bool quit;
    u32 jobs;
    PlatformJobFn job;
    void *user;
    SDL_AtomicInt next;
    u32 pending;
} s_pool;

void platform_set_error(const char *msg) {
    (void)strncpy(s_platform.error, msg, sizeof(s_platform.error) - 1U);
}

static void set_error(const char *msg) {
    platform_set_error(msg);
}

#ifdef _WIN32
/* M9 parte 6: um processador lógico por núcleo físico. A CPU emulada e o GX são duas threads quentes; o Windows às
 * vezes as põe nos dois lados (SMT) do mesmo núcleo, e as duas andam a meia velocidade: na corrida com a GPU (Ryzen 7
 * 5700X3D) a média foi de 59 para 72 quadros/s só com esta máscara. AM_ALL_CPUS=1 deixa todos os lógicos. */
static void one_cpu_per_core(void) {
    const char *all = getenv("AM_ALL_CPUS");
    if (all != NULL && all[0] == '1') {
        return;
    }
    DWORD len = 0;
    (void)GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    if (len == 0U) {
        return;
    }
    u8 *buf = malloc(len);
    if (buf == NULL) {
        return;
    }
    DWORD_PTR mask = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len)) {
        for (DWORD off = 0; off < len;) {
            const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info =
                (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)(buf + off);
            const KAFFINITY core = info->Processor.GroupMask[0].Mask;
            if (info->Processor.GroupMask[0].Group == 0U && core != 0U) {
                mask |= core & (~core + 1U); /* o primeiro lógico do núcleo */
            }
            off += info->Size;
        }
    }
    free(buf);
    DWORD_PTR process = 0;
    DWORD_PTR system = 0;
    if (mask != 0U && GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && (mask & process) != 0U &&
        (mask & process) != process) {
        (void)SetProcessAffinityMask(GetCurrentProcess(), mask & process);
    }
}
#endif

bool platform_init(void) {
    if (s_platform.ready) {
        return true;
    }
#ifdef _WIN32
    one_cpu_per_core();
#endif
    /* Nenhum subsistema aqui: o vídeo sobe com a janela (M6); áudio e gamepad entram no M7. */
    if (!SDL_Init(0)) {
        set_error(SDL_GetError());
        return false;
    }
    s_platform.ready = true;
    if (!s_platform.volume_set) {
        s_platform.volume = 1.0F;
    }
    return true;
}

static void pool_take(u32 worker) {
    for (;;) {
        const int i = SDL_AddAtomicInt(&s_pool.next, 1);
        if (i < 0 || (u32)i >= s_pool.jobs) {
            return;
        }
        s_pool.job((u32)i, worker, s_pool.user);
    }
}

static int SDLCALL pool_thread(void *arg) {
    const u32 worker = *(const u32 *)arg;
    u64 seen = 0;
    SDL_LockMutex(s_pool.lock);
    for (;;) {
        while (!s_pool.quit && s_pool.generation == seen) {
            SDL_WaitCondition(s_pool.start, s_pool.lock);
        }
        if (s_pool.quit) {
            break;
        }
        seen = s_pool.generation;
        SDL_UnlockMutex(s_pool.lock);
        pool_take(worker);
        SDL_LockMutex(s_pool.lock);
        if (--s_pool.pending == 0U) {
            SDL_SignalCondition(s_pool.done);
        }
    }
    SDL_UnlockMutex(s_pool.lock);
    return 0;
}

/* M8 parte 2b: no macOS, threads novas têm QoS padrão e o escalonador as põe nos núcleos de eficiência quando há
 * disputa; a do GX e os trabalhadores andam junto com a CPU emulada (que é a thread principal, interativa). */
typedef struct ThreadStart {
    PlatformThreadFn fn;
    void *user;
} ThreadStart;

static int thread_trampoline(void *arg) {
    const ThreadStart start = *(const ThreadStart *)arg;
    SDL_free(arg);
#ifdef __APPLE__
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    return start.fn(start.user);
}

void *platform_thread_start(const char *name, PlatformThreadFn fn, void *user) {
    ThreadStart *start = SDL_malloc(sizeof(*start));
    if (start == NULL) {
        set_error("sem memória para a thread");
        return NULL;
    }
    start->fn = fn;
    start->user = user;
    SDL_Thread *t = SDL_CreateThread(thread_trampoline, name, start);
    if (t == NULL) {
        SDL_free(start);
        set_error(SDL_GetError());
    }
    return t;
}

void platform_thread_join(void *thread) {
    SDL_WaitThread((SDL_Thread *)thread, NULL);
}

void *platform_lock_create(void) {
    return SDL_CreateMutex();
}

void platform_lock_destroy(void *lock) {
    SDL_DestroyMutex((SDL_Mutex *)lock);
}

void platform_lock(void *lock) {
    SDL_LockMutex((SDL_Mutex *)lock);
}

void platform_unlock(void *lock) {
    SDL_UnlockMutex((SDL_Mutex *)lock);
}

void *platform_cond_create(void) {
    return SDL_CreateCondition();
}

void platform_cond_destroy(void *cond) {
    SDL_DestroyCondition((SDL_Condition *)cond);
}

void platform_cond_wait(void *cond, void *lock) {
    SDL_WaitCondition((SDL_Condition *)cond, (SDL_Mutex *)lock);
}

void platform_cond_signal(void *cond) {
    SDL_BroadcastCondition((SDL_Condition *)cond);
}

bool platform_parallel_init(u32 workers) {
    platform_parallel_shutdown();
    if (workers > PLATFORM_MAX_WORKERS) {
        workers = PLATFORM_MAX_WORKERS;
    }
    workers = workers == 0U ? 1U : workers;
    s_pool.lock = SDL_CreateMutex();
    s_pool.start = SDL_CreateCondition();
    s_pool.done = SDL_CreateCondition();
    if (s_pool.lock == NULL || s_pool.start == NULL || s_pool.done == NULL) {
        set_error(SDL_GetError());
        platform_parallel_shutdown();
        return false;
    }
    s_pool.count = 1;
    for (u32 i = 1; i < workers; i++) {
        s_pool.ids[i] = i;
        s_pool.threads[i] = SDL_CreateThread(pool_thread, "gx", &s_pool.ids[i]);
        if (s_pool.threads[i] == NULL) {
            set_error(SDL_GetError());
            platform_parallel_shutdown();
            return false;
        }
        s_pool.count++;
    }
    return true;
}

void platform_parallel_run(u32 jobs, PlatformJobFn job, void *user) {
    if (s_pool.count <= 1U) {
        for (u32 i = 0; i < jobs; i++) {
            job(i, 0, user);
        }
        return;
    }
    SDL_LockMutex(s_pool.lock);
    s_pool.jobs = jobs;
    s_pool.job = job;
    s_pool.user = user;
    SDL_SetAtomicInt(&s_pool.next, 0);
    s_pool.pending = s_pool.count - 1U;
    s_pool.generation++;
    SDL_BroadcastCondition(s_pool.start);
    SDL_UnlockMutex(s_pool.lock);
    pool_take(0);
    SDL_LockMutex(s_pool.lock);
    while (s_pool.pending != 0U) {
        SDL_WaitCondition(s_pool.done, s_pool.lock);
    }
    SDL_UnlockMutex(s_pool.lock);
}

void platform_parallel_shutdown(void) {
    if (s_pool.lock != NULL) {
        SDL_LockMutex(s_pool.lock);
        s_pool.quit = true;
        SDL_BroadcastCondition(s_pool.start);
        SDL_UnlockMutex(s_pool.lock);
    }
    for (u32 i = 1; i < PLATFORM_MAX_WORKERS; i++) {
        if (s_pool.threads[i] != NULL) {
            SDL_WaitThread(s_pool.threads[i], NULL);
        }
    }
    if (s_pool.lock != NULL) {
        SDL_DestroyMutex(s_pool.lock);
    }
    if (s_pool.start != NULL) {
        SDL_DestroyCondition(s_pool.start);
    }
    if (s_pool.done != NULL) {
        SDL_DestroyCondition(s_pool.done);
    }
    memset(&s_pool, 0, sizeof(s_pool));
}

u32 platform_cpu_count(void) {
    const int n = SDL_GetNumLogicalCPUCores();
    return n > 0 ? (u32)n : 1U;
}

void platform_shutdown(void) {
    platform_parallel_shutdown();
    platform_audio_close();
    platform_window_close();
    if (s_platform.ready) {
        SDL_Quit();
        s_platform.ready = false;
    }
}

const char *platform_last_error(void) {
    return s_platform.error;
}

u8 *platform_load_file(const char *path, size_t *size) {
    void *data = SDL_LoadFile(path, size);
    if (data == NULL) {
        set_error(SDL_GetError());
    }
    return (u8 *)data;
}

bool platform_save_file(const char *path, const u8 *data, size_t size) {
    /* Grava num arquivo ao lado e renomeia: uma queda no meio não estraga o save que já existia. */
    char tmp[1100];
    const int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        set_error("caminho longo demais");
        return false;
    }
    if (!SDL_SaveFile(tmp, data, size) || !SDL_RenamePath(tmp, path)) {
        set_error(SDL_GetError());
        return false;
    }
    return true;
}

/* No macOS fica onde sempre esteve (~/Library/Application Support/automodellista/AutoModellista/, o memory card do
 * M8); nos outros, sem a organização: $XDG_DATA_HOME/AutoModellista/ (~/.local/share/AutoModellista/) no Linux,
 * %APPDATA%/AutoModellista/ no Windows. */
bool platform_user_dir(char *out, size_t size) {
#ifdef __APPLE__
    char *dir = SDL_GetPrefPath("automodellista", "AutoModellista");
#else
    char *dir = SDL_GetPrefPath("", "AutoModellista");
#endif
    if (dir == NULL) {
        set_error(SDL_GetError());
        return false;
    }
    const int n = snprintf(out, size, "%s", dir);
    SDL_free(dir);
    return n > 0 && (size_t)n < size;
}

bool platform_config_dir(char *out, size_t size) {
    char *dir = SDL_GetPrefPath("", "AutoModellista");
    if (dir == NULL) {
        set_error(SDL_GetError());
        return false;
    }
    const int n = snprintf(out, size, "%s", dir);
    SDL_free(dir);
    return n > 0 && (size_t)n < size;
}

bool platform_path_exists(const char *path) {
    return SDL_GetPathInfo(path, NULL);
}

void platform_free_file(u8 *data) {
    SDL_free(data);
}

u64 platform_ticks_ms(void) {
    return (u64)SDL_GetTicks();
}

u64 platform_ticks_ns(void) {
    return (u64)SDL_GetTicksNS();
}

void platform_sleep_ns(u64 ns) {
    SDL_DelayPrecise(ns);
}

bool platform_audio_open(u32 rate_hz) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        set_error(SDL_GetError());
        return false;
    }
    const SDL_AudioSpec spec = {.format = SDL_AUDIO_S16, .channels = 2, .freq = (int)rate_hz};
    s_platform.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (s_platform.audio == NULL) {
        set_error(SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    (void)SDL_SetAudioStreamGain(s_platform.audio, s_platform.volume);
    (void)SDL_ResumeAudioStreamDevice(s_platform.audio);
    return true;
}

void platform_audio_push(const s16 *lr, u32 frames) {
    if (s_platform.audio != NULL) {
        (void)SDL_PutAudioStreamData(s_platform.audio, lr, (int)(frames * 4U));
    }
}

u32 platform_audio_queued_frames(void) {
    if (s_platform.audio == NULL) {
        return 0;
    }
    const int bytes = SDL_GetAudioStreamQueued(s_platform.audio);
    return bytes > 0 ? (u32)bytes / 4U : 0U;
}

void platform_audio_set_volume(float gain) {
    s_platform.volume = gain < 0.0F ? 0.0F : (gain > 1.0F ? 1.0F : gain);
    s_platform.volume_set = true;
    if (s_platform.audio != NULL) {
        (void)SDL_SetAudioStreamGain(s_platform.audio, s_platform.volume);
    }
}

void platform_audio_close(void) {
    if (s_platform.audio != NULL) {
        SDL_DestroyAudioStream(s_platform.audio);
        s_platform.audio = NULL;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

int platform_sdl_version(void) {
    return SDL_GetVersion();
}

bool platform_make_dir(const char *path) {
    if (!SDL_CreateDirectory(path)) {
        set_error(SDL_GetError());
        return false;
    }
    return true;
}

bool platform_save_png(const char *path, u8 *rgba, u32 width, u32 height) {
    SDL_Surface *s = SDL_CreateSurfaceFrom((int)width, (int)height, SDL_PIXELFORMAT_RGBA32, rgba, (int)(width * 4U));
    if (s == NULL) {
        set_error(SDL_GetError());
        return false;
    }
    const bool ok = SDL_SavePNG(s, path);
    if (!ok) {
        set_error(SDL_GetError());
    }
    SDL_DestroySurface(s);
    return ok;
}
