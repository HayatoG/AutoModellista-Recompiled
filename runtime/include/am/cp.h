/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Command Processor (0xCC000000): registradores da FIFO da GPU (base, fim, marcas d'água, ponteiros), controle e
 * status. Sem GPU no M5: o que entra na FIFO é consumido na hora pelo leitor gx_fifo (a GPU nunca fica para trás),
 * então a distância de leitura/escrita é sempre 0 e o CP está sempre ocioso; nunca há estouro de marca d'água.
 * YAGCD 5.11; Dolphin VideoCommon/CommandProcessor.{h,cpp} (nomes de registradores e bits). */
#ifndef AM_CP_H
#define AM_CP_H

#include "am/types.h"

#define CP_BASE 0x0C000000U

#define CP_REG_STATUS    0x00U
#define CP_REG_CTRL      0x02U
#define CP_REG_CLEAR     0x04U
#define CP_REG_TOKEN     0x0EU
#define CP_REG_FIFO_BASE 0x20U /* pares LO (endereço menor) / HI */
#define CP_REG_FIFO_END  0x24U
#define CP_REG_FIFO_HIWM 0x28U
#define CP_REG_FIFO_LOWM 0x2CU
#define CP_REG_FIFO_DIST 0x30U
#define CP_REG_FIFO_WPTR 0x34U
#define CP_REG_FIFO_RPTR 0x38U
#define CP_REG_FIFO_BP   0x3CU
#define CP_REG_LAST      0x7EU /* contadores de desempenho do XF/CP até 0x7E */

/* UCPStatusReg / UCPCtrlReg. */
#define CP_SR_OVERFLOW     0x01U
#define CP_SR_UNDERFLOW    0x02U
#define CP_SR_READ_IDLE    0x04U
#define CP_SR_COMMAND_IDLE 0x08U
#define CP_CR_READ_ENABLE  0x01U
#define CP_CR_BP_ENABLE    0x02U
#define CP_CR_OVF_INT      0x04U
#define CP_CR_UNF_INT      0x08U
#define CP_CR_LINK         0x10U

void cp_reset(void);
u64 cp_read(u32 offset, u8 size);
void cp_write(u32 offset, u64 value, u8 size);

/* GatherPipeBursted: 32 bytes acabaram de ser gravados na FIFO da CPU (PI) em `phys`. */
void cp_gather_pipe_burst(u32 phys, const u8 *data);

u64 cp_bursts(void);

#endif /* AM_CP_H */
