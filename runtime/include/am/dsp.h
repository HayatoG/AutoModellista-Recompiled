/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Interface do DSP (0xCC005000): mailboxes CPU<->DSP, registrador de controle (CSR) e DMA de ARAM. YAGCD 5.6; Dolphin
 * Core/HW/DSP.cpp. */
#ifndef AM_DSP_H
#define AM_DSP_H

#include "am/types.h"

#define DSP_BASE 0x0C005000U

/* Registradores (deslocamento no bloco; Dolphin DSP.cpp). */
#define DSP_REG_MAIL_TO_DSP_HI   0x00U
#define DSP_REG_MAIL_TO_DSP_LO   0x02U
#define DSP_REG_MAIL_FROM_DSP_HI 0x04U
#define DSP_REG_MAIL_FROM_DSP_LO 0x06U
#define DSP_REG_CONTROL          0x0AU
#define DSP_REG_AR_INFO          0x12U
#define DSP_REG_AR_MODE          0x16U
#define DSP_REG_AR_REFRESH       0x1AU
#define DSP_REG_AR_MMADDR_H      0x20U
#define DSP_REG_AR_MMADDR_L      0x22U
#define DSP_REG_AR_ARADDR_H      0x24U
#define DSP_REG_AR_ARADDR_L      0x26U
#define DSP_REG_AR_CNT_H         0x28U
#define DSP_REG_AR_CNT_L         0x2AU

/* Bits de DSP_CONTROL (Dolphin DSP.h: UDSPControl). */
#define DSP_CSR_RESET     0x0001U
#define DSP_CSR_HALT      0x0004U
#define DSP_CSR_AID       0x0008U
#define DSP_CSR_ARAM      0x0020U
#define DSP_CSR_DSP       0x0080U
#define DSP_CSR_DMA_STATE 0x0200U
#define DSP_CSR_INIT_CODE 0x0400U
#define DSP_CSR_INIT      0x0800U

/* Mails que o HLE do Dolphin põe no mailbox DSP->CPU (UCodes/ROM.cpp e UCodes/INIT.cpp). */
#define DSP_MAIL_ROM_READY  0x8071FEEDU
#define DSP_MAIL_INIT_READY 0x80544348U

void dsp_reset(void);
/* Avança eventos com prazo (fim de DMA de ARAM); o chassi chama a cada volta. */
void dsp_update(void);
u64 dsp_read(u32 offset, u8 size);
void dsp_write(u32 offset, u64 value, u8 size);

#endif /* AM_DSP_H */
