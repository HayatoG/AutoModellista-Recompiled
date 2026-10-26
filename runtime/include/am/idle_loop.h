/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Laços de espera ("idle loops"): o jogo gira lendo uma variável global até uma interrupção mudá-la, por exemplo
 * `lwz r0, d(r13); cmplwi r0, 0; beq .-8` (0x800C8070, que é mais da metade dos ciclos da corrida). Enquanto gira,
 * nada muda: a memória só muda por store do próprio jogo ou por evento de dispositivo, e o chassi só processa evento
 * entre fatias. Então todas as fatias até o próximo evento são iguais, e o chassi pode avançar o tempo direto até lá
 * (como o "idle skipping" do Dolphin, Core/PowerPC/PPCAnalyst.cpp: IsBusyWaitLoop), sem mudar a linha do tempo.
 *
 * Só entra o formato load-compare-branch de 3 instruções, com o load numa variável da MEM1 (sem efeito colateral). */
#ifndef AM_IDLE_LOOP_H
#define AM_IDLE_LOOP_H

#include "am/hook.h"
#include "am/types.h"

#define IDLE_LOOP_MAX 64U

/* Procura os laços no código (as mesmas faixas de texto do DOL que o hook_check_reachable recebe). Devolve quantos
 * achou (até IDLE_LOOP_MAX). */
u32 idle_loop_scan(const HookCodeRange *ranges, u32 count);
void idle_loop_clear(void);

/* true se cpu->pc é o início de um laço de espera que vai girar a próxima fatia inteira com o estado atual: a
 * variável está na MEM1, o valor faz o desvio voltar e é o mesmo que o registrador já tem (a fatia anterior leu). */
bool idle_loop_spins(const CPUState *cpu);

#endif /* AM_IDLE_LOOP_H */
