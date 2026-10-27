/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tela inicial (am/front.h), "estilo B": a janela inteira com o banner do disco do jogador por cima de um fundo
 * escuro, um menu grande navegável por teclado e controle (JOGAR, Configurações, Controles, Memory card, Sair) e um
 * rodapé com o backend, a GPU e a escala. Desenhada com o Dear ImGui pela plataforma (am/platform_ui.h). */
#include <cstdio>
#include <cstring>

#include "am/banner.h"
#include "am/front.h"
#include "am/gamedir.h"
#include "am/pad_input.h"
#include "am/platform.h"
#include "am/platform_gpu.h"
#include "am/platform_ui.h"

#include "imgui.h"

namespace {

enum class Screen { Main, Settings, Controls, Memcard, GameDir };

/* Mudança que só pode ser aplicada fora do quadro do ImGui (troca a forma de apresentação). */
enum class Pending { None, Backend };

struct Launcher {
    Settings *s;
    const LauncherInfo *info;
    Screen screen;
    bool done;
    LauncherResult result;
    Pending pending;
    Banner banner;
    bool have_banner;
    u32 art;
    char loaded_art_dir[SETTINGS_PATH_SIZE];
    GameDirResult dir_state;
    char dir_hash[SHA256_HEX_SIZE];
    char typed[SETTINGS_PATH_SIZE];
    bool dialog_folder;
    bool dialog_card;
    int capture; /* controle sendo remapeado; -1 = nenhum */
    bool focus_reset;
    char message[256];
};

Launcher s_l;

constexpr float kMenuSize = 2.0F;   /* fonte do menu, em múltiplos da base */
constexpr float kTitleSize = 2.6F;  /* título sem arte */
constexpr float kHeaderSize = 1.5F; /* título das telas internas */

ImFont *bold() {
    return static_cast<ImFont *>(platform_ui_font(1));
}

float base() {
    return ImGui::GetStyle().FontSizeBase;
}

bool back_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
           ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
}

int horizontal_pressed() {
    const bool left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) ||
                      ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft);
    const bool right = ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) ||
                       ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight);
    return (right ? 1 : 0) - (left ? 1 : 0);
}

void go(Screen screen) {
    s_l.screen = screen;
    s_l.focus_reset = true;
    s_l.message[0] = '\0';
}

/* ---- pasta do jogo e arte ---------------------------------------------------------------------------------------- */

void check_dir(const char *dir) {
    char root[SETTINGS_PATH_SIZE];
    s_l.dir_state = gamedir_check(dir, root, sizeof(root), s_l.dir_hash);
    if (s_l.dir_state == GAMEDIR_OK) {
        (void)std::snprintf(s_l.s->game_dir, sizeof(s_l.s->game_dir), "%s", root);
    }
}

void load_art() {
    if (s_l.dir_state != GAMEDIR_OK || std::strcmp(s_l.loaded_art_dir, s_l.s->game_dir) == 0) {
        return;
    }
    (void)std::snprintf(s_l.loaded_art_dir, sizeof(s_l.loaded_art_dir), "%s", s_l.s->game_dir);
    s_l.have_banner = banner_load(s_l.s->game_dir, &s_l.banner);
    s_l.art = s_l.have_banner ? platform_ui_texture_add(s_l.banner.rgba, BANNER_WIDTH, BANNER_HEIGHT) : 0U;
}

/* ---- fundo, arte e rodapé ------------------------------------------------------------------------------------------
 */

void draw_background() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const ImVec2 a = vp->Pos;
    const ImVec2 b(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    const ImU32 top = IM_COL32(18, 44, 96, 255);
    const ImU32 bottom = IM_COL32(4, 8, 18, 255);
    dl->AddRectFilledMultiColor(a, b, top, top, bottom, bottom);
    if (s_l.screen != Screen::Main) {
        return;
    }
    /* faixa vermelha e azul, como a do banner */
    const float y = a.y + vp->Size.y * 0.47F;
    dl->AddRectFilled(ImVec2(a.x, y), ImVec2(b.x, y + 3.0F), IM_COL32(200, 20, 30, 200));
    dl->AddRectFilled(ImVec2(a.x, y + 6.0F), ImVec2(b.x, y + 7.0F), IM_COL32(150, 190, 240, 120));
}

