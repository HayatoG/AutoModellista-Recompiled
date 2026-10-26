/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ligações entre os arquivos da plataforma (não é API pública: só runtime/platform/ inclui). */
#ifndef AM_PLATFORM_INTERNAL_H
#define AM_PLATFORM_INTERNAL_H

#include "am/types.h"

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* platform.c */
void platform_set_error(const char *msg);

/* platform_gpu.c: o device do GX na GPU (NULL sem ele). */
SDL_GPUDevice *platform_gpu_device(void);

/* platform_window.c: o device vai ser destruído; a janela larga o swapchain (e volta ao SDL_Renderer no próximo
 * quadro). */
void platform_window_release_gpu(void);

/* platform_present_gpu.c: apresentação pelo swapchain do device da GPU. */
bool present_gpu_attach(SDL_Window *window, SDL_GPUDevice *device, bool vsync);
void present_gpu_detach(void);
void present_gpu_set_vsync(bool on);
SDL_GPUTextureFormat present_gpu_format(void);
/* 60 FPS: quadros descartados porque o swapchain não deu textura. */
u64 present_gpu_dropped(void);
/* Envia a imagem (rgba NULL = só o fundo preto), a interface já montada (ui) e apresenta. false = erro. */
bool present_gpu_frame(const u8 *rgba, u32 width, u32 height, const char *save_png, bool ui);

/* platform_ui.cpp */
/* A forma de apresentação mudou: renderer, device ou nenhum (os dois NULL). */
void platform_ui_on_presenter(SDL_Window *window, SDL_Renderer *renderer, SDL_GPUDevice *device,
                              SDL_GPUTextureFormat format);
/* Repassa o evento ao ImGui (o jogo lê o teclado por estado, então não perde nada). */
void platform_ui_event(const SDL_Event *ev);
/* NewFrame, função de desenho e Render. true = há algo para desenhar. */
bool platform_ui_build(void);
void platform_ui_render_renderer(SDL_Renderer *renderer);
void platform_ui_prepare_gpu(SDL_GPUCommandBuffer *cmd);
void platform_ui_render_gpu(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass);

#ifdef __cplusplus
}
#endif

#endif /* AM_PLATFORM_INTERNAL_H */
