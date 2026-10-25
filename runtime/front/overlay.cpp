/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Contador de quadros sobre o jogo (am/front.h). Desenhado pela interface da plataforma depois da imagem do jogo:
 * nunca entra no XFB nem nas capturas. */
#include <cstdio>
#include <cstring>

#include "am/front.h"
#include "am/platform.h"
#include "am/platform_gpu.h"
#include "am/platform_ui.h"

#include "imgui.h"

namespace {

constexpr u64 kWindowNs = 500000000ULL; /* média a cada meio segundo */
constexpr u64 kToastNs = 1500000000ULL; /* aviso de volume */
constexpr u32 kVolumeStep = 10;

struct Overlay {
    Settings *s;
    u64 t0;
    u32 frames;
    u32 drawn;
    double game_fps;
    double drawn_fps;
    double ms;
    u64 toast_until;
    char backend[160];
};

Overlay s_ov;

const char *pretty(const char *driver) {
    if (std::strcmp(driver, "metal") == 0) {
        return "Metal";
    }
    if (std::strcmp(driver, "vulkan") == 0) {
        return "Vulkan";
    }
    return std::strcmp(driver, "direct3d12") == 0 ? "D3D12" : driver;
}

void describe_backend() {
    const char *driver = platform_gpu_driver();
    if (driver[0] != '\0') {
        (void)std::snprintf(s_ov.backend, sizeof(s_ov.backend), "GX: %s · %s", pretty(driver), platform_gpu_adapter());
    } else {
        (void)std::snprintf(s_ov.backend, sizeof(s_ov.backend), "GX: software · janela %s",
                            platform_window_presenter());
    }
}

void hotkeys() {
    const u32 h = platform_hotkeys_take();
    Settings *s = s_ov.s;
    if ((h & PLATFORM_HOTKEY_OVERLAY) != 0U) {
        s->fps_overlay = !s->fps_overlay;
    }
    if ((h & (PLATFORM_HOTKEY_VOLUME_DOWN | PLATFORM_HOTKEY_VOLUME_UP)) != 0U) {
        if ((h & PLATFORM_HOTKEY_VOLUME_DOWN) != 0U) {
            s->volume = s->volume > kVolumeStep ? s->volume - kVolumeStep : 0U;
        }
        if ((h & PLATFORM_HOTKEY_VOLUME_UP) != 0U) {
            s->volume = s->volume + kVolumeStep < 100U ? s->volume + kVolumeStep : 100U;
        }
        platform_audio_set_volume(static_cast<float>(s->volume) / 100.0F);
        s_ov.toast_until = platform_ticks_ns() + kToastNs;
    }
    s->fullscreen = platform_window_fullscreen();
}

void draw_stats() {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    const float pad = 10.0F;
    ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55F);
    if (ImGui::Begin("##fps", nullptr, flags)) {
        ImGui::PushFont(static_cast<ImFont *>(platform_ui_font(1)), 0.0F);
        ImGui::Text("%.1f quadros/s", s_ov.game_fps);
        ImGui::PopFont();
        ImGui::Text("%.1f desenhados/s · %.1f ms por quadro", s_ov.drawn_fps, s_ov.ms);
        ImGui::TextUnformatted(s_ov.backend);
        ImGui::TextDisabled("F3 esconde");
    }
    ImGui::End();
}

void draw_toast() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5F, vp->Pos.y + vp->Size.y - 40.0F), ImGuiCond_Always,
                            ImVec2(0.5F, 1.0F));
    ImGui::SetNextWindowBgAlpha(0.7F);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##volume", nullptr, flags)) {
        ImGui::PushFont(static_cast<ImFont *>(platform_ui_font(1)), ImGui::GetStyle().FontSizeBase * 1.4F);
        ImGui::Text("Volume %u%%", s_ov.s->volume);
        ImGui::PopFont();
    }
    ImGui::End();
}

void draw(void *user) {
    (void)user;
    hotkeys();
    if (s_ov.s->fps_overlay) {
        draw_stats();
    }
    if (platform_ticks_ns() < s_ov.toast_until) {
        draw_toast();
    }
}

} // namespace

extern "C" {

void overlay_start(Settings *s) {
    s_ov = Overlay{};
    s_ov.s = s;
    s_ov.t0 = platform_ticks_ns();
    describe_backend();
    platform_ui_set_draw(draw, nullptr);
}

void overlay_frame(bool drawn) {
    if (s_ov.s == nullptr) {
        return;
    }
    s_ov.frames++;
    s_ov.drawn += drawn ? 1U : 0U;
    const u64 now = platform_ticks_ns();
    const u64 dt = now - s_ov.t0;
    if (dt >= kWindowNs) {
        const double secs = static_cast<double>(dt) / 1e9;
        s_ov.game_fps = s_ov.frames / secs;
        s_ov.drawn_fps = s_ov.drawn / secs;
        s_ov.ms = secs * 1000.0 / s_ov.frames;
        s_ov.frames = 0;
        s_ov.drawn = 0;
        s_ov.t0 = now;
        describe_backend();
    }
}

void overlay_stop(void) {
    platform_ui_set_draw(nullptr, nullptr);
    s_ov = Overlay{};
}

} /* extern "C" */