/* O banner (96x32) ampliado no alto, ou o nome do jogo em letras grandes sem a pasta do jogo. */
void draw_art(float max_h) {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const u64 id = s_l.art != 0U ? platform_ui_texture_id(s_l.art) : 0U;
    const float cx = vp->Pos.x + vp->Size.x * 0.5F;
    const float top = vp->Pos.y + vp->Size.y * 0.07F;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    if (id != 0U) {
        float w = vp->Size.x * 0.72F;
        float h = w / 3.0F;
        if (h > max_h) {
            h = max_h;
            w = h * 3.0F;
        }
        dl->AddImageRounded(ImTextureRef(static_cast<ImTextureID>(id)), ImVec2(cx - w * 0.5F, top),
                            ImVec2(cx + w * 0.5F, top + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, h * 0.06F);
        const char *sub = "Recompiled";
        ImFont *f = bold();
        const float fs = base() * 1.3F;
        const ImVec2 ts = f->CalcTextSizeA(fs, 1e9F, 0.0F, sub);
        dl->AddText(f, fs, ImVec2(cx + w * 0.5F - ts.x, top + h + 6.0F), IM_COL32(230, 236, 245, 230), sub);
        return;
    }
    const char *title = "AUTO MODELLISTA";
    const float fs = base() * kTitleSize;
    const ImVec2 ts = bold()->CalcTextSizeA(fs, 1e9F, 0.0F, title);
    dl->AddText(bold(), fs, ImVec2(cx - ts.x * 0.5F, top + max_h * 0.25F), IM_COL32(240, 244, 250, 255), title);
    const char *sub = "Recompiled";
    const float ss = base() * 1.3F;
    const ImVec2 ss2 = bold()->CalcTextSizeA(ss, 1e9F, 0.0F, sub);
    dl->AddText(bold(), ss, ImVec2(cx + ts.x * 0.5F - ss2.x, top + max_h * 0.25F + ts.y), IM_COL32(220, 40, 50, 255),
                sub);
}

const char *pretty_driver(const char *driver) {
    if (std::strcmp(driver, "metal") == 0) {
        return "Metal";
    }
    if (std::strcmp(driver, "vulkan") == 0) {
        return "Vulkan";
    }
    return std::strcmp(driver, "direct3d12") == 0 ? "D3D12" : driver;
}

void draw_footer() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    const float h = base() * 2.2F;
    const ImVec2 a(vp->Pos.x, vp->Pos.y + vp->Size.y - h);
    dl->AddRectFilled(a, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 150));
    char left[256];
    const char *driver = platform_gpu_driver();
    if (driver[0] != '\0') {
        (void)std::snprintf(left, sizeof(left), "%s · %s · escala %ux%s", pretty_driver(driver), platform_gpu_adapter(),
                            s_l.s->window_scale, s_l.s->fullscreen ? " (tela cheia)" : "");
    } else {
        (void)std::snprintf(left, sizeof(left), "software · janela %s · escala %ux%s", platform_window_presenter(),
                            s_l.s->window_scale, s_l.s->fullscreen ? " (tela cheia)" : "");
    }
    const float ty = a.y + (h - base()) * 0.5F;
    dl->AddText(ImVec2(a.x + 16.0F, ty), IM_COL32(200, 210, 225, 255), left);
    const char *hint = s_l.capture >= 0 ? "Esc Cancelar" : "A / Enter Confirmar  ·  B / Esc Voltar";
    const ImVec2 hs = ImGui::CalcTextSize(hint);
    dl->AddText(ImVec2(vp->Pos.x + vp->Size.x - hs.x - 16.0F, ty), IM_COL32(235, 240, 250, 255), hint);
}

/* ---- componentes ----------------------------------------------------------------------------------------------- */

/* Item grande do menu principal, centralizado. */
bool menu_item(const char *label, float width) {
    ImGui::PushFont(bold(), base() * kMenuSize);
    const float h = ImGui::GetFontSize() * 1.5F;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5F);
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5F, 0.5F));
    const bool hit = ImGui::Selectable(label, false, 0, ImVec2(width, h));
    ImGui::PopStyleVar();
    ImGui::PopFont();
    return hit;
}

/* Linha de opção: rótulo à esquerda, "< valor >" à direita. Esquerda/direita (setas, direcional) mudam o valor quando
 * a linha está em foco; A/Enter também (para a frente). Devolve -1, 0 ou +1. */
