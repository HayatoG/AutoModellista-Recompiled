/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Interface sobre a janela (M9 parte 5): Dear ImGui com os backends oficiais da SDL3 (imgui_impl_sdl3), desenhada
 * pelo SDL_GPU quando a janela é do device da GPU (imgui_impl_sdlgpu3) e pelo SDL_Renderer no resto
 * (imgui_impl_sdlrenderer3). Só a plataforma conhece a SDL; quem desenha (a tela inicial e o contador de quadros, em
 * runtime/front/) usa a API do ImGui dentro da função registrada aqui, chamada a cada platform_window_present.
 *
 * A interface é desenhada depois da imagem do jogo e depois da leitura de volta do --shot com janela: nunca entra no
 * XFB nem nas capturas. */
#ifndef AM_PLATFORM_UI_H
#define AM_PLATFORM_UI_H

#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PlatformUiDrawFn)(void *user);

/* Cria o contexto do ImGui e as fontes (as embutidas no executável). Pode vir
 * antes ou depois de abrir a janela: os backends entram quando a janela tem forma de apresentação. */
bool platform_ui_init(void);
void platform_ui_shutdown(void);
bool platform_ui_ready(void);
/* Função chamada a cada quadro entre ImGui::NewFrame e ImGui::Render (NULL = nada). */
void platform_ui_set_draw(PlatformUiDrawFn fn, void *user);
/* Fontes embutidas (am/ui_assets.h): Saira em quatro pesos, Saira Condensed ExtraBold (títulos) e JetBrains Mono.
 * BODY e BOLD são as do contador de quadros. Ponteiros ImFont*. */
typedef enum PlatformUiFont {
    PLATFORM_UI_FONT_BODY,     /* Saira 500 */
    PLATFORM_UI_FONT_BOLD,     /* Saira 700 */
    PLATFORM_UI_FONT_REGULAR,  /* Saira 400 */
    PLATFORM_UI_FONT_SEMIBOLD, /* Saira 600 */
    PLATFORM_UI_FONT_DISPLAY,  /* Saira Condensed 800 */
    PLATFORM_UI_FONT_MONO,     /* JetBrains Mono 400 */
    PLATFORM_UI_FONT_COUNT,
} PlatformUiFont;
void *platform_ui_font(u32 which);
/* Imagem RGBA para ImGui::Image (cópia guardada; recriada se a forma de apresentação mudar). Devolve um identificador
 * (1, 2...) ou 0 em falha; platform_ui_texture_id dá o ImTextureID do quadro atual (0 = indisponível). */
u32 platform_ui_texture_add(const u8 *rgba, u32 width, u32 height);
u64 platform_ui_texture_id(u32 handle);
/* Troca a imagem de uma textura já criada (a miniatura do vidro muda com a intensidade). Chamar antes de pedir o id no
 * mesmo quadro. */
bool platform_ui_texture_update(u32 handle, const u8 *rgba, u32 width, u32 height);
/* PNG da memória para RGBA (linhas contíguas), em memória a liberar com platform_ui_free. */
bool platform_ui_decode_png(const u8 *data, size_t size, u8 **rgba, u32 *width, u32 *height);
void platform_ui_free(void *p);

#ifdef __cplusplus
}
#endif

#endif /* AM_PLATFORM_UI_H */
