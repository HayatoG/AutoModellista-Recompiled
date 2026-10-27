/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M9 parte 5: com o GX na GPU, a janela é apresentada pelo swapchain do mesmo device da SDL3 GPU (sem um segundo
 * device do SDL_Renderer). O XFB vai para uma textura e é copiado para o swapchain com SDL_BlitGPUTexture (barras
 * pretas, a mesma conta do SDL_LOGICAL_PRESENTATION_LETTERBOX) com o filtro escolhido (PlatformFilter: nearest, linear
 * ou sharp, que são dois blits); a interface (imgui_impl_sdlgpu3) é desenhada por cima num passe com LOAD. Nenhum
 * shader próprio: o blit é da SDL e os shaders do ImGui vêm embutidos no backend dele (SPIR-V, DXBC e metallib). */
#include <stdio.h>
#include <string.h>

#include "am/platform.h"

#include "platform_internal.h"

static struct {
    SDL_GPUDevice *device;
    SDL_Window *window;
    SDL_GPUTexture *xfb;
    u32 xfb_w, xfb_h;
    SDL_GPUTexture *mid; /* filtro sharp: o XFB já ampliado por nearest numa escala inteira */
    u32 mid_w, mid_h;
    SDL_GPUTransferBuffer *upload;
    u32 upload_size;
    SDL_GPUTextureFormat format;
} s_pg;

/* 60 FPS: quadros em que o swapchain não deu textura e nada foi mostrado (--frame-trace; sobrevive ao detach). */
static u64 s_dropped;

/* Filtro da imagem (PlatformFilter), também fora de s_pg: a escolha vale antes do attach e depois de um detach. */
static u32 s_filter;

void present_gpu_set_filter(u32 filter) {
    s_filter = filter < (u32)PLATFORM_FILTER_COUNT ? filter : (u32)PLATFORM_FILTER_NEAREST;
}

u64 present_gpu_dropped(void) {
    return s_dropped;
}

static bool fail(const char *what) {
    char msg[256];
    (void)snprintf(msg, sizeof(msg), "%s: %s", what, SDL_GetError());
    platform_set_error(msg);
    return false;
}

bool present_gpu_attach(SDL_Window *window, SDL_GPUDevice *device, bool vsync) {
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        return fail("SDL_ClaimWindowForGPUDevice");
    }
    s_pg.device = device;
    s_pg.window = window;
    s_pg.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    present_gpu_set_vsync(vsync);
    return true;
}

void present_gpu_detach(void) {
    if (s_pg.device == NULL) {
        return;
    }
    SDL_WaitForGPUIdle(s_pg.device);
    if (s_pg.xfb != NULL) {
        SDL_ReleaseGPUTexture(s_pg.device, s_pg.xfb);
    }
    if (s_pg.mid != NULL) {
        SDL_ReleaseGPUTexture(s_pg.device, s_pg.mid);
    }
    if (s_pg.upload != NULL) {
        SDL_ReleaseGPUTransferBuffer(s_pg.device, s_pg.upload);
    }
    SDL_ReleaseWindowFromGPUDevice(s_pg.device, s_pg.window);
    memset(&s_pg, 0, sizeof(s_pg));
}

/* Sem VSync: IMMEDIATE se a janela aceitar, senão MAILBOX (não bloqueia), senão o VSYNC, que sempre existe. */
void present_gpu_set_vsync(bool on) {
    if (s_pg.device == NULL) {
        return;
    }
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!on && SDL_WindowSupportsGPUPresentMode(s_pg.device, s_pg.window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
        mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    } else if (!on && SDL_WindowSupportsGPUPresentMode(s_pg.device, s_pg.window, SDL_GPU_PRESENTMODE_MAILBOX)) {
        mode = SDL_GPU_PRESENTMODE_MAILBOX;
    }
    (void)SDL_SetGPUSwapchainParameters(s_pg.device, s_pg.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
}

SDL_GPUTextureFormat present_gpu_format(void) {
    return s_pg.format;
}

static bool ensure_xfb(u32 width, u32 height) {
    if (s_pg.xfb != NULL && s_pg.xfb_w == width && s_pg.xfb_h == height) {
        return true;
    }
    if (s_pg.xfb != NULL) {
        SDL_ReleaseGPUTexture(s_pg.device, s_pg.xfb);
        SDL_ReleaseGPUTransferBuffer(s_pg.device, s_pg.upload);
        s_pg.xfb = NULL;
        s_pg.upload = NULL;
    }
    const SDL_GPUTextureCreateInfo tex = {.type = SDL_GPU_TEXTURETYPE_2D,
                                          .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                          .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                          .width = width,
                                          .height = height,
                                          .layer_count_or_depth = 1,
                                          .num_levels = 1};
    const SDL_GPUTransferBufferCreateInfo up = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                .size = width * height * 4U};
    s_pg.xfb = SDL_CreateGPUTexture(s_pg.device, &tex);
    s_pg.upload = SDL_CreateGPUTransferBuffer(s_pg.device, &up);
    if (s_pg.xfb == NULL || s_pg.upload == NULL) {
        return fail("textura do XFB");
    }
    s_pg.xfb_w = width;
    s_pg.xfb_h = height;
    return true;
}

