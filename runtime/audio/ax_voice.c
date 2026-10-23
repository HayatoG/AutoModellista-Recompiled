/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/ax_voice.h"

#include <string.h>

#include "am/log.h"
#include "am/mem.h"

#define AX_MS_PER_FRAME    5U
#define AX_SAMPLES_PER_MS  32U /* 32 kHz */
#define AX_UPDATES_MAX     32U
#define AX_PB_LIST_MAX     256U
#define AX_SRC_POLYPHASE   0U
#define AX_SRC_LINEAR      1U
#define AX_ADDR_START_MASK 0x3FFFFFFFU
#define AX_ADDR_CUR_MASK   0xBFFFFFFFU

/* Estado do acelerador do DSP durante uma voz (DSPAccelerator.h). */
typedef struct Accel {
    u16 *pb;
    u32 start;
    u32 end;
    u32 cur;
    u16 format; /* bits 0-1 tamanho, 2-3 decodificação, 4-5 ganho */
    s16 gain;
    s16 yn1;
    s16 yn2;
    u16 pred_scale;
    bool stopped;
} Accel;

static struct {
    AxVoiceStats stats;
} s_ax;

void ax_voice_reset(void) {
    memset(&s_ax, 0, sizeof(s_ax));
}

const AxVoiceStats *ax_voice_stats(void) {
    return &s_ax.stats;
}

static u32 hilo(const u16 *pb, u32 at) {
    return ((u32)pb[at] << 16) | pb[at + 1U];
}

static u8 aram8(u32 address) {
    return mem_aram_read8(address & (MEM_ARAM_SIZE - 1U)); /* DSPManager::ReadARAM: ptr[address & mask] */
}

/* Accelerator::GetCurrentSample. */
static s16 current_sample(const Accel *a) {
    switch (a->format & 3U) {
    case 0U: {
        const u8 b = aram8(a->cur >> 1);
        return (s16)((a->cur & 1U) != 0U ? (b & 0xFU) : (b >> 4));
    }
    case 1U:
        return (s16)aram8(a->cur);
    case 2U:
        return (s16)(((u16)aram8(a->cur * 2U) << 8) | aram8((a->cur * 2U) + 1U));
    default:
        am_fatal("AX: formato de amostra 0x%04X com tamanho inválido", a->format);
    }
}

/* HLEAccelerator::OnSampleReadEndException. */
static void end_of_sample(Accel *a) {
    u16 *pb = a->pb;
    if (pb[AX_PB_ADDR_LOOPING] != 0U) {
        a->pred_scale = (u16)(pb[AX_PB_LOOP_PS] & 0x7FU);
        if (pb[AX_PB_IS_STREAM] != 1U) {
            a->yn1 = (s16)pb[AX_PB_LOOP_YN1];
            a->yn2 = (s16)pb[AX_PB_LOOP_YN2];
        } else {
            pb[AX_PB_LOOP_COUNTER]++;
        }
        a->stopped = false; /* SetYn2 retoma as leituras */
        s_ax.stats.loops++;
    } else {
        pb[AX_PB_RUNNING] = 0;
        s_ax.stats.ended++;
    }
}

static s16 sat16(s32 v, s32 lo) {
    if (v < lo) {
        return (s16)lo;
    }
    return (s16)(v > 0x7FFF ? 0x7FFF : v);
}

