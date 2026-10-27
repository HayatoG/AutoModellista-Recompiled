/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Dear ImGui sobre a janela (am/platform_ui.h). C++ só porque o ImGui é C++: o resto da runtime vê a API em C. Os
 * backends são os oficiais do ImGui para a SDL3: imgui_impl_sdl3 (eventos), imgui_impl_sdlgpu3 (desenho no device da
 * GPU, shaders embutidos no próprio backend) e imgui_impl_sdlrenderer3 (desenho pelo SDL_Renderer). */
#include "am/platform_ui.h"

#include <cstdio>
#include <cstring>

#include "am/ui_assets.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "platform_internal.h"

namespace {

constexpr u32 kMaxTextures = 8;
constexpr float kFontSize = 20.0F;

struct UiTexture {
    u8 *rgba;
    u32 w, h;
    SDL_Texture *sdl;
    SDL_GPUTexture *gpu; /* desde o ImGui 1.92.2, o ImTextureID do backend da SDL GPU é o SDL_GPUTexture* */
};

struct UiState {
    bool ready;
    bool backends;
    bool scaled;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_GPUDevice *device;
    SDL_GPUTextureFormat format;
    PlatformUiDrawFn draw;
    void *user;
    ImFont *fonts[PLATFORM_UI_FONT_COUNT];
    UiTexture tex[kMaxTextures];
    u32 tex_count;
};

UiState s_ui;

/* Fontes OFL embutidas (am/ui_assets.h, third_party/fonts): independem do que o sistema tem. */
ImFont *load_font(UiAssetId id) {
    ImGuiIO &io = ImGui::GetIO();
    const UiAsset a = ui_asset(id);
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; /* os bytes são do executável */
    ImFont *f =
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(a.data), static_cast<int>(a.size), 0.0F, &cfg);
    return f != nullptr ? f : io.Fonts->AddFontDefaultVector();
}

void release_texture_objects(UiTexture &t) {
    if (t.sdl != nullptr) {
        SDL_DestroyTexture(t.sdl);
        t.sdl = nullptr;
    }
    if (t.gpu != nullptr) {
        SDL_ReleaseGPUTexture(s_ui.device, t.gpu);
        t.gpu = nullptr;
    }
}

/* Escala do monitor além da densidade de pixels (Windows e Linux com 150%...). No macOS com Retina dá 1: a densidade
 * já vem no DisplayFramebufferScale do ImGui. */
void apply_content_scale() {
    if (s_ui.scaled) {
        return;
    }
    const float density = SDL_GetWindowPixelDensity(s_ui.window);
    const float scale = SDL_GetWindowDisplayScale(s_ui.window) / (density > 0.0F ? density : 1.0F);
    if (scale > 1.01F) {
        ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui::GetStyle().FontScaleDpi = scale;
    }
    s_ui.scaled = true;
}

void backends_up() {
    if (!s_ui.ready || s_ui.backends || s_ui.window == nullptr) {
        return;
    }
    if (s_ui.renderer != nullptr) {
        ImGui_ImplSDL3_InitForSDLRenderer(s_ui.window, s_ui.renderer);
        ImGui_ImplSDLRenderer3_Init(s_ui.renderer);
    } else if (s_ui.device != nullptr) {
        ImGui_ImplSDL3_InitForSDLGPU(s_ui.window);
        ImGui_ImplSDLGPU3_InitInfo info;
        info.Device = s_ui.device;
        info.ColorTargetFormat = s_ui.format;
        ImGui_ImplSDLGPU3_Init(&info);
    } else {
        return;
    }
    apply_content_scale();
    s_ui.backends = true;
}

void backends_down() {
    if (!s_ui.backends) {
        return;
    }
    for (u32 i = 0; i < s_ui.tex_count; i++) {
        release_texture_objects(s_ui.tex[i]);
    }
    if (s_ui.renderer != nullptr) {
        ImGui_ImplSDLRenderer3_Shutdown();
    } else {
        ImGui_ImplSDLGPU3_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    s_ui.backends = false;
}

void style() {
    ImGuiStyle &st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.FontSizeBase = kFontSize;
    st.WindowRounding = 10.0F;
    st.FrameRounding = 6.0F;
    st.WindowBorderSize = 0.0F;
    st.ItemSpacing = ImVec2(10.0F, 8.0F);
    st.SelectableTextAlign = ImVec2(0.0F, 0.5F);
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0.04F, 0.07F, 0.14F, 0.86F);
    st.Colors[ImGuiCol_Header] = ImVec4(0.11F, 0.31F, 0.62F, 0.70F);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.16F, 0.40F, 0.78F, 0.85F);
    st.Colors[ImGuiCol_HeaderActive] = ImVec4(0.80F, 0.10F, 0.12F, 0.90F);
    st.Colors[ImGuiCol_NavCursor] = ImVec4(0.95F, 0.20F, 0.20F, 1.0F);
}