int option_row(const char *label, const char *value, float width) {
    ImGui::PushID(label);
    const float h = ImGui::GetFontSize() * 1.8F;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    int delta = ImGui::Selectable("##row", false, 0, ImVec2(width, h)) ? 1 : 0;
    if (ImGui::IsItemFocused()) {
        const int d = horizontal_pressed();
        delta = d != 0 ? d : delta;
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float ty = at.y + (h - ImGui::GetFontSize()) * 0.5F;
    dl->AddText(ImVec2(at.x + 14.0F, ty), IM_COL32(235, 240, 250, 255), label);
    char shown[300];
    (void)std::snprintf(shown, sizeof(shown), "<  %s  >", value);
    const ImVec2 vs = ImGui::CalcTextSize(shown);
    dl->AddText(ImVec2(at.x + width - vs.x - 14.0F, ty), IM_COL32(255, 210, 120, 255), shown);
    ImGui::PopID();
    return delta;
}

bool button_row(const char *label, float width) {
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5F);
    return ImGui::Selectable(label, false, 0, ImVec2(width, ImGui::GetFontSize() * 1.8F));
}

void header(const char *title, float width) {
    ImGui::PushFont(bold(), base() * kHeaderSize);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5F);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Spacing();
}

void message(float width) {
    if (s_l.message[0] != '\0') {
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5F);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.4F, 1.0F), "%s", s_l.message);
        ImGui::PopTextWrapPos();
    }
}

/* Janela sem borda ocupando a área do menu (abaixo da arte, acima do rodapé). */
bool begin_panel(const char *id, float top_frac, float width, bool panel_bg) {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const float footer = base() * 2.2F;
    const float top = vp->Size.y * top_frac;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x - width) * 0.5F, vp->Pos.y + top));
    ImGui::SetNextWindowSize(ImVec2(width, vp->Size.y - top - footer - 12.0F));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (!panel_bg) {
        flags |= ImGuiWindowFlags_NoBackground;
    }
    if (s_l.focus_reset) {
        ImGui::SetNextWindowFocus();
    }
    return ImGui::Begin(id, nullptr, flags);
}

void first_focus() {
    if (s_l.focus_reset) {
        ImGui::SetItemDefaultFocus();
        ImGui::SetKeyboardFocusHere(-1);
        ImGui::SetNavCursorVisible(true);
        s_l.focus_reset = false;
    }
}

/* ---- telas ------------------------------------------------------------------------------------------------------- */

void play() {
    check_dir(s_l.s->game_dir);
    if (s_l.dir_state != GAMEDIR_OK) {
        go(Screen::GameDir);
        (void)std::snprintf(s_l.message, sizeof(s_l.message), "Antes de jogar, escolha a pasta do jogo extraído.");
        return;
    }
    s_l.result = LAUNCHER_PLAY;
    s_l.done = true;
}

void screen_main() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    draw_art(vp->Size.y * 0.30F);
    const float width = vp->Size.x * 0.42F < 320.0F ? 320.0F : vp->Size.x * 0.42F;
    if (begin_panel("##main", 0.50F, width, false)) {
        const float item_w = width * 0.9F;
        if (menu_item("JOGAR", item_w)) {
            play();
        }
        first_focus();
        if (menu_item("Configurações", item_w)) {
            go(Screen::Settings);
        }
        if (menu_item("Controles", item_w)) {
            go(Screen::Controls);
        }
        if (menu_item("Memory card", item_w)) {
            go(Screen::Memcard);
        }
        if (menu_item("Sair", item_w)) {
            s_l.result = LAUNCHER_QUIT;
            s_l.done = true;
        }
    }
    ImGui::End();
}

const char *yes_no(bool v) {
    return v ? "Sim" : "Não";
}

