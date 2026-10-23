/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Trace de chamadas da runtime: uma linha de texto por entrada de função, no formato que o oráculo do Dolphin também
 * produz (scripts/am_trace_lib.py; docs/M4_BOOT.md §5):
 *
 *     # am-trace v1 source=runtime ...
 *     <pc> <lr> <ctr>           hex com 8 dígitos
 *
 * Todo bl/bcl/bctrl/blrl do C gerado devolve o pc ao chassi, então o chassi vê toda chamada. Um retorno ao chassi é
 * chamada quando a instrução em LR-4 é um desvio com link cujo alvo é o pc (mesmo critério do lado do Dolphin). */
#ifndef AM_TRACE_H
#define AM_TRACE_H

#include "am/types.h"

/* Critério de chamada, isolado para teste: `insn` é a palavra em `lr - 4`. bcctrl confere o alvo pelo CTR; bclrl
 * não pode ser conferido (o LR já foi sobrescrito) e conta como chamada, exceto quando pc == lr (é o retorno). */
bool trace_is_call(u32 insn, GuestAddr lr, GuestAddr pc, u32 ctr);

/* Teto padrão do arquivo de trace: passou disso é am_fatal (um laço sem fim não pode encher o disco). */
#define TRACE_DEFAULT_MAX_BYTES (256ULL * 1024ULL * 1024ULL)

/* Abre o arquivo e escreve o cabeçalho; `extra` (pode ser NULL) é acrescentado ao cabeçalho como "k=v k=v".
 * `max_bytes` = 0 usa TRACE_DEFAULT_MAX_BYTES. */
bool trace_open(const char *path, const char *extra, u64 max_bytes);
void trace_close(void);

/* Grava em `path` a tabela de trechos do C gerado ("início fim" em hex, um por linha): o comparador só consegue
 * comparar chamadas entre trechos diferentes (docs/M4_BOOT.md §5.2). */
bool trace_write_chunks(const char *path);
bool trace_is_open(void);

/* Chamado pelo chassi antes de cada despacho. Grava o evento se for chamada. Devolve true se gravou. */
bool trace_on_dispatch(const CPUState *cpu);
u64 trace_event_count(void);

#endif /* AM_TRACE_H */
