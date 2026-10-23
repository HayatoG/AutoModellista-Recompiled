/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Roteiro de botões para testes (M5): "quadro:BOTÃO[+BOTÃO]:duração,...", por exemplo "200:D_LEFT:8,240:A:8". O
 * botão fica apertado nos quadros [quadro, quadro + duração). Os nomes são os do controle por pipe do Dolphin (A, B,
 * X, Y, Z, START, D_UP, D_DOWN, D_LEFT, D_RIGHT), para o mesmo roteiro valer nos dois lados
 * (scripts/dolphin_trace.py --pad-script). L/R ficam de fora: no Dolphin eles também mexem no gatilho analógico. */
#ifndef AM_PAD_SCRIPT_H
#define AM_PAD_SCRIPT_H

#include "am/types.h"

#define PAD_SCRIPT_MAX 64U

typedef struct PadScriptStep {
    u64 frame;
    u64 hold;
    u16 buttons;
} PadScriptStep;

typedef struct PadScript {
    PadScriptStep steps[PAD_SCRIPT_MAX];
    u32 count;
} PadScript;

/* false se o texto é inválido (nome desconhecido, número ruim, passos demais). */
bool pad_script_parse(const char *text, PadScript *out);
/* Botões apertados no quadro `frame`. */
u16 pad_script_buttons(const PadScript *script, u64 frame);

#endif /* AM_PAD_SCRIPT_H */
