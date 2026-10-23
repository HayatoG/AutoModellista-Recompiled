/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Vozes do AX sem mixagem (M5): o que o microcódigo faz com cada parameter block (PB) na MRAM a cada quadro de 5 ms,
 * menos o som. O jogo depende disso para andar: a CRI (ADX/Sofdec) e o próprio AX leem de volta o endereço atual de
 * cada voz e o campo `running` para saber quanto foi tocado e quando um som acabou (sem isso, o primeiro jingle dos
 * logotipos nunca termina e o jogo espera para sempre).
 *
 * Por PB e por milissegundo: aplica as atualizações agendadas (updates), lê 32 amostras de saída pelo conversor de
 * taxa (linear; o polifásico do Dolphin só existe com dsp_coef.bin, que o oráculo não tem) e pelo acelerador do DSP
 * (ADPCM, PCM8, PCM16 na ARAM, laço e fim de amostra), atualiza o envelope de volume e grava o PB de volta. A
 * mixagem (volumes com rampa, dpop, filtro passa-baixa, buffers de saída) fica para o M7: os campos de saída do PB
 * não mudam. Fonte: Dolphin DSPHLE/UCodes/AX.cpp (ProcessPBList), AXVoice.h (ProcessVoice, GetInputSamples,
 * ResampleAudio, ApplyUpdatesForMs), AXStructs.h (AXPB) e Core/DSP/DSPAccelerator.cpp (ReadSample). */
#ifndef AM_AX_VOICE_H
#define AM_AX_VOICE_H

#include "am/types.h"

/* AXPB do GameCube com filtro passa-baixa (todos os AX conhecidos menos 0x4E8A8B21), em palavras de 16 bits. */
#define AX_PB_WORDS        122U
#define AX_PB_NEXT_HI      0U
#define AX_PB_SRC_TYPE     4U
#define AX_PB_RUNNING      7U
#define AX_PB_IS_STREAM    8U
#define AX_PB_UPDATES      34U /* num_updates[5], data_hi, data_lo */
#define AX_PB_VOL_ENV      50U
#define AX_PB_ADDR_LOOPING 55U
#define AX_PB_ADDR_FORMAT  56U
#define AX_PB_ADDR_LOOP_HI 57U
#define AX_PB_ADDR_END_HI  59U
#define AX_PB_ADDR_CUR_HI  61U
#define AX_PB_ADPCM_COEFS  63U
#define AX_PB_ADPCM_GAIN   79U
#define AX_PB_ADPCM_PS     80U
#define AX_PB_ADPCM_YN1    81U
#define AX_PB_ADPCM_YN2    82U
#define AX_PB_SRC_RATIO_HI 83U
#define AX_PB_SRC_FRAC     85U
#define AX_PB_SRC_LAST     86U
#define AX_PB_LOOP_PS      90U
#define AX_PB_LOOP_YN1     91U
#define AX_PB_LOOP_YN2     92U
#define AX_PB_LOOP_COUNTER 97U

typedef struct AxVoiceStats {
    u64 frames;       /* listas de PB processadas */
    u64 voice_frames; /* PB x quadro com running = 1 */
    u64 samples;      /* amostras lidas da ARAM */
    u64 ended;        /* vozes que chegaram ao fim sem laço */
    u64 loops;
} AxVoiceStats;

void ax_voice_reset(void);
/* CMD_PROCESS: percorre a lista de PBs a partir de `pb_addr` (físico) e processa um quadro de 5 ms. */
void ax_voice_process_list(u32 pb_addr);
const AxVoiceStats *ax_voice_stats(void);

#endif /* AM_AX_VOICE_H */
