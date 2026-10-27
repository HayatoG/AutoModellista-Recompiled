/* SPDX-License-Identifier: GPL-3.0-or-later */
/* As páginas de configuração da tela inicial (Gráficos, Áudio, Controles, Interface, Suporte, Sobre). Só ficam ativas
 * as opções que o port realmente implementa; as do design que ainda não existem aparecem apagadas, sem interação e com
 * a etiqueta "EM BREVE" (nada delas é gravado no settings.ini). */
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "am/pad_input.h"
#include "am/platform.h"
#include "am/platform_gpu.h"

#include "launcher_state.hpp"

#ifndef AM_VERSION
#define AM_VERSION "?"
#endif

namespace launcher {

namespace {

using namespace lui;

constexpr const char *kRepoUrl = "https://github.com/HayatoG/AutoModellista-Recompiled";
constexpr const char *kIssuesUrl = "https://github.com/HayatoG/AutoModellista-Recompiled/issues";

/* ---- linhas prontas ------------------------------------------------------------------------------------------------
 */

int seg_row(Form &f, const char *id, const char *title, const char *desc, const char *const *labels, int n, int sel,
            unsigned mask, bool soon, bool dim) {
    const Row r = row_begin(f, title, desc, seg_width(labels, n), px(44.0F), soon, dim);
    const int pick = seg(f, r, id, labels, n, sel, mask);
    row_end(f, r);
    return pick;
}

bool switch_row(Form &f, const char *id, const char *title, const char *desc, bool on, bool enabled, bool soon) {
    const Row r = row_begin(f, title, desc, px(46.0F), px(26.0F), soon, !enabled);
    const bool hit_it = toggle(f, r, id, on, enabled);
    row_end(f, r);
    return hit_it;
}

int slider_row(Form &f, const char *id, const char *title, const char *desc, int value, int lo, int hi,
               const char *suffix, bool enabled, bool soon) {
    const Row r = row_begin(f, title, desc, px(220.0F + 12.0F + 44.0F), px(24.0F), soon, !enabled);
    const int v = slider(f, r, id, value, lo, hi, suffix, enabled);
    row_end(f, r);
    return v;
}

bool button_row(Form &f, const char *id, const char *title, const char *desc, const char *label, Icon ic, bool enabled,
                bool soon) {
    const Row r = row_begin(f, title, desc, ghost_width(label, ic, 0.0F), px(44.0F), soon, !enabled);
    const bool hit_it = row_ghost(f, r, id, label, ic, enabled, 0.0F);
    row_end(f, r);
    return hit_it;
}

const char *const kFilterNames[] = {"Pixels", "Suave", "Nítido"};

/* ---- Gráficos ------------------------------------------------------------------------------------------------------
 */

void page_graphics(Form &f) {
    Settings *s = g.s;
    char desc[400];
    char label[256];
    (void)driver_label(label, sizeof(label));
    (void)std::snprintf(desc, sizeof(desc),
                        "Em uso: %s. O driver da GPU é escolhido pela SDL; Vulkan e OpenGL não são opções.", label);
    static const char *const backend[] = {"GPU", "Software"};
    int pick = seg_row(f, "backend", "Backend gráfico", desc, backend, 2, s->gpu ? 0 : 1, 3U, false, false);
    if (pick >= 0 && (pick == 0) != s->gpu) {
        s->gpu = pick == 0;
        g.pending = Pending::Backend;
    }
    static const char *const res[] = {"Nativa", "2×", "3×", "4×"};
    (void)seg_row(f, "res", "Resolução interna",
                  "Multiplica a resolução original do jogo (640×448). Só a nativa existe por enquanto.", res, 4, 0, 1U,
                  true, false);
    static const char *const scale[] = {"1×", "2×", "3×", "4×", "5×", "6×"};
    pick = seg_row(f, "scale", "Escala da janela",
                   s->fullscreen ? "Tela cheia ativa: a escala não tem efeito agora, só vale em janela."
                                 : "Tamanho da janela em múltiplos de 640×448.",
                   scale, 6, static_cast<int>(s->window_scale) - 1, 0x3FU, false, s->fullscreen);
    if (pick >= 0) {
        s->window_scale = static_cast<u32>(pick) + 1U;
        platform_window_set_scale(s->window_scale);
    }
    static const char *const mode[] = {"Janela", "Tela cheia"};
    pick = seg_row(f, "mode", "Modo de exibição",
                   "Tela cheia usa a área de trabalho, sem trocar o modo de vídeo (F11 ou Alt+Enter).", mode, 2,
                   s->fullscreen ? 1 : 0, 3U, false, false);
    if (pick >= 0 && (pick == 1) != s->fullscreen) {
        s->fullscreen = pick == 1;
        platform_window_set_fullscreen(s->fullscreen);
    }
    static const char *const aspect[] = {"4:3", "16:9", "21:9"};
    (void)seg_row(f, "aspect", "Proporção", "O jogo roda em 4:3; as outras proporções ficam para depois.", aspect, 3, 0,
                  1U, true, false);
    pick = seg_row(f, "filter", "Filtro da imagem",
                   "Como a janela estica a imagem do jogo. Só a janela muda, não as capturas.", kFilterNames, 3,
                   static_cast<int>(s->filter < 3U ? s->filter : 0U), 7U, false, false);
    if (pick >= 0) {
        s->filter = static_cast<u32>(pick);
        platform_window_set_filter(s->filter);
    }
    static const char *const fps[] = {"30", "60", "120", "Livre"};
    (void)seg_row(f, "fps", "Limite de FPS", "O jogo corre a 60 quadros/s, no ritmo do vídeo original.", fps, 4, -1, 0U,
                  true, true);
    static const char *const aa[] = {"Off", "2×", "4×", "8×"};
    (void)seg_row(f, "aa", "Anti-aliasing", "Hoje o jogo é desenhado sem suavização de bordas.", aa, 4, 0, 1U, true,
                  false);
    if (switch_row(f, "vsync", "VSync", "Evita tearing; pode adicionar um pouco de latência.", s->vsync, true, false)) {
        s->vsync = !s->vsync;
        platform_window_set_vsync(s->vsync);
    }
    (void)switch_row(f, "outlines", "Contornos cel-shading",
                     "Os contornos pretos já são desenhados; ligar e desligar fica para depois.", true, false, true);
    if (switch_row(f, "fps_overlay", "Contador de FPS", "Mostra os quadros por segundo sobre o jogo (F3).",
                   s->fps_overlay, true, false)) {
        s->fps_overlay = !s->fps_overlay;
    }
}

/* ---- Áudio ---------------------------------------------------------------------------------------------------------
 */

void page_audio(Form &f) {
    Settings *s = g.s;
    const int v =
        slider_row(f, "vol", "Volume geral", "Só soa com uma saída de som disponível. F9 e F10 também ajustam.",
                   static_cast<int>(s->volume), 0, 100, "%", true, false);
    if (v != static_cast<int>(s->volume)) {
        s->volume = static_cast<u32>(v);
        platform_audio_set_volume(static_cast<float>(s->volume) / 100.0F);
    }
    (void)slider_row(f, "music", "Música", "O jogo mistura tudo antes da saída: sem volumes separados por enquanto.",
                     100, 0, 100, "%", false, true);
    (void)slider_row(f, "sfx", "Efeitos sonoros", "Motores, pneus e colisões.", 100, 0, 100, "%", false, true);
    (void)slider_row(f, "voice", "Interface e vozes", nullptr, 100, 0, 100, "%", false, true);
    static const char *const out[] = {"Estéreo", "Surround"};
    (void)seg_row(f, "output", "Saída", "Hoje a saída é estéreo.", out, 2, 0, 1U, true, false);
    (void)button_row(f, "device", "Dispositivo", "Padrão do sistema", "Alterar", Icon::Chevron, false, true);
    (void)switch_row(f, "bgmute", "Silenciar em segundo plano", "Muta o jogo quando a janela perde o foco.", false,
                     false, true);
}

/* ---- Controles -----------------------------------------------------------------------------------------------------
 */

void bind_text(u32 c, char *out, size_t size) {
    const Settings *s = g.s;
    char pad[160] = "";
    for (u32 b = 0; b < static_cast<u32>(PLATFORM_PAD_BUTTON_COUNT); b++) {
        if ((s->pad.pad[c] & (1U << b)) != 0U) {
            const size_t n = std::strlen(pad);
            (void)std::snprintf(pad + n, sizeof(pad) - n, "%s%s", n != 0U ? " + " : "",
                                pad_input_button_label(static_cast<PlatformPadButton>(b)));
        }
    }
    (void)std::snprintf(out, size, "%s | %s", s->keys[c][0] != '\0' ? s->keys[c] : "-", pad[0] != '\0' ? pad : "-");
}

void poll_capture() {
    if (g.capture < 0) {
        return;
    }
    char name[SETTINGS_KEY_SIZE];
    PlatformPadButton button = PLATFORM_PAD_SOUTH;
    const PlatformCapture c = platform_capture_poll(name, sizeof(name), &button);
    const u32 ctl = static_cast<u32>(g.capture);
    if (c == PLATFORM_CAPTURE_KEY) {
        (void)std::snprintf(g.s->keys[ctl], sizeof(g.s->keys[ctl]), "%s", name);
    } else if (c == PLATFORM_CAPTURE_BUTTON) {
        g.s->pad.pad[ctl] = 1U << static_cast<u32>(button);
    }
    if (c != PLATFORM_CAPTURE_WAITING) {
        g.capture = -1;
    }
}

void gamepad_row(Form &f) {
    Settings *s = g.s;
    const u32 n = platform_gamepad_count();
    int current = -1; /* -1 = automático */
    for (u32 i = 0; i < n; i++) {
        if (s->gamepad[0] != '\0' && std::strcmp(platform_gamepad_name(i), s->gamepad) == 0) {
            current = static_cast<int>(i);
        }
    }
    char value[200];
    if (s->gamepad[0] == '\0') {
        (void)std::snprintf(value, sizeof(value), "%s%s",
                            n != 0U ? "Automático: " : "Automático (nenhum controle conectado)",
                            n != 0U ? platform_gamepad_name(0) : "");
    } else {
        (void)std::snprintf(value, sizeof(value), "%s%s", s->gamepad, current < 0 ? " (desconectado)" : "");
    }
    if (button_row(f, "pad", "Jogador 1", value, "Trocar controle", Icon::Gamepad, true, false)) {
        const int total = static_cast<int>(n) + 1;
        const int next = (((current + 1) + 1) % total + total) % total - 1;
        (void)std::snprintf(s->gamepad, sizeof(s->gamepad), "%s",
                            next < 0 ? "" : platform_gamepad_name(static_cast<u32>(next)));
        platform_gamepad_select(s->gamepad);
    }
}

void memcard_rows(Form &f) {
    Settings *s = g.s;
    const bool none = std::strcmp(s->memcard, "none") == 0;
    const char *path = s->memcard[0] != '\0' ? s->memcard : g.info->default_card;
    char desc[SETTINGS_PATH_SIZE + 80];
    if (none) {
        (void)std::snprintf(desc, sizeof(desc), "Sem cartão: o jogo roda sem salvar nada.");
    } else {
        (void)std::snprintf(desc, sizeof(desc), "%s%s %s", path, s->memcard[0] == '\0' ? " (padrão)" : "",
                            platform_path_exists(path) ? "" : "· será criado na primeira vez");
    }
    if (button_row(f, "card", "Memory card (slot A)", desc, "Escolher arquivo", Icon::Folder, !none, false)) {
        g.dialog_card = true;
        platform_dialog_save_file(path, "Memory card do Dolphin (.raw)", "raw");
    }
    if (s->memcard[0] != '\0' && !none &&
        button_row(f, "card_default", "Caminho padrão", "Volta ao MemoryCardA.raw da pasta de dados.", "Usar o padrão",
                   Icon::Refresh, true, false)) {
        s->memcard[0] = '\0';
    }
    if (switch_row(f, "card_none", "Sem cartão", "Joga sem memory card (equivale a --memcard none).", none, true,
                   false)) {
        (void)std::snprintf(s->memcard, sizeof(s->memcard), "%s", none ? "" : "none");
    }
}

void mapping_rows(Form &f) {
    ImDrawList *dl = f.dl;
    for (u32 c = 0; c < static_cast<u32>(PAD_CONTROL_COUNT); c++) {
        const float h = px(60.0F);
        const float cy = f.y + h * 0.5F;
        if (c != 0U) {
            dl->AddLine(ImVec2(f.x, f.y), ImVec2(f.x + f.w, f.y), white(0.09F), 1.0F);
        }
        const bool waiting = g.capture == static_cast<int>(c);
        char value[256];
        if (waiting) {
            (void)std::snprintf(value, sizeof(value), "Aperte uma tecla ou botão (Esc cancela)");
        } else {
            bind_text(c, value, sizeof(value));
        }
        char id[24];
        (void)std::snprintf(id, sizeof(id), "map%u", c);
        const char *label = waiting ? "Aguardando" : "Alterar";
        const float bw = ghost_width(label, Icon::None, 96.0F);
        const bool pressed = ghost(id, ImVec2(f.x + f.w - bw, cy - px(18.0F)), label, Icon::None,
                                   !waiting && g.capture < 0, 96.0F, nullptr, 36.0F);
        const float vx = f.x + f.w - bw - px(16.0F);
        const float vw = text_w(Face::Mono, 13.0F, value);
        text_cy(dl, Face::Semi, 15.0F, f.x, cy, white(1.0F), pad_input_control_label(static_cast<PadControl>(c)));
        text_cy(dl, Face::Mono, 13.0F, vx - vw, cy, waiting ? fr.accent : white(0.85F), value);
        if (pressed) {
            g.capture = static_cast<int>(c);
            platform_capture_start();
        }
        f.y += h;
    }
    f.first = false;
}

void page_controls(Form &f) {
    poll_capture();
    gamepad_row(f);
    static const char *const glyphs[] = {"Xbox", "PlayStation", "GameCube"};
    (void)seg_row(f, "glyphs", "Ícones dos botões", "Glifos exibidos nos menus do jogo.", glyphs, 3, -1, 0U, true,
                  true);
    (void)slider_row(f, "deadzone", "Zona morta do analógico", "Aumente se o carro puxar para um lado sozinho.", 12, 0,
                     40, "%", false, true);
    (void)switch_row(f, "rumble", "Vibração", nullptr, false, false, true);
    (void)switch_row(f, "analog_triggers", "Gatilhos analógicos",
                     "Hoje L e R são analógicos só quando ligados a um gatilho do controle.", false, false, true);
    memcard_rows(f);
    /* subtítulo da seção */
    f.y += px(20.0F);
    text(f.dl, Face::Bold, 12.0F, ImVec2(f.x, f.y), white(0.70F), "TECLAS E BOTÕES", 0.1F);
    f.y += line_h(Face::Bold, 12.0F) + px(4.0F);
    f.dl->AddLine(ImVec2(f.x, f.y), ImVec2(f.x + f.w, f.y), white(0.09F), 1.0F);
    mapping_rows(f);
    f.y += px(8.0F);
    if (button_row(f, "restore", "Restaurar os padrões do Dolphin", nullptr, "Restaurar", Icon::Refresh, g.capture < 0,
                   false)) {
        for (u32 c = 0; c < static_cast<u32>(PAD_CONTROL_COUNT); c++) {
            (void)std::snprintf(g.s->keys[c], sizeof(g.s->keys[c]), "%s",
                                pad_input_default_key(static_cast<PadControl>(c)));
        }
        pad_input_default_bindings(&g.s->pad);
    }
}

/* ---- Interface -----------------------------------------------------------------------------------------------------
 */

void page_interface(Form &f) {
    Settings *s = g.s;
    static const char *const style[] = {"Liquid Glass", "Sólido"};
    const int pick =
        seg_row(f, "style", "Estilo visual", "Liquid Glass deixa o cenário aparecer, desfocado, através dos painéis.",
                style, 2, s->ui_glass ? 0 : 1, 3U, false, false);
    if (pick >= 0) {
        s->ui_glass = pick == 0;
    }
    const int blur = slider_row(f, "blur", "Intensidade do vidro",
                                s->ui_glass ? "Quantidade de desfoque nos painéis." : "Só vale no estilo Liquid Glass.",
                                static_cast<int>(s->ui_blur), static_cast<int>(SETTINGS_BLUR_MIN),
                                static_cast<int>(SETTINGS_BLUR_MAX), "px", s->ui_glass, false);
    s->ui_blur = static_cast<u32>(blur);
    static const char *const lang[] = {"Português", "Inglês", "Japonês"};
    (void)seg_row(f, "lang", "Idioma", nullptr, lang, 3, 0, 1U, true, false);
    (void)switch_row(f, "close_on_play", "Fechar o launcher ao jogar",
                     "Hoje o jogo roda na mesma janela; não há como voltar ao launcher sem fechar.", false, false,
                     true);
}

/* ---- Suporte -------------------------------------------------------------------------------------------------------
 */

struct CardSpec {
    Icon ic;
    const char *title;
    const char *desc;
    bool enabled;
};

bool card(const char *id, ImDrawList *dl, ImVec2 a, ImVec2 b, const CardSpec &c) {
    Hit h;
    if (c.enabled) {
        h = hit(id, a, b);
    }
    const float dim = c.enabled ? 1.0F : 0.5F;
    dl->AddRectFilled(a, b, white((h.hovered ? 0.13F : 0.07F) * dim), px(16.0F));
    rrect_stroke(dl, a, b, 16.0F, white(0.14F * dim));
    const float pad = px(20.0F);
    icon(dl, c.ic, ImVec2(a.x + pad + px(12.0F), a.y + pad + px(12.0F)), 24.0F, white(dim), 1.8F);
    float y = a.y + pad + px(24.0F) + px(10.0F);
    text(dl, Face::Semi, 17.0F, ImVec2(a.x + pad, y), white(dim), c.title);
    if (!c.enabled) {
        (void)soon_badge(dl, ImVec2(a.x + pad + text_w(Face::Semi, 17.0F, c.title) + px(10.0F), y + px(5.0F)));
    }
    y += line_h(Face::Semi, 17.0F) + px(10.0F);
    (void)paragraph(dl, Face::Body, 13.0F, 13.0F * 1.574F, ImVec2(a.x + pad, y), b.x - a.x - 2.0F * pad,
                    white(0.72F * dim), c.desc);
    if (h.focused) {
        focus_ring(dl, a, b, 16.0F);
    }
    return h.pressed;
}

float card_height(const CardSpec &c, float w) {
    const float pad = px(20.0F);
    return 2.0F * pad + px(24.0F) + px(10.0F) + line_h(Face::Semi, 17.0F) + px(10.0F) +
           paragraph(nullptr, Face::Body, 13.0F, 13.0F * 1.574F, ImVec2(0, 0), w - 2.0F * pad, 0, c.desc);
}

void page_support(Form &f) {
    ImDrawList *dl = f.dl;
    char data_dir[SETTINGS_PATH_SIZE] = "";
    const bool have_dir = platform_config_dir(data_dir, sizeof(data_dir));
    char data_desc[SETTINGS_PATH_SIZE + 64];
    (void)std::snprintf(data_desc, sizeof(data_desc),
                        "settings.ini e o memory card ficam aqui; o log vai para o terminal.");
    const CardSpec cards[4] = {
        {Icon::Bug, "Relatar um problema", "Abre as issues do projeto no GitHub; cole as informações do sistema.",
         true},
        {Icon::Link, "Projeto no GitHub", "Código, novidades e versões do port.", true},
        {Icon::Folder, "Abrir pasta de dados", data_desc, have_dir},
        {Icon::Help, "Perguntas frequentes", "Desempenho, controles e formatos de imagem.", false},
    };
    const float gap = px(14.0F);
    const float cw = (f.w - gap) * 0.5F;
    f.y += px(8.0F);
    for (int row = 0; row < 2; row++) {
        const float h = std::max(card_height(cards[row * 2], cw), card_height(cards[row * 2 + 1], cw));
        for (int col = 0; col < 2; col++) {
            const int i = row * 2 + col;
            const ImVec2 a(f.x + static_cast<float>(col) * (cw + gap), f.y);
            char id[16];
            (void)std::snprintf(id, sizeof(id), "card%d", i);
            if (card(id, dl, a, ImVec2(a.x + cw, a.y + h), cards[i])) {
                if (i == 0) {
                    (void)platform_open_url(kIssuesUrl);
                } else if (i == 1) {
                    (void)platform_open_url(kRepoUrl);
                } else if (i == 2) {
                    (void)platform_open_folder(data_dir);
                }
            }
        }
        f.y += h + gap;
    }
    /* informações do sistema */
    char info[1024];
    system_info(info, sizeof(info));
    const float pad_x = px(20.0F);
    const float pad_y = px(18.0F);
    const float lh = px(13.0F * 1.7F);
    int lines = 1;
    for (const char *p = info; *p != '\0'; p++) {
        lines += *p == '\n' ? 1 : 0;
    }
    const float head = line_h(Face::Bold, 12.0F) + px(6.0F);
    const float h = std::max(2.0F * pad_y + head + lh * static_cast<float>(lines), px(44.0F) + 2.0F * pad_y);
    const ImVec2 a(f.x, f.y);
    const ImVec2 b(f.x + f.w, f.y + h);
    dl->AddRectFilled(a, b, black(0.30F), px(16.0F));
    rrect_stroke(dl, a, b, 16.0F, white(0.12F));
    text(dl, Face::Bold, 12.0F, ImVec2(a.x + pad_x, a.y + pad_y), white(0.70F), "INFORMAÇÕES DO SISTEMA", 0.1F);
    float y = a.y + pad_y + head;
    for (const char *p = info; *p != '\0';) {
        const char *e = std::strchr(p, '\n');
        const size_t n = e != nullptr ? static_cast<size_t>(e - p) : std::strlen(p);
        char line[256];
        (void)std::snprintf(line, sizeof(line), "%.*s", static_cast<int>(n), p);
        text(dl, Face::Mono, 13.0F, ImVec2(a.x + pad_x, y + (lh - line_h(Face::Mono, 13.0F)) * 0.5F), white(0.88F),
             line);
        y += lh;
        p += n + (e != nullptr ? 1U : 0U);
    }
    const bool copied = platform_ticks_ns() < g.copied_until_ns;
    const char *label = copied ? "Copiado" : "Copiar";
    const float bw = ghost_width(label, Icon::Copy, 0.0F);
    if (ghost("copy", ImVec2(b.x - pad_x - bw, a.y + pad_y), label, copied ? Icon::Check : Icon::Copy, true, 0.0F,
              nullptr)) {
        if (platform_clipboard_set(info)) {
            g.copied_until_ns = platform_ticks_ns() + 1600000000ULL;
        }
    }
    f.y += h;
}

} // namespace

const char *driver_label(char *out, size_t size) {
    const char *driver = platform_gpu_driver();
    if (driver[0] != '\0') {
        (void)std::snprintf(out, size, "%s · %s", driver, platform_gpu_adapter());
    } else {
        (void)std::snprintf(out, size, "software · janela %s", platform_window_presenter());
    }
    return out;
}

void system_info(char *out, size_t size) {
    char gpu[256];
    const int sdl = platform_sdl_version();
    (void)std::snprintf(
        out, size, "Auto Modellista Recompiled v%s\nSO: %s\nGráficos: %s\nSDL %d.%d.%d · filtro %s · escala %u×",
        AM_VERSION, platform_os_name(), driver_label(gpu, sizeof(gpu)), sdl / 1000000, (sdl / 1000) % 1000, sdl % 1000,
        kFilterNames[g.s->filter < 3U ? g.s->filter : 0U], g.s->window_scale);
}

/* ---- Sobre ---------------------------------------------------------------------------------------------------------
 */

namespace {

void page_about(ImDrawList *dl, ImVec2 a, ImVec2 b) {
    const float pad = px(40.0F);
    const float x = a.x + pad;
    float y = a.y + pad;
    /* AUTO MODELLISTA, 72 px, inclinado, uma linha só */
    const float size = 72.0F;
    const float box_h = size * 0.9F;
    const float baseline = y + px((box_h - size * 1.574F) * 0.5F) + ascent(Face::Display, size) + px(0.0F);
    const SkewLayer layers[3] = {
        {ImVec2(6, 6), fr.accent}, {ImVec2(3, 3), IM_COL32(6, 4, 12, 255)}, {ImVec2(0, 0), IM_COL32_WHITE}};
    text_skewed(dl, Face::Display, size, ImVec2(x, baseline), y + px(box_h), 9.0F * -1.0F, "AUTO MODELLISTA", layers,
                3);
    y += px(box_h) + px(16.0F + 8.0F);
    y +=
        paragraph(dl, Face::Body, 16.0F, 16.0F * 1.55F, ImVec2(x, y), std::min(px(560.0F), b.x - x - pad), white(0.85F),
                  "Port nativo para PC baseado na versão de GameCube. Feito por fãs, sem fins lucrativos.");
    y += px(16.0F);
    char ver[32];
    (void)std::snprintf(ver, sizeof(ver), "v%s", AM_VERSION);
    const float w1 = chip(dl, ImVec2(x, y), ver, Icon::None, 0);
    (void)chip(dl, ImVec2(x + w1 + px(8.0F), y), "Port por HayatoG", Icon::None, 0);
    const char *legal =
        "Este projeto não inclui arquivos do jogo. É necessária uma cópia própria e legítima de Auto Modellista para "
        "GameCube. Auto Modellista é uma marca registrada da Capcom; este port não é afiliado nem endossado pela "
        "Capcom.";
    const float lw = std::min(px(640.0F), b.x - x - pad);
    const float lh = paragraph(nullptr, Face::Body, 13.0F, 13.0F * 1.6F, ImVec2(0, 0), lw, 0, legal);
    (void)paragraph(dl, Face::Body, 13.0F, 13.0F * 1.6F, ImVec2(x, b.y - pad - lh), lw, white(0.70F), legal);
}

struct PageText {
    const char *title;
    const char *sub;
};

const PageText kPageText[kPageCount] = {
    {"", ""},
    {"Gráficos", "Resolução, desempenho e o visual cel-shaded do jogo."},
    {"Áudio", "Volumes, saída de som e comportamento em segundo plano."},
    {"Controles", "Gamepad, teclado e memory card."},
    {"Interface", "Aparência e comportamento do launcher."},
    {"Suporte", "Algo deu errado? Junte as informações e fale com a gente."},
    {"", ""},
};

} // namespace

void draw_page(Page page, ImVec2 a, ImVec2 b) {
    ImDrawList *wdl = ImGui::GetWindowDrawList();
    if (page == Page::About) {
        page_about(wdl, a, b);
        return;
    }
    const PageText &t = kPageText[static_cast<int>(page)];
    text(wdl, Face::Display, 40.0F, ImVec2(a.x + px(32.0F), a.y + px(28.0F)), IM_COL32_WHITE, t.title, 0.02F);
    const float sub_y = a.y + px(28.0F) + line_h(Face::Display, 40.0F) + px(2.0F);
    text(wdl, Face::Body, 14.0F, ImVec2(a.x + px(32.0F), sub_y), white(0.72F), t.sub);
    const float top = sub_y + line_h(Face::Body, 14.0F) + px(8.0F);
    ImGui::SetCursorScreenPos(ImVec2(a.x, top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, px(8.0F));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImGui::ColorConvertU32ToFloat4(white(0.25F)));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImGui::ColorConvertU32ToFloat4(white(0.40F)));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImGui::ColorConvertU32ToFloat4(white(0.50F)));
    if (ImGui::BeginChild("##page", ImVec2(b.x - a.x, b.y - top - px(2.0F)), ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        Form f{ImGui::GetWindowDrawList(), at.x + px(32.0F), at.y + px(8.0F), b.x - a.x - px(64.0F), true, true};
        if (g.focus_content) {
            fr.focus_request = true; /* o primeiro controle ativo recebe o foco */
            g.focus_content = false;
        }
        switch (page) {
        case Page::Graphics:
            page_graphics(f);
            break;
        case Page::Audio:
            page_audio(f);
            break;
        case Page::Controls:
            page_controls(f);
            break;
        case Page::Interface:
            page_interface(f);
            break;
        case Page::Support:
            f.y += px(8.0F);
            page_support(f);
            break;
        default:
            break;
        }
        fr.focus_request = false;
        ImGui::SetCursorScreenPos(ImVec2(at.x, f.y + px(24.0F)));
        ImGui::Dummy(ImVec2(1.0F, 1.0F));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
}

} // namespace launcher
