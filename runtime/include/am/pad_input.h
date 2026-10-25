/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Teclado e gamepad da janela -> controle de GameCube (M7; remapeável desde o M9 parte 5). Padrões em
 * docs/M7_MENUS.md §3, os mesmos do Dolphin:
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
 * Cada controle (PadControl) tem uma tecla e um conjunto de botões do gamepad. A tecla é resolvida pela plataforma
 * (platform_input_set_keys): o bit `1 << controle` de PlatformInput.keys diz se a tecla daquele controle está
 * apertada. Os sticks analógicos do gamepad ficam fixos (esquerdo = stick, direito = stick C); botões ligados a uma
 * direção de stick dão a inclinação inteira. L e R são analógicos quando ligados a um gatilho do gamepad.
 * Teclado e gamepad valem juntos: botões somam, e em cada eixo vale o que estiver mais inclinado. */
#ifndef AM_PAD_INPUT_H
#define AM_PAD_INPUT_H

#include "am/platform.h"
#include "am/si.h"
#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zona morta radial dos sticks do gamepad (fração do curso); fora dela o curso é reescalado para começar em 0. */
#define PAD_INPUT_DEADZONE 0.12F
/* Fração do gatilho analógico a partir da qual o clique digital (PAD_TRIGGER_L/R) é apertado. */
#define PAD_INPUT_TRIGGER_CLICK 0.9F
/* Gatilho ligado a um botão digital (A, START...): apertado a partir desta fração. */
#define PAD_INPUT_TRIGGER_BUTTON 0.5F

typedef enum PadControl {
    PAD_CONTROL_A,
    PAD_CONTROL_B,
    PAD_CONTROL_X,
    PAD_CONTROL_Y,
    PAD_CONTROL_Z,
    PAD_CONTROL_START,
    PAD_CONTROL_UP, /* direcional */
    PAD_CONTROL_DOWN,
    PAD_CONTROL_LEFT,
    PAD_CONTROL_RIGHT,
    PAD_CONTROL_STICK_UP,
    PAD_CONTROL_STICK_DOWN,
    PAD_CONTROL_STICK_LEFT,
    PAD_CONTROL_STICK_RIGHT,
    PAD_CONTROL_C_UP,
    PAD_CONTROL_C_DOWN,
    PAD_CONTROL_C_LEFT,
    PAD_CONTROL_C_RIGHT,
    PAD_CONTROL_L,
    PAD_CONTROL_R,
    PAD_CONTROL_HALF, /* modificador: stick com meia inclinação */
    PAD_CONTROL_COUNT,
} PadControl;

/* Botões do gamepad de cada controle: bits de PlatformPadButton. */
typedef struct PadBindings {
    u32 pad[PAD_CONTROL_COUNT];
} PadBindings;

void pad_input_default_bindings(PadBindings *out);
/* Liga o mapeamento do gamepad (o padrão vale até a primeira chamada). */
void pad_input_set_bindings(const PadBindings *b);
void pad_input_map(const PlatformInput *in, SiPadState *out);

/* Nomes estáveis (arquivo de configuração) e rótulos (tela). */
const char *pad_input_control_id(PadControl c);    /* "A", "DPAD_UP", "STICK_LEFT"... */
const char *pad_input_control_label(PadControl c); /* "A", "Direcional: cima"... */
bool pad_input_control_from_id(const char *id, PadControl *out);
/* Tecla padrão de cada controle, no nome da SDL (SDL_GetScancodeName): "X", "Return", "Left Shift"... */
const char *pad_input_default_key(PadControl c);
const char *pad_input_button_id(PlatformPadButton b);    /* "south", "leftshoulder"... */
const char *pad_input_button_label(PlatformPadButton b); /* "Sul (A do Xbox)"... */
bool pad_input_button_from_id(const char *id, PlatformPadButton *out);

#ifdef __cplusplus
}
#endif

#endif /* AM_PAD_INPUT_H */