static bool upload_xfb(SDL_GPUCommandBuffer *cmd, const u8 *rgba, u32 width, u32 height) {
    if (!ensure_xfb(width, height)) {
        return false;
    }
    u8 *map = SDL_MapGPUTransferBuffer(s_pg.device, s_pg.upload, true);
    if (map == NULL) {
        return fail("SDL_MapGPUTransferBuffer (XFB)");
    }
    memcpy(map, rgba, (size_t)width * height * 4U);
    SDL_UnmapGPUTransferBuffer(s_pg.device, s_pg.upload);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTextureTransferInfo src = {.transfer_buffer = s_pg.upload, .pixels_per_row = width};
    const SDL_GPUTextureRegion dst = {.texture = s_pg.xfb, .w = width, .h = height, .d = 1};
    SDL_UploadToGPUTexture(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);
    return true;
}

/* Filtro sharp: a textura intermediária (width x height, o XFB vezes k), recriada só quando k muda. */
static bool ensure_mid(u32 width, u32 height) {
    if (s_pg.mid != NULL && s_pg.mid_w == width && s_pg.mid_h == height) {
        return true;
    }
    if (s_pg.mid != NULL) {
        SDL_ReleaseGPUTexture(s_pg.device, s_pg.mid);
        s_pg.mid = NULL;
    }
    const SDL_GPUTextureCreateInfo tex = {.type = SDL_GPU_TEXTURETYPE_2D,
                                          .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                          .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                          .width = width,
                                          .height = height,
                                          .layer_count_or_depth = 1,
                                          .num_levels = 1};
    s_pg.mid = SDL_CreateGPUTexture(s_pg.device, &tex);
    if (s_pg.mid == NULL) {
        return false;
    }
    s_pg.mid_w = width;
    s_pg.mid_h = height;
    return true;
}

/* XFB escalado para caber em target (tw x th), centralizado, fundo preto, com o filtro escolhido. */
static void blit_xfb(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target, u32 tw, u32 th) {
    const float sx = (float)tw / (float)s_pg.xfb_w;
    const float sy = (float)th / (float)s_pg.xfb_h;
    const float s = sx < sy ? sx : sy;
    const u32 w = (u32)((float)s_pg.xfb_w * s);
    const u32 h = (u32)((float)s_pg.xfb_h * s);
    SDL_GPUTexture *source = s_pg.xfb;
    u32 source_w = s_pg.xfb_w;
    u32 source_h = s_pg.xfb_h;
    SDL_GPUFilter filter = s_filter == (u32)PLATFORM_FILTER_NEAREST ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    if (s_filter == (u32)PLATFORM_FILTER_SHARP) {
        /* Escala não inteira: primeiro k vezes por nearest (pixels nítidos), depois o bilinear só tira a diferença
         * entre k e a escala de verdade. Abaixo de 2x o bilinear direto é o mesmo resultado. Escala inteira: nearest.
         */
        const u32 k = (u32)s;
        if (s == (float)k) {
            filter = SDL_GPU_FILTER_NEAREST;
        } else if (k >= 2U && ensure_mid(s_pg.xfb_w * k, s_pg.xfb_h * k)) {
            const SDL_GPUBlitInfo up = {
                .source = {.texture = s_pg.xfb, .w = s_pg.xfb_w, .h = s_pg.xfb_h},
                .destination = {.texture = s_pg.mid, .w = s_pg.mid_w, .h = s_pg.mid_h},
                .load_op = SDL_GPU_LOADOP_DONT_CARE,
                .filter = SDL_GPU_FILTER_NEAREST,
            };
            SDL_BlitGPUTexture(cmd, &up);
            source = s_pg.mid;
            source_w = s_pg.mid_w;
            source_h = s_pg.mid_h;
        }
    }
    const SDL_GPUBlitInfo blit = {
        .source = {.texture = source, .w = source_w, .h = source_h},
        .destination = {.texture = target, .x = (tw - w) / 2U, .y = (th - h) / 2U, .w = w, .h = h},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .clear_color = {0.0F, 0.0F, 0.0F, 1.0F},
        .filter = filter,
    };
    SDL_BlitGPUTexture(cmd, &blit);
}