void settings_rows(float w) {
    Settings *s = s_l.s;
    int d = option_row("Backend do GX", s->gpu ? "GPU" : "Software", w);
    first_focus();
    if (d != 0) {
        s->gpu = !s->gpu;
        s_l.pending = Pending::Backend;
    }
    char text[64];
    (void)std::snprintf(text, sizeof(text), "%ux", s->window_scale);
    d = option_row("Escala da janela", text, w);
    if (d != 0) {
        const int v = static_cast<int>(s->window_scale) + d;
        s->window_scale = static_cast<u32>(v < 1 ? static_cast<int>(SETTINGS_SCALE_MAX)
                                                 : (v > static_cast<int>(SETTINGS_SCALE_MAX) ? 1 : v));
        platform_window_set_scale(s->window_scale);
    }
    if (option_row("Tela cheia (F11)", yes_no(s->fullscreen), w) != 0) {
        s->fullscreen = !s->fullscreen;
        platform_window_set_fullscreen(s->fullscreen);
    }
    if (option_row("VSync", yes_no(s->vsync), w) != 0) {
        s->vsync = !s->vsync;
        platform_window_set_vsync(s->vsync);
    }
    static const char *const filters[] = {"Pixels (nearest)", "Suave (linear)", "Nítido (sharp)"};
    const int nf = static_cast<int>(PLATFORM_FILTER_COUNT);
    d = option_row("Filtro da imagem", filters[s->filter < static_cast<u32>(nf) ? s->filter : 0U], w);
    if (d != 0) {
        s->filter = static_cast<u32>((static_cast<int>(s->filter) + d + nf) % nf);
        platform_window_set_filter(s->filter);
    }
    if (option_row("Contador de FPS (F3)", yes_no(s->fps_overlay), w) != 0) {
        s->fps_overlay = !s->fps_overlay;
    }
    (void)std::snprintf(text, sizeof(text), "%u%%", s->volume);
    d = option_row("Volume (F9/F10)", text, w);
    if (d != 0) {
        const int v = static_cast<int>(s->volume) + d * 10;
        s->volume = static_cast<u32>(v < 0 ? 0 : (v > 100 ? 100 : v));
        platform_audio_set_volume(static_cast<float>(s->volume) / 100.0F);
    }
}

void screen_settings() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const float width = vp->Size.x * 0.6F < 420.0F ? 420.0F : vp->Size.x * 0.6F;
    draw_art(vp->Size.y * 0.16F);
    if (begin_panel("##settings", 0.28F, width, true)) {
        const float w = width - 40.0F;
        header("Configurações", w);
        settings_rows(w);
        ImGui::Spacing();
        char dir[SETTINGS_PATH_SIZE + 32];
        (void)std::snprintf(dir, sizeof(dir), "Pasta do jogo: %s",
                            s_l.s->game_dir[0] != '\0' ? s_l.s->game_dir : "(escolher)");
        if (button_row(dir, w)) {
            go(Screen::GameDir);
        }
        if (button_row("Voltar", w) || back_pressed()) {
            go(Screen::Main);
        }
        ImGui::TextDisabled(
            "  Arquivo: settings.ini no diretório de configurações; a linha de comando tem prioridade.");
    }
    ImGui::End();
}

void bind_text(u32 c, char *out, size_t size) {
    const Settings *s = s_l.s;
    char pad[160] = "";
    for (u32 b = 0; b < static_cast<u32>(PLATFORM_PAD_BUTTON_COUNT); b++) {
        if ((s->pad.pad[c] & (1U << b)) != 0U) {
            const size_t n = std::strlen(pad);
            (void)std::snprintf(pad + n, sizeof(pad) - n, "%s%s", n != 0U ? " + " : "",
                                pad_input_button_label(static_cast<PlatformPadButton>(b)));
        }
    }
    (void)std::snprintf(out, size, "%s  |  %s", s->keys[c][0] != '\0' ? s->keys[c] : "-", pad[0] != '\0' ? pad : "-");
}

void poll_capture() {
    if (s_l.capture < 0) {
        return;
    }
    char name[SETTINGS_KEY_SIZE];
    PlatformPadButton button = PLATFORM_PAD_SOUTH;
    const PlatformCapture c = platform_capture_poll(name, sizeof(name), &button);
    const u32 ctl = static_cast<u32>(s_l.capture);
    if (c == PLATFORM_CAPTURE_KEY) {
        (void)std::snprintf(s_l.s->keys[ctl], sizeof(s_l.s->keys[ctl]), "%s", name);
    } else if (c == PLATFORM_CAPTURE_BUTTON) {
        s_l.s->pad.pad[ctl] = 1U << static_cast<u32>(button);
    }
    if (c != PLATFORM_CAPTURE_WAITING) {
        s_l.capture = -1;
    }
}

void gamepad_row(float w) {
    Settings *s = s_l.s;
    const u32 n = platform_gamepad_count();
    int current = -1; /* -1 = automático */
    for (u32 i = 0; i < n; i++) {
        if (s->gamepad[0] != '\0' && std::strcmp(platform_gamepad_name(i), s->gamepad) == 0) {
            current = static_cast<int>(i);
        }
    }
    char value[160];
    if (s->gamepad[0] == '\0') {
        (void)std::snprintf(value, sizeof(value), "Automático%s%s", n != 0U ? ": " : " (nenhum conectado)",
                            n != 0U ? platform_gamepad_name(0) : "");
    } else {
        (void)std::snprintf(value, sizeof(value), "%s%s", s->gamepad, current < 0 ? " (desconectado)" : "");
    }
    const int d = option_row("Controle da porta 1", value, w);
    if (d != 0) {
        const int total = static_cast<int>(n) + 1;
        const int next = (((current + 1) + d) % total + total) % total - 1;
        (void)std::snprintf(s->gamepad, sizeof(s->gamepad), "%s",
                            next < 0 ? "" : platform_gamepad_name(static_cast<u32>(next)));
        platform_gamepad_select(s->gamepad);
    }
}