SDL_Texture *renderer_texture(UiTexture &t) {
    if (t.sdl == nullptr) {
        t.sdl = SDL_CreateTexture(s_ui.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                  static_cast<int>(t.w), static_cast<int>(t.h));
        if (t.sdl != nullptr) {
            (void)SDL_UpdateTexture(t.sdl, nullptr, t.rgba, static_cast<int>(t.w * 4U));
            (void)SDL_SetTextureScaleMode(t.sdl, SDL_SCALEMODE_LINEAR);
        }
    }
    return t.sdl;
}

SDL_GPUTexture *gpu_texture(UiTexture &t) {
    if (t.gpu != nullptr) {
        return t.gpu;
    }
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = t.w;
    ti.height = t.h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    SDL_GPUTransferBufferCreateInfo bi{};
    bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    bi.size = t.w * t.h * 4U;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(s_ui.device, &ti);
    SDL_GPUTransferBuffer *buf = SDL_CreateGPUTransferBuffer(s_ui.device, &bi);
    void *map = buf != nullptr ? SDL_MapGPUTransferBuffer(s_ui.device, buf, false) : nullptr;
    SDL_GPUCommandBuffer *cmd = map != nullptr ? SDL_AcquireGPUCommandBuffer(s_ui.device) : nullptr;
    if (tex != nullptr && cmd != nullptr) {
        std::memcpy(map, t.rgba, bi.size);
        SDL_UnmapGPUTransferBuffer(s_ui.device, buf);
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = buf;
        src.pixels_per_row = t.w;
        SDL_GPUTextureRegion dst{};
        dst.texture = tex;
        dst.w = t.w;
        dst.h = t.h;
        dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
        SDL_EndGPUCopyPass(copy);
        (void)SDL_SubmitGPUCommandBuffer(cmd);
        t.gpu = tex;
    } else if (tex != nullptr) {
        SDL_ReleaseGPUTexture(s_ui.device, tex);
    }
    if (buf != nullptr) {
        SDL_ReleaseGPUTransferBuffer(s_ui.device, buf);
    }
    return t.gpu;
}

} // namespace