static void clear(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target) {
    const SDL_GPUColorTargetInfo color = {.texture = target,
                                          .clear_color = {0.0F, 0.0F, 0.0F, 1.0F},
                                          .load_op = SDL_GPU_LOADOP_CLEAR,
                                          .store_op = SDL_GPU_STOREOP_STORE};
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &color, 1, NULL));
}

/* --shot com janela: a mesma imagem numa textura do tamanho do swapchain, lida de volta (sem a interface). */
typedef struct Readback {
    SDL_GPUTexture *texture;
    SDL_GPUTransferBuffer *buffer;
    u32 w, h;
} Readback;

static bool readback_record(SDL_GPUCommandBuffer *cmd, u32 w, u32 h, bool image, Readback *rb) {
    const SDL_GPUTextureCreateInfo tex = {.type = SDL_GPU_TEXTURETYPE_2D,
                                          .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                          .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                          .width = w,
                                          .height = h,
                                          .layer_count_or_depth = 1,
                                          .num_levels = 1};
    const SDL_GPUTransferBufferCreateInfo down = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = w * h * 4U};
    rb->texture = SDL_CreateGPUTexture(s_pg.device, &tex);
    rb->buffer = SDL_CreateGPUTransferBuffer(s_pg.device, &down);
    rb->w = w;
    rb->h = h;
    if (rb->texture == NULL || rb->buffer == NULL) {
        return fail("leitura da janela");
    }
    if (image) {
        blit_xfb(cmd, rb->texture, w, h);
    } else {
        clear(cmd, rb->texture);
    }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTextureRegion src = {.texture = rb->texture, .w = w, .h = h, .d = 1};
    const SDL_GPUTextureTransferInfo dst = {.transfer_buffer = rb->buffer, .pixels_per_row = w};
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    return true;
}

static bool readback_save(Readback *rb, SDL_GPUFence *fence, const char *path) {
    bool ok = fence != NULL && SDL_WaitForGPUFences(s_pg.device, true, &fence, 1);
    if (fence != NULL) {
        SDL_ReleaseGPUFence(s_pg.device, fence);
    }
    u8 *map = ok ? SDL_MapGPUTransferBuffer(s_pg.device, rb->buffer, false) : NULL;
    if (map != NULL) {
        ok = platform_save_png(path, map, rb->w, rb->h);
        SDL_UnmapGPUTransferBuffer(s_pg.device, rb->buffer);
    } else {
        ok = fail("leitura da janela");
    }
    SDL_ReleaseGPUTexture(s_pg.device, rb->texture);
    SDL_ReleaseGPUTransferBuffer(s_pg.device, rb->buffer);
    return ok;
}

bool present_gpu_frame(const u8 *rgba, u32 width, u32 height, const char *save_png, bool ui) {
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(s_pg.device);
    if (cmd == NULL) {
        return fail("SDL_AcquireGPUCommandBuffer (janela)");
    }
    if (rgba != NULL && !upload_xfb(cmd, rgba, width, height)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    SDL_GPUTexture *swap = NULL;
    u32 sw = 0;
    u32 sh = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, s_pg.window, &swap, &sw, &sh)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return fail("SDL_AcquireGPUSwapchainTexture");
    }
    if (swap == NULL) { /* janela minimizada ou swapchain ocupado: este quadro não aparece */
        s_dropped++;
        return SDL_SubmitGPUCommandBuffer(cmd) || fail("SDL_SubmitGPUCommandBuffer (janela)");
    }
    const bool image = rgba != NULL && s_pg.xfb != NULL;
    Readback rb = {0};
    if (save_png != NULL && !readback_record(cmd, sw, sh, image, &rb)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    if (ui) {
        platform_ui_prepare_gpu(cmd); /* cópias dos vértices: antes de qualquer passe de desenho na tela */
    }
    if (image) {
        blit_xfb(cmd, swap, sw, sh);
    } else {
        clear(cmd, swap);
    }
    if (ui) {
        const SDL_GPUColorTargetInfo color = {
            .texture = swap, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE};
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color, 1, NULL);
        platform_ui_render_gpu(cmd, pass);
        SDL_EndGPURenderPass(pass);
    }
    if (save_png != NULL) {
        return readback_save(&rb, SDL_SubmitGPUCommandBufferAndAcquireFence(cmd), save_png);
    }
    return SDL_SubmitGPUCommandBuffer(cmd) || fail("SDL_SubmitGPUCommandBuffer (janela)");
}
