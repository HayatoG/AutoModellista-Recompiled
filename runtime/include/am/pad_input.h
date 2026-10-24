/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Teclado e gamepad da janela -> controle de GameCube (M7). Mapeamento fixo, documentado em docs/M7_MENUS.md §3:
 *
 *   GameCube        teclado (padrão do Dolphin, GCPadEmu.cpp)   gamepad (SDL_Gamepad, por posição)
 *   A B X Y         X Z C S                                      sul, oeste, leste, norte
 *   Z               D                                            ombro direito ou esquerdo
 *   START           Return                                       Start
 *   direcional      T G F H (cima, baixo, esquerda, direita)     direcional
 *   stick           setas (Shift = meia inclinação)              stick esquerdo
 *   stick C         I K J L                                      stick direito
 *   L R             Q W (gatilho inteiro + clique)               gatilhos (analógico; clique a partir de 90%)
 *
 * Teclado e gamepad valem juntos: botões somam, e em cada eixo vale o que estiver mais inclinado. */
#ifndef AM_PAD_INPUT_H
#define AM_PAD_INPUT_H

#include "am/platform.h"
#include "am/si.h"
#include "am/types.h"

/* Zona morta radial dos sticks do gamepad (fração do curso); fora dela o curso é reescalado para começar em 0. */
#define PAD_INPUT_DEADZONE 0.12f
/* Fração do gatilho analógico a partir da qual o clique digital (PAD_TRIGGER_L/R) é apertado. */
#define PAD_INPUT_TRIGGER_CLICK 0.9f

void pad_input_map(const PlatformInput *in, SiPadState *out);

#endif /* AM_PAD_INPUT_H */
