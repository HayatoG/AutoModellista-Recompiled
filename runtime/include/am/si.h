/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Serial Interface (0xCC006400): portas de controle. YAGCD 5.8; Dolphin Core/HW/SI/SI.cpp. */
#ifndef AM_SI_H
#define AM_SI_H

#include "am/types.h"

#define SI_BASE 0x0C006400U

#define SI_CHANNEL_COUNT 4U
#define SI_BUFFER_SIZE   128U

#define SI_REG_POLL   0x30U
#define SI_REG_COMCSR 0x34U
#define SI_REG_STATUS 0x38U
#define SI_REG_EXICLK 0x3CU
#define SI_REG_BUFFER 0x80U

/* SICOMCSR (Dolphin SI.h: USIComCSR). */
#define SI_CSR_TSTART     0x00000001U
#define SI_CSR_CHANNEL    0x00000006U
#define SI_CSR_INLNGTH    0x00007F00U
#define SI_CSR_OUTLNGTH   0x007F0000U
#define SI_CSR_RDSTINTMSK 0x08000000U
#define SI_CSR_RDSTINT    0x10000000U
#define SI_CSR_COMERR     0x20000000U
#define SI_CSR_TCINTMSK   0x40000000U
#define SI_CSR_TCINT      0x80000000U

/* Resposta de tipo de um controle padrão (Dolphin SI_Device.h: SI_GC_CONTROLLER = 0x09000000, 3 bytes). */
#define SI_TYPE_GC_CONTROLLER 0x09000000U

/* Aparelhos do M4: controle padrão só na porta 1 (canal 0), como no oráculo. */
void si_reset(void);
u64 si_read(u32 offset, u8 size);
void si_write(u32 offset, u64 value, u8 size);

#endif /* AM_SI_H */
