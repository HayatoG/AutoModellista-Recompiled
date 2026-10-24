/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Interface do DSP (0xCC005000): mailboxes CPU<->DSP, registrador de controle (CSR) e DMA de ARAM. YAGCD 5.6; Dolphin
 * Core/HW/DSP.cpp. */
#ifndef AM_DSP_H
#define AM_DSP_H

#include "am/types.h"

#define DSP_BASE 0x0C005000U

/* Registradores (deslocamento no bloco; Dolphin DSP.cpp). */
#define DSP_REG_MAIL_TO_DSP_HI    0x00U
#define DSP_REG_MAIL_TO_DSP_LO    0x02U
#define DSP_REG_MAIL_FROM_DSP_HI  0x04U
#define DSP_REG_MAIL_FROM_DSP_LO  0x06U
#define DSP_REG_CONTROL           0x0AU
#define DSP_REG_AR_INFO           0x12U
#define DSP_REG_AR_MODE           0x16U
#define DSP_REG_AR_REFRESH        0x1AU
#define DSP_REG_AR_MMADDR_H       0x20U
#define DSP_REG_AR_MMADDR_L       0x22U
#define DSP_REG_AR_ARADDR_H       0x24U
#define DSP_REG_AR_ARADDR_L       0x26U
#define DSP_REG_AR_CNT_H          0x28U
#define DSP_REG_AR_CNT_L          0x2AU
#define DSP_REG_AUDIO_START_HI    0x30U
#define DSP_REG_AUDIO_START_LO    0x32U
#define DSP_REG_AUDIO_CONTROL     0x36U
#define DSP_REG_AUDIO_BLOCKS_LEFT 0x3AU

/* Bits de DSP_CONTROL (Dolphin DSP.h: UDSPControl). */
#define DSP_CSR_RESET     0x0001U
#define DSP_CSR_HALT      0x0004U
#define DSP_CSR_AID       0x0008U
#define DSP_CSR_ARAM      0x0020U
#define DSP_CSR_DSP       0x0080U
#define DSP_CSR_DMA_STATE 0x0200U
#define DSP_CSR_INIT_CODE 0x0400U
#define DSP_CSR_INIT      0x0800U

void dsp_reset(void);
/* Avança eventos com prazo (fim de DMA de ARAM); o chassi chama a cada volta. */
void dsp_update(void);
/* Ciclo do próximo evento (fim de DMA, bloco da DMA de áudio, interrupção pedida pelo microcódigo). */
u64 dsp_next_event_cycle(void);
u64 dsp_audio_dma_interrupts(void);

/* Saída de áudio (M7): a cada bloco da DMA de áudio, 8 quadros estéreo (~32028 Hz: 108 MHz / 3372) em L, R
 * intercalados; com a DMA desligada, silêncio (Dolphin DSPManager::UpdateAudioDMA -> AudioCommon::SendAIBuffer). O
 * ritmo é o do tempo emulado: o mesmo áudio em qualquer velocidade de execução. NULL desliga. Sobrevive a
 * dsp_reset. */
#define DSP_AUDIO_BLOCK_FRAMES 8U
#define DSP_AUDIO_RATE_HZ      32028U /* 108000000 / 3372, inteiro como no WAV do Dolphin (WaveFileWriter::Start) */
typedef void (*DspAudioSink)(const s16 *lr, u32 frames, void *user);
void dsp_set_audio_sink(DspAudioSink sink, void *user);
u64 dsp_read(u32 offset, u8 size);
void dsp_write(u32 offset, u64 value, u8 size);

#endif /* AM_DSP_H */
