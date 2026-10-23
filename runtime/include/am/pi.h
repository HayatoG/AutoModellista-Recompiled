/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Processor Interface (0xCC003000): causa e máscara das interrupções externas, código de reset, revisão do Flipper.
 * Fonte: YAGCD 5.4 e Dolphin Core/HW/ProcessorInterface.{h,cpp} (tag 2609a). */
#ifndef AM_PI_H
#define AM_PI_H

#include "am/types.h"

#define PI_BASE 0x0C003000U

/* Bits de causa/máscara (Dolphin ProcessorInterface.h: InterruptCause). */
#define PI_INT_PI         0x00001U
#define PI_INT_RSW        0x00002U
#define PI_INT_DI         0x00004U
#define PI_INT_SI         0x00008U
#define PI_INT_EXI        0x00010U
#define PI_INT_AI         0x00020U
#define PI_INT_DSP        0x00040U
#define PI_INT_MEM        0x00080U
#define PI_INT_VI         0x00100U
#define PI_INT_PE_TOKEN   0x00200U
#define PI_INT_PE_FINISH  0x00400U
#define PI_INT_CP         0x00800U
#define PI_INT_DEBUG      0x01000U
#define PI_INT_HSP        0x02000U
#define PI_INT_RST_BUTTON 0x10000U /* estado do botão (1 = solto), não mascarável */

/* Registradores (deslocamento no bloco). */
#define PI_REG_INTSR       0x00U
#define PI_REG_INTMR       0x04U
#define PI_REG_FIFO_BASE   0x0CU
#define PI_REG_FIFO_END    0x10U
#define PI_REG_FIFO_WPTR   0x14U
#define PI_REG_ERROR_CAUSE 0x1CU
#define PI_REG_ERROR_ADDR  0x20U
#define PI_REG_RESET_CODE  0x24U
#define PI_REG_FLIPPER_REV 0x2CU

/* Dolphin ProcessorInterface.cpp: FLIPPER_REV_C (console de varejo). */
#define PI_FLIPPER_REV_C 0x246500B1U

void pi_reset(void);
u64 pi_read(u32 offset, u8 size);
void pi_write(u32 offset, u64 value, u8 size);

/* Dispositivos sobem/descem a linha de interrupção. */
void pi_set_interrupt(u32 cause, bool set);
u32 pi_cause(void);
u32 pi_mask(void);
/* FIFO da CPU (PI_FIFO_BASE/END/WPTR), endereços físicos alinhados a 32. O gather pipe grava em wptr e avança:
 * depois do bloco em END volta a BASE (Dolphin GPFifo.cpp: UpdateGatherPipe). */
u32 pi_fifo_write_pointer(void);
void pi_fifo_advance(void);

/* true se alguma causa mascarável está ativa e liberada pela máscara: a CPU recebe a exceção externa (0x500). */
bool pi_external_pending(void);

#endif /* AM_PI_H */