void screen_controls() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const float width = vp->Size.x * 0.8F < 560.0F ? 560.0F : vp->Size.x * 0.8F;
    poll_capture();
    if (begin_panel("##controls", 0.04F, width, true)) {
        const float w = width - 40.0F;
        header("Controles", w);
        gamepad_row(w);
        first_focus();
        ImGui::SetCursorPosX(20.0F);
        ImGui::TextDisabled("Escolha um controle e aperte a tecla ou o botão novo. Sticks analógicos: fixos.");
        for (u32 c = 0; c < static_cast<u32>(PAD_CONTROL_COUNT); c++) {
            char value[256];
            if (s_l.capture == static_cast<int>(c)) {
                (void)std::snprintf(value, sizeof(value), "aperte uma tecla ou botão...");
            } else {
                bind_text(c, value, sizeof(value));
            }
            if (option_row(pad_input_control_label(static_cast<PadControl>(c)), value, w) != 0 && s_l.capture < 0) {
                s_l.capture = static_cast<int>(c);
                platform_capture_start();
            }
        }
        if (button_row("Restaurar os padrões do Dolphin", w)) {
            for (u32 c = 0; c < static_cast<u32>(PAD_CONTROL_COUNT); c++) {
                (void)std::snprintf(s_l.s->keys[c], sizeof(s_l.s->keys[c]), "%s",
                                    pad_input_default_key(static_cast<PadControl>(c)));
            }
            pad_input_default_bindings(&s_l.s->pad);
        }
        if (button_row("Voltar", w) || (s_l.capture < 0 && back_pressed())) {
            go(Screen::Main);
        }
    }
    ImGui::End();
}

void screen_memcard() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const float width = vp->Size.x * 0.7F < 480.0F ? 480.0F : vp->Size.x * 0.7F;
    draw_art(vp->Size.y * 0.16F);
    char picked[SETTINGS_PATH_SIZE];
    if (s_l.dialog_card && platform_dialog_take(picked, sizeof(picked))) {
        s_l.dialog_card = false;
        if (picked[0] != '\0') {
            (void)std::snprintf(s_l.s->memcard, sizeof(s_l.s->memcard), "%s", picked);
        }
    }
    if (begin_panel("##memcard", 0.28F, width, true)) {
        const float w = width - 40.0F;
        header("Memory card (slot A)", w);
        const char *path = s_l.s->memcard[0] != '\0' ? s_l.s->memcard : s_l.info->default_card;
        ImGui::SetCursorPosX(20.0F);
        ImGui::PushTextWrapPos(width - 20.0F);
        ImGui::Text("Arquivo: %s%s", path, s_l.s->memcard[0] == '\0' ? " (padrão)" : "");
        ImGui::SetCursorPosX(20.0F);
        ImGui::TextDisabled(platform_path_exists(path)
                                ? "Imagem .raw do Dolphin (4 Mbit, 59 blocos); o jogo grava nela ao salvar."
                                : "Ainda não existe: será criado e formatado na primeira vez.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (button_row("Escolher outro arquivo...", w)) {
            s_l.dialog_card = true;
            platform_dialog_save_file(path, "Memory card do Dolphin (.raw)", "raw");
        }
        first_focus();
        if (button_row("Usar o padrão", w)) {
            s_l.s->memcard[0] = '\0';
        }
        if (button_row("Voltar", w) || back_pressed()) {
            go(Screen::Main);
        }
    }
    ImGui::End();
}

