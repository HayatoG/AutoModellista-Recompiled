/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Write-gather pipe (0xCC008000, WPAR): a CPU escreve comandos do GX em pedaços de 1, 2, 4 ou 8 bytes; a cada 32
 * bytes o bloco vai para a FIFO da CPU na memória (PI_FIFO_WPTR, volta à base depois do bloco no fim) e o CP é
 * avisado. Dolphin Core/HW/GPFifo.cpp (UpdateGatherPipe) e ProcessorInterface (ponteiros da FIFO). */
#ifndef AM_GATHER_PIPE_H
#define AM_GATHER_PIPE_H

#include "am/types.h"

#define GATHER_PIPE_BASE 0x0C008000U
#define GATHER_PIPE_SIZE 32U

void gather_pipe_reset(void);
void gather_pipe_write(u32 offset, u64 value, u8 size);
u64 gather_pipe_read(u32 offset, u8 size);

/* Bytes ainda no buffer (menos de 32) e total escrito, para log e testes. */
u32 gather_pipe_pending(void);
u64 gather_pipe_total_bytes(void);

#endif /* AM_GATHER_PIPE_H */
