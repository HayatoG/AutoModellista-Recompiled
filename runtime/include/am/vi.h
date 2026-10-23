/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Video Interface (0xCC002000): registradores de timing, framebuffer (XFB), interrupções de display e posição do
 * feixe. Sem saída de vídeo (M6): o XFB só é registrado (vi_xfb_top) e cada campo é contado.
 *
 * O tempo é o da CPU emulada (timebase_cycles), não um timer do host: o feixe avança uma meia-linha a cada
 * GetTicksPerHalfLine ciclos, e a 27 MHz com HLW = 429 e 525 meias-linhas por campo isso dá 59,94 campos/s. É
 * determinístico e é o que o Dolphin faz (CoreTiming). Fonte: YAGCD 5.2; Dolphin Core/HW/VideoInterface.{h,cpp}
 * (Preset, RegisterMMIO, Update, GetTicksPerHalfLine), tag 2609a. */
#ifndef AM_VI_H
#define AM_VI_H

#include "am/types.h"

#define VI_BASE 0x0C002000U

/* Registradores de 16 bits (Dolphin VideoInterface.h). Os de 32 bits são pares HI (endereço menor) / LO. */
#define VI_REG_VTR     0x00U /* EQU (bits 0-3), ACV (4-13) */
#define VI_REG_DCR     0x02U /* ENB, RST, NIN, DLR, LE0, LE1, FMT */
#define VI_REG_HTR0_HI 0x04U
#define VI_REG_HTR0_LO 0x06U /* HLW (bits 0-9) */
#define VI_REG_VTO_HI  0x0CU /* PSB do campo ímpar */
#define VI_REG_VTO_LO  0x0EU /* PRB do campo ímpar */
#define VI_REG_VTE_HI  0x10U
#define VI_REG_VTE_LO  0x12U
#define VI_REG_TFBL_HI 0x1CU /* XFB do campo de cima: FBB 24 bits, XOFF, POFF (bit 28), CLRPOFF */
#define VI_REG_TFBL_LO 0x1EU
#define VI_REG_BFBL_HI 0x24U
#define VI_REG_BFBL_LO 0x26U
#define VI_REG_VCOUNT  0x2CU /* posição vertical do feixe (só leitura) */
#define VI_REG_HCOUNT  0x2EU
#define VI_REG_DI0_HI  0x30U /* DI0..DI3: HCT (LO), VCT (bits 16-26), máscara (28), status (31) */
#define VI_REG_DI3_LO  0x3EU
#define VI_REG_CLOCK   0x6CU
#define VI_REG_DTV     0x6EU
#define VI_REG_FBWIDTH 0x70U
#define VI_REG_LAST    0x76U /* fim do bloco que o Dolphin registra */
#define VI_DI_COUNT    4U
#define VI_DI_INT_HI   0x8000U /* IR_INT, bit 31 */
#define VI_DI_MASK_HI  0x1000U /* IR_MASK, bit 28 */
#define VI_DCR_RST     0x0002U

void vi_reset(bool ntsc);
/* Avança o feixe até o ciclo atual da CPU (o chassi chama a cada volta). */
void vi_update(void);
u64 vi_read(u32 offset, u8 size);
void vi_write(u32 offset, u64 value, u8 size);

/* Ciclo de CPU em que o próximo evento de meia-linha acontece: o chassi pode avançar o tempo até lá quando a CPU só
 * espera (ver chassis.c, ociosidade). */
u64 vi_next_event_cycle(void);

/* Para log e testes. */
u64 vi_field_count(void);
u32 vi_half_line(void);
u64 vi_ticks_per_half_line(void);
u32 vi_xfb_top(void);

#endif /* AM_VI_H */