/* Accelerator::ReadSample (só as decodificações que leem a ARAM: ADPCM e PCM). */
static s16 read_sample(Accel *a) {
    if (a->stopped) {
        return 0;
    }
    const u32 decode = (a->format >> 2) & 3U;
    if (decode == 1U || decode == 3U) {
        am_fatal("AX: formato 0x%04X lê da entrada MMIO do acelerador, não suportado", a->format);
    }
    s32 raw = current_sample(a);
    const u32 coef_idx = (a->pred_scale >> 4) & 7U;
    const s32 c1 = (s16)a->pb[AX_PB_ADPCM_COEFS + (coef_idx * 2U)];
    const s32 c2 = (s16)a->pb[AX_PB_ADPCM_COEFS + (coef_idx * 2U) + 1U];
    s16 val = 0;
    u32 step = 2;
    s_ax.stats.samples++;
    if (decode == 0U) {
        raw &= 0xF;
        if (raw >= 8) {
            raw -= 16;
        }
        const s32 v = ((1 << (a->pred_scale & 0xFU)) * raw) + ((0x400 + (c1 * a->yn1) + (c2 * a->yn2)) >> 11);
        val = sat16(v, -0x7FFF);
        a->yn2 = a->yn1;
        a->yn1 = val;
        a->cur++;
        if ((a->end & 0xFU) == 0U && a->cur == a->end) {
            a->cur = a->start + 1U;
        } else if ((a->end & 0xFU) == 1U && a->cur == a->end - 1U) {
            a->cur = a->start;
        } else if ((a->cur & 15U) == 0U) {
            a->pred_scale = aram8((a->cur & ~15U) >> 1);
            a->cur += 2U;
            step += 2U;
        }
    } else {
        static const u8 s_gain_shift[4] = {11, 0, 16, 0};
        const u32 sh = s_gain_shift[(a->format >> 4) & 3U];
        const s32 v = ((a->gain * (s32)(s16)raw) >> sh) + ((c1 * a->yn1) >> sh) + ((c2 * a->yn2) >> sh);
        val = (s16)v;
        a->yn2 = a->yn1;
        a->yn1 = val;
        a->cur++;
    }
    if (a->cur == a->end + step - 1U) {
        a->cur = a->start;
        a->stopped = true;
        end_of_sample(a);
    }
    a->cur &= AX_ADDR_CUR_MASK;
    return val;
}

/* GetInputSamples + ResampleAudio (linear; com polifásico e sem coeficientes o Dolphin também usa o linear). O
 * resultado das amostras não é usado (sem mixagem), mas o estado (posição fracionária, últimas 4 amostras, endereço,
 * yn1/yn2, pred_scale) é o que o PB guarda. */
static void input_samples(u16 *pb, u32 count) {
    Accel a = {.pb = pb,
               .start = hilo(pb, AX_PB_ADDR_LOOP_HI) & AX_ADDR_START_MASK,
               .end = hilo(pb, AX_PB_ADDR_END_HI) & AX_ADDR_START_MASK,
               .cur = hilo(pb, AX_PB_ADDR_CUR_HI) & AX_ADDR_CUR_MASK,
               .format = pb[AX_PB_ADDR_FORMAT],
               .gain = (s16)pb[AX_PB_ADPCM_GAIN],
               .yn1 = (s16)pb[AX_PB_ADPCM_YN1],
               .yn2 = (s16)pb[AX_PB_ADPCM_YN2],
               .pred_scale = (u16)(pb[AX_PB_ADPCM_PS] & 0x7FU)};
    const u32 type = pb[AX_PB_SRC_TYPE];
    s16 out_last[4];
    if (type == AX_SRC_LINEAR || type == AX_SRC_POLYPHASE) {
        s16 temp[4];
        u32 idx = 0;
        for (u32 i = 0; i < 4U; i++) {
            temp[idx++ & 3U] = (s16)pb[AX_PB_SRC_LAST + i];
        }
        u32 pos = pb[AX_PB_SRC_FRAC];
        const u32 ratio = hilo(pb, AX_PB_SRC_RATIO_HI);
        for (u32 i = 0; i < count; i++) {
            pos += ratio;
            while (pos >= 0x10000U) {
                temp[idx++ & 3U] = read_sample(&a);
                pos -= 0x10000U;
            }
            /* A interpolação do Dolphin avança idx de 4 por amostra (lê 2 e pula 2, ou lê 1 e pula 3): nada muda
             * no anel de 4 posições. */
        }
        for (u32 i = 4; i > 0U; i--) {
            out_last[i - 1U] = temp[--idx & 3U];
        }
        pb[AX_PB_SRC_FRAC] = (u16)pos;
    } else {
        s16 tail[4] = {0};
        for (u32 i = 0; i < count; i++) {
            tail[i & 3U] = read_sample(&a); /* SRCTYPE_NEAREST: uma amostra por saída */
        }
        for (u32 i = 0; i < 4U; i++) {
            out_last[i] = tail[(count + i) & 3U];
        }
    }
    for (u32 i = 0; i < 4U; i++) {
        pb[AX_PB_SRC_LAST + i] = (u16)out_last[i];
    }
    pb[AX_PB_ADDR_CUR_HI] = (u16)(a.cur >> 16);
    pb[AX_PB_ADDR_CUR_HI + 1U] = (u16)a.cur;
    pb[AX_PB_ADPCM_YN1] = (u16)a.yn1;
    pb[AX_PB_ADPCM_YN2] = (u16)a.yn2;
    pb[AX_PB_ADPCM_PS] = a.pred_scale;
}