void screen_gamedir() {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const float width = vp->Size.x * 0.75F < 520.0F ? 520.0F : vp->Size.x * 0.75F;
    char picked[SETTINGS_PATH_SIZE];
    if (s_l.dialog_folder && platform_dialog_take(picked, sizeof(picked))) {
        s_l.dialog_folder = false;
        if (picked[0] != '\0') {
            (void)std::snprintf(s_l.typed, sizeof(s_l.typed), "%s", picked);
            check_dir(picked);
        }
    }
    draw_art(vp->Size.y * 0.16F);
    if (begin_panel("##gamedir", 0.28F, width, true)) {
        const float w = width - 40.0F;
        header("Pasta do jogo", w);
        ImGui::SetCursorPosX(20.0F);
        ImGui::PushTextWrapPos(width - 20.0F);
        ImGui::TextUnformatted("A pasta com o disco extraído (sys/ e files/), do \"dtk disc extract\" ou do "
                               "\"Extrair disco inteiro\" do Dolphin. O main.dol é conferido pelo SHA-256.");
        const bool ok = s_l.dir_state == GAMEDIR_OK;
        ImGui::SetCursorPosX(20.0F);
        ImGui::TextColored(ok ? ImVec4(0.5F, 0.95F, 0.5F, 1.0F) : ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "%s: %s",
                           ok ? s_l.s->game_dir : (s_l.typed[0] != '\0' ? s_l.typed : "nenhuma"),
                           gamedir_result_text(s_l.dir_state));
        if (s_l.dir_state == GAMEDIR_BAD_HASH) {
            ImGui::SetCursorPosX(20.0F);
            ImGui::TextDisabled("SHA-256: %s", s_l.dir_hash);
        }
        ImGui::PopTextWrapPos();
        message(w);
        ImGui::Spacing();
        if (button_row("Escolher pasta...", w)) {
            s_l.dialog_folder = true;
            platform_dialog_pick_folder(s_l.s->game_dir[0] != '\0' ? s_l.s->game_dir : nullptr);
        }
        first_focus();
        ImGui::SetCursorPosX(20.0F);
        ImGui::SetNextItemWidth(w - 140.0F);
        const bool enter = ImGui::InputTextWithHint("##typed", "ou digite o caminho aqui", s_l.typed, sizeof(s_l.typed),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Conferir") || enter) {
            check_dir(s_l.typed);
        }
        if (ok && button_row("JOGAR", w)) {
            play();
        }
        if (button_row("Voltar", w) || (!ImGui::IsAnyItemActive() && back_pressed())) {
            go(Screen::Main);
        }
    }
    ImGui::End();
}

void draw(void *user) {
    (void)user;
    const u32 hot = platform_hotkeys_take();
    if ((hot & PLATFORM_HOTKEY_OVERLAY) != 0U) {
        s_l.s->fps_overlay = !s_l.s->fps_overlay;
    }
    s_l.s->fullscreen = platform_window_fullscreen();
    load_art();
    draw_background();
    switch (s_l.screen) {
    case Screen::Main:
        screen_main(); /* B no menu principal não sai: sair é pelo item Sair */
        break;
    case Screen::Settings:
        screen_settings();
        break;
    case Screen::Controls:
        screen_controls();
        break;
    case Screen::Memcard:
        screen_memcard();
        break;
    case Screen::GameDir:
        screen_gamedir();
        break;
    }
    draw_footer();
}

void apply_pending() {
    if (s_l.pending == Pending::Backend) {
        if (s_l.s->gpu) {
            if (!platform_gpu_init(static_cast<PlatformGpuShader>(s_l.info->gpu_shader))) {
                (void)std::snprintf(s_l.message, sizeof(s_l.message), "GPU indisponível (%s): fica o software.",
                                    platform_gpu_error());
                s_l.s->gpu = false;
            }
        } else {
            platform_gpu_shutdown();
        }
    }
    s_l.pending = Pending::None;
}

} // namespace

extern "C" LauncherResult launcher_run(Settings *s, const LauncherInfo *info) {
    s_l = Launcher{};
    s_l.s = s;
    s_l.info = info;
    s_l.capture = -1;
    s_l.focus_reset = true;
    check_dir(s->game_dir);
    s_l.screen = s_l.dir_state == GAMEDIR_OK ? Screen::Main : Screen::GameDir;
    if (s_l.dir_state != GAMEDIR_OK && s->game_dir[0] != '\0') {
        (void)std::snprintf(s_l.typed, sizeof(s_l.typed), "%s", s->game_dir);
    }
    platform_ui_set_draw(draw, nullptr);
    while (!s_l.done) {
        const PlatformWindowResult r = platform_window_present(nullptr, 0, 0, nullptr);
        if (r != PLATFORM_WINDOW_OK) {
            s_l.result = r == PLATFORM_WINDOW_CLOSED ? LAUNCHER_QUIT : LAUNCHER_ERROR;
            break;
        }
        apply_pending();
        platform_sleep_ns(8000000ULL); /* a tela inicial não precisa de mais que ~120 quadros/s */
    }
    platform_ui_set_draw(nullptr, nullptr);
    return s_l.result;
}