extern "C" {

bool platform_ui_init(void) {
    if (s_ui.ready) {
        return true;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; /* nada de imgui.ini: as configurações do jogo ficam no arquivo próprio */
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    style();
    s_ui.fonts[PLATFORM_UI_FONT_BODY] = load_font(UI_ASSET_FONT_SAIRA_500);
    s_ui.fonts[PLATFORM_UI_FONT_BOLD] = load_font(UI_ASSET_FONT_SAIRA_700);
    s_ui.fonts[PLATFORM_UI_FONT_REGULAR] = load_font(UI_ASSET_FONT_SAIRA_400);
    s_ui.fonts[PLATFORM_UI_FONT_SEMIBOLD] = load_font(UI_ASSET_FONT_SAIRA_600);
    s_ui.fonts[PLATFORM_UI_FONT_DISPLAY] = load_font(UI_ASSET_FONT_SAIRA_COND_800);
    s_ui.fonts[PLATFORM_UI_FONT_MONO] = load_font(UI_ASSET_FONT_MONO_400);
    s_ui.ready = true;
    backends_up();
    return true;
}

void platform_ui_shutdown(void) {
    if (!s_ui.ready) {
        return;
    }
    backends_down();
    for (u32 i = 0; i < s_ui.tex_count; i++) {
        SDL_free(s_ui.tex[i].rgba);
    }
    ImGui::DestroyContext();
    s_ui = UiState{};
}

bool platform_ui_ready(void) {
    return s_ui.ready;
}

void platform_ui_set_draw(PlatformUiDrawFn fn, void *user) {
    s_ui.draw = fn;
    s_ui.user = user;
}

void *platform_ui_font(u32 which) {
    return which < static_cast<u32>(PLATFORM_UI_FONT_COUNT) ? s_ui.fonts[which] : nullptr;
}

u32 platform_ui_texture_add(const u8 *rgba, u32 width, u32 height) {
    if (s_ui.tex_count == kMaxTextures) {
        return 0;
    }
    UiTexture &t = s_ui.tex[s_ui.tex_count];
    t = UiTexture{};
    t.rgba = static_cast<u8 *>(SDL_malloc(static_cast<size_t>(width) * height * 4U));
    if (t.rgba == nullptr) {
        return 0;
    }
    std::memcpy(t.rgba, rgba, static_cast<size_t>(width) * height * 4U);
    t.w = width;
    t.h = height;
    return ++s_ui.tex_count;
}

bool platform_ui_texture_update(u32 handle, const u8 *rgba, u32 width, u32 height) {
    if (handle == 0U || handle > s_ui.tex_count) {
        return false;
    }
    UiTexture &t = s_ui.tex[handle - 1U];
    const size_t bytes = static_cast<size_t>(width) * height * 4U;
    u8 *copy = static_cast<u8 *>(SDL_malloc(bytes));
    if (copy == nullptr) {
        return false;
    }
    std::memcpy(copy, rgba, bytes);
    release_texture_objects(t);
    SDL_free(t.rgba);
    t.rgba = copy;
    t.w = width;
    t.h = height;
    return true;
}

bool platform_ui_decode_png(const u8 *data, size_t size, u8 **rgba, u32 *width, u32 *height) {
    SDL_IOStream *io = SDL_IOFromConstMem(data, size);
    SDL_Surface *png = io != nullptr ? SDL_LoadPNG_IO(io, true) : nullptr;
    SDL_Surface *conv = png != nullptr ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;
    SDL_DestroySurface(png);
    if (conv == nullptr) {
        return false;
    }
    const size_t row = static_cast<size_t>(conv->w) * 4U;
    u8 *out = static_cast<u8 *>(SDL_malloc(row * static_cast<size_t>(conv->h)));
    if (out != nullptr) {
        for (int y = 0; y < conv->h; y++) {
            std::memcpy(out + row * static_cast<size_t>(y), static_cast<const u8 *>(conv->pixels) + conv->pitch * y,
                        row);
        }
        *rgba = out;
        *width = static_cast<u32>(conv->w);
        *height = static_cast<u32>(conv->h);
    }
    SDL_DestroySurface(conv);
    return out != nullptr;
}

void platform_ui_free(void *p) {
    SDL_free(p);
}

u64 platform_ui_texture_id(u32 handle) {
    if (!s_ui.backends || handle == 0U || handle > s_ui.tex_count) {
        return 0;
    }
    UiTexture &t = s_ui.tex[handle - 1U];
    if (s_ui.renderer != nullptr) {
        return static_cast<u64>(reinterpret_cast<uintptr_t>(renderer_texture(t)));
    }
    return static_cast<u64>(reinterpret_cast<uintptr_t>(gpu_texture(t)));
}

void platform_ui_on_presenter(SDL_Window *window, SDL_Renderer *renderer, SDL_GPUDevice *device,
                              SDL_GPUTextureFormat format) {
    backends_down();
    s_ui.window = window;
    s_ui.renderer = renderer;
    s_ui.device = device;
    s_ui.format = format;
    backends_up();
}

void platform_ui_event(const SDL_Event *ev) {
    if (s_ui.backends) {
        (void)ImGui_ImplSDL3_ProcessEvent(ev);
    }
}

bool platform_ui_build(void) {
    if (!s_ui.backends) {
        return false;
    }
    if (s_ui.renderer != nullptr) {
        ImGui_ImplSDLRenderer3_NewFrame();
    } else {
        ImGui_ImplSDLGPU3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (s_ui.draw != nullptr) {
        s_ui.draw(s_ui.user);
    }
    ImGui::Render();
    const ImDrawData *d = ImGui::GetDrawData();
    return d->DisplaySize.x > 0.0F && d->DisplaySize.y > 0.0F;
}

/* Como no exemplo oficial (example_sdl3_sdlrenderer3): o backend desenha em pontos e conta com a escala da
 * densidade de pixels no renderer (Retina); sem ela, a interface sai num quarto da janela e os recortes, errados. */
void platform_ui_render_renderer(SDL_Renderer *renderer) {
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    (void)SDL_SetRenderScale(renderer, scale.x, scale.y);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    (void)SDL_SetRenderScale(renderer, 1.0F, 1.0F);
}

void platform_ui_prepare_gpu(SDL_GPUCommandBuffer *cmd) {
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd);
}

void platform_ui_render_gpu(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass) {
    ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd, pass);
}

} /* extern "C" */
