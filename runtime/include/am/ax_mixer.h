/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lista de comandos do microcódigo AX do GameCube e a mixagem que ela pede (M7): prepara os 9 buffers de 5 ms
 * (CMD_SETUP), processa as vozes (CMD_PROCESS, audio/ax_voice.c), troca os buffers AUX com a CPU para os efeitos do
 * jogo (CMD_MIX_AUXA/B, CMD_SEND_AUX_AND_MIX...), aplica o compressor e entrega o resultado em PCM de 16 bits na MRAM
 * (CMD_OUTPUT), de onde a DMA de áudio leva ao AI. Fonte: Dolphin 2609a DSPHLE/UCodes/AX.cpp (HandleCommandList e os
 * métodos que ele chama). */
#ifndef AM_AX_MIXER_H
#define AM_AX_MIXER_H

#include "am/ax_voice.h"
#include "am/types.h"

typedef struct AxMixerStats {
    u64 lists;     /* listas executadas */
    u64 commands;  /* comandos executados (sem contar CMD_END) */
    u64 outputs;   /* CMD_OUTPUT: quadros de 5 ms entregues à MRAM */
    u64 clipped;   /* amostras que saturaram em 16 bits no CMD_OUTPUT */
    u32 used_mask; /* bit n = comando n apareceu */
} AxMixerStats;

/* Estado novo, como um AXUCode recém-criado (troca de microcódigo): buffers zerados, compressor parado. */
void ax_mixer_reset(void);
/* HandleCommandList: executa a lista de `words` palavras em `addr` (físico) até CMD_END. Comando desconhecido é
 * am_fatal (o Dolphin registra e para a lista). `crc` é o microcódigo atual: o 0x4E8A8B21 tem outro PB e não é
 * suportado. */
void ax_mixer_command_list(u32 addr, u32 words, u32 crc);
const AxMixerStats *ax_mixer_stats(void);
/* Os buffers de mixagem (para teste). */
AxMixBuffers *ax_mixer_buffers(void);

#endif /* AM_AX_MIXER_H */
