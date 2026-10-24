/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Vozes do AX (M5: andam; M7: tocam). O que o microcódigo faz com cada parameter block (PB) na MRAM a cada quadro de
 * 5 ms: aplica as atualizações agendadas por milissegundo, lê 32 amostras por milissegundo pelo acelerador do DSP
 * (ADPCM, PCM8, PCM16 na ARAM, laço e fim de amostra) e pelo conversor de taxa, aplica o envelope de volume e o
 * filtro passa-baixa, e soma a voz nos 9 buffers de mixagem (principal, AUX A e AUX B; esquerda, direita, surround)
 * com o volume e a rampa de cada um, guardando a última amostra no "dpop". O PB volta para a MRAM. A CRI (ADX e
 * Sofdec) lê de volta o endereço atual de cada voz e o campo `running` para saber quanto foi tocado.
 *
 * Fonte: Dolphin 2609a DSPHLE/UCodes/AX.cpp (ProcessPBList, ConvertMixerControl), AXVoice.h (ProcessVoice,
 * GetInputSamples, ResampleAudio, MixAdd, LowPassFilter, ApplyUpdatesForMs), AXStructs.h (AXPB) e
 * Core/DSP/DSPAccelerator.cpp (ReadSample). O conversor polifásico do Dolphin só existe com dsp_coef.bin, que o
 * oráculo não tem: como lá, o tipo 0 (polifásico) usa o linear. */
#ifndef AM_AX_VOICE_H
#define AM_AX_VOICE_H

#include "am/types.h"

/* AXPB do GameCube com filtro passa-baixa (todos os AX conhecidos menos 0x4E8A8B21), em palavras de 16 bits. */
#define AX_PB_WORDS         122U
#define AX_PB_NEXT_HI       0U
#define AX_PB_SRC_TYPE      4U
#define AX_PB_MIXER_CONTROL 6U
#define AX_PB_RUNNING       7U
#define AX_PB_IS_STREAM     8U
#define AX_PB_MIXER         9U  /* 9 x (volume, delta): L R, AUXA L R, AUXB L R, AUXB S, S, AUXA S */
#define AX_PB_ITD_ON        27U /* atraso inicial (3D): o Dolphin também não implementa */
#define AX_PB_UPDATES       34U /* num_updates[5], data_hi, data_lo */
#define AX_PB_DPOP          41U /* 9 x s16: L, AUXA L, AUXB L, R, AUXA R, AUXB R, S, AUXA S, AUXB S */
#define AX_PB_VOL_ENV       50U
#define AX_PB_ADDR_LOOPING  55U
#define AX_PB_ADDR_FORMAT   56U
#define AX_PB_ADDR_LOOP_HI  57U
#define AX_PB_ADDR_END_HI   59U
#define AX_PB_ADDR_CUR_HI   61U
#define AX_PB_ADPCM_COEFS   63U
#define AX_PB_ADPCM_GAIN    79U
#define AX_PB_ADPCM_PS      80U
#define AX_PB_ADPCM_YN1     81U
#define AX_PB_ADPCM_YN2     82U
#define AX_PB_SRC_RATIO_HI  83U
#define AX_PB_SRC_FRAC      85U
#define AX_PB_SRC_LAST      86U
#define AX_PB_LOOP_PS       90U
#define AX_PB_LOOP_YN1      91U
#define AX_PB_LOOP_YN2      92U
#define AX_PB_LPF_ON        93U /* on, yn1, a0, b0 */
#define AX_PB_LOOP_COUNTER  97U

#define AX_SRC_POLYPHASE 0U
#define AX_SRC_LINEAR    1U
#define AX_SRC_NEAREST   2U

/* Um quadro do AX: 5 ms a 32 amostras por ms. */
#define AX_SAMPLES_PER_MS    32U
#define AX_MS_PER_FRAME      5U
#define AX_SAMPLES_PER_FRAME (AX_SAMPLES_PER_MS * AX_MS_PER_FRAME)

typedef enum AxChannel {
    AX_MAIN_L,
    AX_MAIN_R,
    AX_MAIN_S,
    AX_AUXA_L,
    AX_AUXA_R,
    AX_AUXA_S,
    AX_AUXB_L,
    AX_AUXB_R,
    AX_AUXB_S,
    AX_CHANNELS,
} AxChannel;

/* Buffers de mixagem do microcódigo (m_samples_* do AXUCode): inteiros de 32 bits, 5 ms. */
typedef struct AxMixBuffers {
    s32 ch[AX_CHANNELS][AX_SAMPLES_PER_FRAME];
} AxMixBuffers;

typedef struct AxVoiceStats {
    u64 frames;       /* listas de PB processadas */
    u64 voice_frames; /* PB x milissegundo com running = 1 */
    u64 samples;      /* amostras lidas da ARAM */
    u64 ended;        /* vozes que chegaram ao fim sem laço */
    u64 loops;
    u64 max_voices; /* maior lista de PBs num quadro */
} AxVoiceStats;

void ax_voice_reset(void);
/* CMD_PROCESS: percorre a lista de PBs a partir de `pb_addr` (físico) e processa um quadro de 5 ms, somando cada voz
 * em `mix`. */
void ax_voice_process_list(u32 pb_addr, AxMixBuffers *mix);
const AxVoiceStats *ax_voice_stats(void);

/* Partes expostas para teste (dados montados à mão). */

/* Fonte de amostras de entrada do conversor de taxa (o acelerador, nos PBs). */
typedef s16 (*AxSampleSource)(void *user);
/* ResampleAudio: `count` saídas a partir da posição fracionária `pos` (16.16) e da razão `ratio` (16.16); `last` são
 * as 4 últimas amostras de entrada (entram e saem atualizadas). Devolve a nova posição. */
u32 ax_resample(AxSampleSource source, void *user, s16 *out, u32 count, s16 last[4], u32 pos, u32 ratio, u32 src_type);
/* MixAdd: soma `in` x volume (1.15) em `out`, com rampa (volume += delta por amostra) se `ramp`; *dpop recebe a
 * última amostra somada. */
void ax_mix_add(s32 *out, const s16 *in, u32 count, u16 *volume, u16 delta, s16 *dpop, bool ramp);

#endif /* AM_AX_VOICE_H */
