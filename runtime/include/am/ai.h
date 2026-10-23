/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Audio Interface (0xCC006C00): controle (AICR), volume do streaming, contador de amostras, timing de interrupção.
 * YAGCD 5.10; Dolphin Core/HW/AudioInterface.{h,cpp} (tag 2609a). Streaming do disco (PSTAT) é am_fatal até o M5. */
#ifndef AM_AI_H
#define AM_AI_H

#include "am/types.h"

#define AI_BASE 0x0C006C00U

#define AI_REG_CONTROL 0x00U
#define AI_REG_VOLUME  0x04U
#define AI_REG_SCNT    0x08U
#define AI_REG_IT      0x0CU

/* Bits de AICR (Dolphin AudioInterface.h). */
#define AI_CR_PSTAT    0x01U
#define AI_CR_AISFR    0x02U
#define AI_CR_AIINTMSK 0x04U
#define AI_CR_AIINT    0x08U
#define AI_CR_AIINTVLD 0x10U
#define AI_CR_SCRESET  0x20U
#define AI_CR_AIDFR    0x40U

void ai_reset(void);
u64 ai_read(u32 offset, u8 size);
void ai_write(u32 offset, u64 value, u8 size);

#endif /* AM_AI_H */
