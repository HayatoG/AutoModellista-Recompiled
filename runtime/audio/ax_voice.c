/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tradução para C de partes do Dolphin 2609a (GPL-2.0-or-later): Source/Core/Core/HW/DSPHLE/UCodes/AXVoice.h e AX.cpp
 * (ProcessPBList) e Source/Core/Core/DSP/DSPAccelerator.cpp (ReadSample, GetCurrentSample). Copyright Dolphin Emulator
 * Project. */
#include "am/ax_voice.h"

#include <string.h>

#include "am/log.h"
#include "am/mem.h"

#define AX_UPDATES_MAX     32U
#define AX_PB_LIST_MAX     256U
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

/* Canal de mixagem: onde ficam volume e dpop no PB e os bits de mixer_control que o ligam (AXUCode::
 * ConvertMixerControl, ramo "newer GameCube ucodes"). */
typedef struct MixSlot {
    AxChannel channel;
    u8 mixer;
    u8 dpop;
    u16 on;
    u16 ramp;
} MixSlot;

static const MixSlot s_slots[AX_CHANNELS] = {
    {AX_MAIN_L, 0, 0, 0x0001U, 0x0008U}, {AX_MAIN_R, 1, 3, 0x0002U, 0x0008U}, {AX_MAIN_S, 7, 6, 0x0004U, 0x0008U},
    {AX_AUXA_L, 2, 1, 0x0010U, 0x0040U}, {AX_AUXA_R, 3, 4, 0x0020U, 0x0040U}, {AX_AUXA_S, 8, 7, 0x0080U, 0x0100U},
    {AX_AUXB_L, 4, 2, 0x0200U, 0x0800U}, {AX_AUXB_R, 5, 5, 0x0400U, 0x0800U}, {AX_AUXB_S, 6, 8, 0x1000U, 0x2000U},
};

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

/* AXVoice.h ClampS16. */
static s16 clamp16(s64 v) {
    return (s16)(v < -0x8000 ? -0x8000 : (v > 0x7FFF ? 0x7FFF : v));
}

static u8 aram8(u32 address) {
    return mem_aram_read8(address & (MEM_ARAM_SIZE - 1U)); /* DSPManager::ReadARAM: ptr[address & mask] */
}

