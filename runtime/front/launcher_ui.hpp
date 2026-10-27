/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tema e componentes da tela inicial (design em work/launcher-bg/design_Main.dc.html): vidro fosco por miniatura do
 * fundo, texto inclinado, chips, controles segmentados, chaves, barras, botões e ícones, tudo desenhado com o
 * ImDrawList do Dear ImGui. Medidas em "px do design" (1280x760); o fator de escala da janela é aplicado aqui. Só
 * launcher*.cpp inclui este arquivo. */
#ifndef AM_LAUNCHER_UI_HPP
#define AM_LAUNCHER_UI_HPP

#include "am/launcher_gfx.h"

#include "imgui.h"

namespace lui {

enum class Face { Body, Bold, Regular, Semi, Display, Mono };

enum class Icon {
    None,
    Home,
    Monitor,
    Speaker,
    Gamepad,
    Drop,
    Lifebuoy,
    Info,
    Folder,
    Play,
    Check,
    Refresh,
    Close,
    Copy,
    Bug,
    Doc,
    Help,
    Link,
    Chevron,
};

/* Estado do quadro: janela, escala, estilo e texturas. */
struct Frame {
    ImVec2 origin;
    ImVec2 size;
    float s;
    bool glass;
    ImU32 accent;
    ImU32 on_accent; /* texto sobre a cor de destaque */
    ImU64 bg_tex;    /* 0 = sem fundo */
    ImU64 thumb_tex; /* 0 = sem vidro (cai no sólido) */
    LauncherRect cover;
    bool focus_request; /* o próximo item navegável criado por hit() recebe o foco */
};
extern Frame fr;

void begin_frame(const ImGuiViewport *vp, bool glass, ImU64 bg_tex, ImU64 thumb_tex, const LauncherRect &cover);
inline float px(float design) {
    return design * fr.s;
}
ImU32 white(float alpha);
ImU32 black(float alpha);
ImU32 scale_alpha(ImU32 c, float f);

/* ---- texto ---- */
ImFont *font(Face f);
/* Largura de `t` (px da tela); `tracking` é o letter-spacing em em. */
float text_w(Face f, float size_d, const char *t, float tracking = 0.0F);
float line_h(Face f, float size_d); /* altura de linha "normal" do CSS, px da tela */
/* `top_left` é o canto da caixa da linha (altura line_h). */
void text(ImDrawList *dl, Face f, float size_d, ImVec2 top_left, ImU32 col, const char *t, float tracking = 0.0F);
/* Centralizado na vertical em torno de cy (como o flex align-items:center). */
void text_cy(ImDrawList *dl, Face f, float size_d, float x, float cy, ImU32 col, const char *t, float tracking = 0.0F);
/* Texto cortado com "…" para caber em max_w (px da tela). */
void text_fit(ImDrawList *dl, Face f, float size_d, float x, float cy, float max_w, ImU32 col, const char *t);
/* Parágrafo com quebra de linha por palavra e altura de linha fixa (px do design); devolve a altura usada (tela). Com
 * dl == nullptr só mede. */
float paragraph(ImDrawList *dl, Face f, float size_d, float lh_d, ImVec2 top_left, float max_w, ImU32 col,
                const char *t);
/* Texto cisalhado como skewX(-deg) com origem na altura `shear_y` (a base da caixa, como transform-origin: left
 * bottom): o título inclinado. Várias cópias deslocadas são desenhadas na ordem pedida (sombra de trás para a frente).
 */
struct SkewLayer {
    ImVec2 offset; /* px do design */
    ImU32 col;
};
void text_skewed(ImDrawList *dl, Face f, float size_d, ImVec2 baseline_left, float shear_y, float skew_deg,
                 const char *t, const SkewLayer *layers, int n_layers);
/* Distância da linha de base até o topo da caixa da linha, px da tela. */
float ascent(Face f, float size_d);

/* ---- fundo e painéis ---- */
void draw_background(ImDrawList *dl);
void panel(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d);
void rrect_fill(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d, ImU32 col);
void rrect_stroke(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d, ImU32 col, float thick_d = 1.0F);
void focus_ring(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d);
/* Quadrado arredondado cisalhado (o logotipo AM). */
void skewed_box(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d, float skew_deg, ImU32 col);

void icon(ImDrawList *dl, Icon ic, ImVec2 center, float size_d, ImU32 col, float stroke_d = 1.8F);

/* ---- itens navegáveis ---- */
struct Hit {
    bool pressed = false;
    bool hovered = false;
    bool focused = false; /* foco do teclado/controle com o cursor de navegação visível */
    bool held = false;
};
Hit hit(const char *id, ImVec2 a, ImVec2 b);

/* Chip (cápsula): devolve a largura. kind 0 = vidro, 1 = destaque, 2 = verde. */
float chip(ImDrawList *dl, ImVec2 pos, const char *label, Icon ic, int kind, float height_d = 26.0F,
           float size_d = 12.0F, bool measure_only = false);
/* Etiqueta "EM BREVE": devolve a largura. */
float soon_badge(ImDrawList *dl, ImVec2 pos, const char *label = "EM BREVE");

/* Botão do menu lateral (44 de altura). */
bool nav_item(const char *id, ImVec2 a, ImVec2 b, Icon ic, const char *label, bool active);
/* Botão "ghost" (vidro, borda clara). Devolve se foi apertado; out_w recebe a largura. */
bool ghost(const char *id, ImVec2 pos, const char *label, Icon ic, bool enabled, float min_w_d, float *out_w,
           float height_d = 44.0F);
float ghost_width(const char *label, Icon ic, float min_w_d);
/* Botão de ícone 44x44 / janela. */
bool icon_button(const char *id, ImVec2 a, ImVec2 b, Icon ic, bool danger_hover);

/* ---- formulário de linhas ---- */
struct Form {
    ImDrawList *dl;
    float x, y, w; /* canto superior esquerdo e largura (px da tela) */
    bool first;
    bool nav; /* false: os controles não são navegáveis (captura de tela) */
    const char *badge = nullptr; /* etiqueta ("EXPERIMENTAL") ao lado do título da próxima linha, se não for EM BREVE */
};
struct Row {
    float top, h, cy, right, left_w;
    bool dim;
};
Row row_begin(Form &f, const char *title, const char *desc, float ctrl_w, float ctrl_h, bool soon, bool dim);
void row_end(Form &f, const Row &r);
/* Controles de linha: devolvem a mudança (índice novo / true / valor novo / true). */
int seg(Form &f, const Row &r, const char *id, const char *const *labels, int n, int selected, unsigned enabled_mask);
float seg_width(const char *const *labels, int n);
bool toggle(const Form &f, const Row &r, const char *id, bool on, bool enabled);
int slider(const Form &f, const Row &r, const char *id, int value, int lo, int hi, const char *suffix, bool enabled);
bool row_ghost(const Form &f, const Row &r, const char *id, const char *label, Icon ic, bool enabled, float min_w_d);

} // namespace lui

#endif /* AM_LAUNCHER_UI_HPP */
