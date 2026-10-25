/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Dear ImGui sobre a janela (am/platform_ui.h). C++ só porque o ImGui é C++: o resto da runtime vê a API em C. Os
 * backends são os oficiais do ImGui para a SDL3: imgui_impl_sdl3 (eventos), imgui_impl_sdlgpu3 (desenho no device da
 * GPU, shaders embutidos no próprio backend) e imgui_impl_sdlrenderer3 (desenho pelo SDL_Renderer). */
#include "am/platform_ui.h"

#include <cstdio>
#include <cstring>

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
    ImFont *fonts[2];
    UiTexture tex[kMaxTextures];
    u32 tex_count;
};

UiState s_ui;

/* Fontes do sistema com acentos (a primeira que existir); sem nenhuma, a vetorial embutida no ImGui. */
const char *const kRegular[] = {
    "/System/Library/Fonts/Supplemental/Arial.ttf",        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",   "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",          "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
};
const char *const kBold[] = {
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/noto/NotoSans-Bold.ttf",
    "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
    "C:\\Windows\\Fonts\\segoeuib.ttf",
};

template <size_t N> ImFont *load_font(const char *const (&paths)[N]) {
    ImGuiIO &io = ImGui::GetIO();
    for (const char *path : paths) {
        if (SDL_GetPathInfo(path, nullptr)) {
            ImFont *f = io.Fonts->AddFontFromFileTTF(path);
            if (f != nullptr) {
                return f;
            }
        }
    }
    return io.Fonts->AddFontDefaultVector();
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
    s_ui.fonts[0] = load_font(kRegular);
    s_ui.fonts[1] = load_font(kBold);
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
    return which < 2U ? s_ui.fonts[which] : nullptr;
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