/* Accelerator::GetCurrentSample (u16: o PCM8 entra positivo, como no Dolphin). */
static u16 current_sample(const Accel *a) {
    switch (a->format & 3U) {
    case 0U: {
        const u8 b = aram8(a->cur >> 1);
        return (u16)((a->cur & 1U) != 0U ? (b & 0xFU) : (b >> 4));
    }
    case 1U:
        return aram8(a->cur);
    case 2U:
        return (u16)(((u16)aram8(a->cur * 2U) << 8) | aram8((a->cur * 2U) + 1U));
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

static s16 adpcm_sample(Accel *a, s32 raw, s32 c1, s32 c2, u32 *step) {
    raw &= 0xF;
    if (raw >= 8) {
        raw -= 16;
    }
    const s32 v = ((1 << (a->pred_scale & 0xFU)) * raw) + ((0x400 + (c1 * a->yn1) + (c2 * a->yn2)) >> 11);
    const s16 val = clamp16(v); /* MathUtil::SaturatingCast<s16> */
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
        *step += 2U;
    }
    return val;
}

/* Accelerator::ReadSample (só as decodificações que leem a ARAM: ADPCM e PCM). */
static s16 read_sample(void *user) {
    Accel *a = user;
    if (a->stopped) {
        return 0;
    }
    const u32 decode = (a->format >> 2) & 3U;
    if (decode == 1U || decode == 3U) {
        am_fatal("AX: formato 0x%04X lê da entrada MMIO do acelerador, não suportado", a->format);
    }
    const s32 raw = (s16)current_sample(a);
    const u32 coef_idx = (a->pred_scale >> 4) & 7U;
    const s32 c1 = (s16)a->pb[AX_PB_ADPCM_COEFS + (coef_idx * 2U)];
    const s32 c2 = (s16)a->pb[AX_PB_ADPCM_COEFS + (coef_idx * 2U) + 1U];
    s16 val = 0;
    u32 step = 2;
    s_ax.stats.samples++;
    if (decode == 0U) {
        val = adpcm_sample(a, raw, c1, c2, &step);
    } else {
        static const u8 s_gain_shift[4] = {11, 0, 16, 0};
        const u32 sh = s_gain_shift[(a->format >> 4) & 3U];
        const s32 v = ((a->gain * raw) >> sh) + ((c1 * a->yn1) >> sh) + ((c2 * a->yn2) >> sh);
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

/* AXVoice.h ResampleAudio, ramos linear (também para o polifásico sem coeficientes) e "nearest". A interpolação usa as
 * duas amostras mais antigas do anel de 4: o atraso de 3 amostras é o do Dolphin (e do microcódigo). */
u32 ax_resample(AxSampleSource source, void *user, s16 *out, u32 count, s16 last[4], u32 pos, u32 ratio, u32 src_type) {
    if (src_type != AX_SRC_LINEAR && src_type != AX_SRC_POLYPHASE) {
        for (u32 i = 0; i < count; i++) {
            out[i] = source(user);
        }
        memcpy(last, out + count - 4U, 4U * sizeof(s16));
        return pos;
    }
    s16 temp[4];
    u32 idx = 0;
    for (u32 i = 0; i < 4U; i++) {
        temp[idx++ & 3U] = last[i];
    }
    for (u32 i = 0; i < count; i++) {
        pos += ratio;
        while (pos >= 0x10000U) {
            temp[idx++ & 3U] = source(user);
            pos -= 0x10000U;
        }
        const u16 frac = (u16)pos;
        if (frac != 0U) {
            const u16 inv = (u16)(0x10000U - frac);
            const s32 s0 = temp[idx++ & 3U];
            const s32 s1 = temp[idx++ & 3U];
            out[i] = (s16)(((s0 * inv) + (s1 * frac)) >> 16);
            idx += 2U;
        } else {
            out[i] = temp[idx++ & 3U];
            idx += 3U;
        }
    }
    for (u32 i = 4; i > 0U; i--) {
        last[i - 1U] = temp[--idx & 3U];
    }
    return pos;
}

/* GetInputSamples: acelerador montado a partir do PB (AcceleratorSetup), conversor de taxa, estado de volta no PB. */
static void input_samples(u16 *pb, s16 *samples) {
    Accel a = {.pb = pb,
               .start = hilo(pb, AX_PB_ADDR_LOOP_HI) & AX_ADDR_START_MASK,
               .end = hilo(pb, AX_PB_ADDR_END_HI) & AX_ADDR_START_MASK,
               .cur = hilo(pb, AX_PB_ADDR_CUR_HI) & AX_ADDR_CUR_MASK,
               .format = pb[AX_PB_ADDR_FORMAT],
               .gain = (s16)pb[AX_PB_ADPCM_GAIN],
               .yn1 = (s16)pb[AX_PB_ADPCM_YN1],
               .yn2 = (s16)pb[AX_PB_ADPCM_YN2],
               .pred_scale = (u16)(pb[AX_PB_ADPCM_PS] & 0x7FU)};
    s16 last[4];
    for (u32 i = 0; i < 4U; i++) {
        last[i] = (s16)pb[AX_PB_SRC_LAST + i];
    }
    const u32 pos = ax_resample(read_sample, &a, samples, AX_SAMPLES_PER_MS, last, pb[AX_PB_SRC_FRAC],
                                hilo(pb, AX_PB_SRC_RATIO_HI), pb[AX_PB_SRC_TYPE]);
    pb[AX_PB_SRC_FRAC] = (u16)pos;
    for (u32 i = 0; i < 4U; i++) {
        pb[AX_PB_SRC_LAST + i] = (u16)last[i];
    }
    pb[AX_PB_ADDR_CUR_HI] = (u16)(a.cur >> 16);
    pb[AX_PB_ADDR_CUR_HI + 1U] = (u16)a.cur;
    pb[AX_PB_ADPCM_YN1] = (u16)a.yn1;
    pb[AX_PB_ADPCM_YN2] = (u16)a.yn2;
    pb[AX_PB_ADPCM_PS] = a.pred_scale;
}

void ax_mix_add(s32 *out, const s16 *in, u32 count, u16 *volume, u16 delta, s16 *dpop, bool ramp) {
    const u16 step = ramp ? delta : 0U;
    for (u32 i = 0; i < count; i++) {
        const s16 s = clamp16(((s64)in[i] * *volume) >> 15);
        out[i] = (s32)((u32)out[i] + (u32)(s32)s); /* o acumulador do Dolphin é int; sem UB no estouro */
        *volume = (u16)(*volume + step);
        *dpop = s;
    }
}

/* ProcessVoice: 1 ms de uma voz somado nos buffers a partir de `at`. */
static void process_voice(u16 *pb, AxMixBuffers *mix, u32 at) {
    s16 samples[AX_SAMPLES_PER_MS];
    input_samples(pb, samples);
    for (u32 i = 0; i < AX_SAMPLES_PER_MS; i++) {
        const s32 volume = (s16)pb[AX_PB_VOL_ENV]; /* com sinal no GameCube */
        samples[i] = clamp16(((s32)samples[i] * volume) >> 15);
        pb[AX_PB_VOL_ENV] = (u16)(pb[AX_PB_VOL_ENV] + pb[AX_PB_VOL_ENV + 1U]);
    }
    if (pb[AX_PB_LPF_ON] != 0U) {
        const s32 a0 = pb[AX_PB_LPF_ON + 2U];
        const s32 b0 = (s16)pb[AX_PB_LPF_ON + 3U];
        for (u32 i = 0; i < AX_SAMPLES_PER_MS; i++) {
            samples[i] = clamp16(((a0 * samples[i]) + (b0 * (s16)pb[AX_PB_LPF_ON + 1U])) >> 15);
            pb[AX_PB_LPF_ON + 1U] = (u16)samples[i];
        }
    }
    const u16 control = pb[AX_PB_MIXER_CONTROL];
    for (u32 c = 0; c < AX_CHANNELS; c++) {
        const MixSlot *m = &s_slots[c];
        if ((control & m->on) == 0U) {
            continue;
        }
        s16 dpop = (s16)pb[AX_PB_DPOP + m->dpop];
        u16 volume = pb[AX_PB_MIXER + (m->mixer * 2U)];
        ax_mix_add(&mix->ch[m->channel][at], samples, AX_SAMPLES_PER_MS, &volume,
                   pb[AX_PB_MIXER + (m->mixer * 2U) + 1U], &dpop, (control & m->ramp) != 0U);
        pb[AX_PB_MIXER + (m->mixer * 2U)] = volume;
        pb[AX_PB_DPOP + m->dpop] = (u16)dpop;
    }
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

static void process_pb(u32 pb_addr, AxMixBuffers *mix) {
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
        process_voice(pb, mix, ms * AX_SAMPLES_PER_MS);
    }
    for (u32 i = 0; i < AX_PB_WORDS; i++) {
        mem_write16(NULL, base + (i * 2U), pb[i]);
    }
}

void ax_voice_process_list(u32 pb_addr, AxMixBuffers *mix) {
    s_ax.stats.frames++;
    u64 n = 0;
    for (; pb_addr != 0U; n++) {
        if (n == AX_PB_LIST_MAX) {
            am_fatal("AX: lista de PBs com mais de %u vozes (laço?) a partir de 0x%08X", AX_PB_LIST_MAX, pb_addr);
        }
        process_pb(pb_addr & 0x03FFFFFFU, mix);
        pb_addr = ((u32)mem_read16(NULL, MEM_CACHED_BASE | (pb_addr & 0x03FFFFFFU)) << 16) |
                  mem_read16(NULL, (MEM_CACHED_BASE | (pb_addr & 0x03FFFFFFU)) + 2U);
    }
    if (n > s_ax.stats.max_voices) {
        s_ax.stats.max_voices = n;
    }
}
