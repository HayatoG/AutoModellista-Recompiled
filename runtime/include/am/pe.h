/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Pixel Engine (0xCC001000): configuração de Z/alpha, interrupções de token e de fim de desenho, contadores.
 * Sem GPU no M5: os registradores são guardados e as interrupções vêm das escritas de BP que o leitor do fluxo
 * (gx_fifo) encontra. YAGCD 5.13; Dolphin VideoCommon/PixelEngine.{h,cpp} (registradores e UPECtrlReg). */
#ifndef AM_PE_H
#define AM_PE_H

#include "am/types.h"

#define PE_BASE 0x0C001000U

#define PE_REG_CTRL  0x0AU /* bit 0 token habilitado, 1 fim habilitado, 2 token (1 apaga), 3 fim (1 apaga) */
#define PE_REG_TOKEN 0x0EU
#define PE_REG_LAST  0x2EU /* contadores de desempenho vão até 0x2E */

#define PE_CTRL_TOKEN_ENABLE  0x1U
#define PE_CTRL_FINISH_ENABLE 0x2U
#define PE_CTRL_TOKEN         0x4U
#define PE_CTRL_FINISH        0x8U

void pe_reset(void);
u64 pe_read(u32 offset, u8 size);
void pe_write(u32 offset, u64 value, u8 size);

/* Eventos do fluxo de comandos (BP 0x45 e 0x47/0x48). */
void pe_signal_finish(void);
void pe_signal_token(u16 token, bool interrupt);

#endif /* AM_PE_H */