/* ApplyUpdatesForMs: as atualizações do milissegundo `ms` (pares offset/valor) vêm depois das dos anteriores. */
static void apply_updates(u16 *pb, u32 ms, const u16 *updates) {
    u32 start = 0;
    for (u32 i = 0; i < ms; i++) {
        start += pb[AX_PB_UPDATES + i];
    }
    const u32 count = pb[AX_PB_UPDATES + ms];
    if (start >= AX_UPDATES_MAX || count > AX_UPDATES_MAX - start) {
        return;
    }
    for (size_t i = start; i < (size_t)start + count; i++) {
        const u16 off = updates[i * 2U];
        if (off < AX_PB_WORDS) {
            pb[off] = updates[(i * 2U) + 1U];
        }
    }
}

static void process_pb(u32 pb_addr) {
    u16 pb[AX_PB_WORDS];
    u16 updates[AX_UPDATES_MAX * 2U];
    const GuestAddr base = MEM_CACHED_BASE | pb_addr;
    for (u32 i = 0; i < AX_PB_WORDS; i++) {
        pb[i] = mem_read16(NULL, base + (i * 2U));
    }
    const GuestAddr up = MEM_CACHED_BASE | (hilo(pb, AX_PB_UPDATES + 5U) & 0x03FFFFFFU);
    for (u32 i = 0; i < AX_UPDATES_MAX * 2U; i++) {
        updates[i] = mem_read16(NULL, up + (i * 2U));
    }
    for (u32 ms = 0; ms < AX_MS_PER_FRAME; ms++) {
        apply_updates(pb, ms, updates);
        if (pb[AX_PB_RUNNING] != 1U) {
            continue;
        }
        s_ax.stats.voice_frames++;
        input_samples(pb, AX_SAMPLES_PER_MS);
        /* Envelope de volume: cur_volume += delta por amostra (com sinal no GameCube). */
        pb[AX_PB_VOL_ENV] = (u16)(pb[AX_PB_VOL_ENV] + (u16)(pb[AX_PB_VOL_ENV + 1U] * AX_SAMPLES_PER_MS));
    }
    for (u32 i = 0; i < AX_PB_WORDS; i++) {
        mem_write16(NULL, base + (i * 2U), pb[i]);
    }
}

void ax_voice_process_list(u32 pb_addr) {
    s_ax.stats.frames++;
    for (u32 n = 0; pb_addr != 0U; n++) {
        if (n == AX_PB_LIST_MAX) {
            am_fatal("AX: lista de PBs com mais de %u vozes (laço?) a partir de 0x%08X", AX_PB_LIST_MAX, pb_addr);
        }
        process_pb(pb_addr & 0x03FFFFFFU);
        pb_addr = ((u32)mem_read16(NULL, MEM_CACHED_BASE | (pb_addr & 0x03FFFFFFU)) << 16) |
                  mem_read16(NULL, (MEM_CACHED_BASE | (pb_addr & 0x03FFFFFFU)) + 2U);
    }
}
