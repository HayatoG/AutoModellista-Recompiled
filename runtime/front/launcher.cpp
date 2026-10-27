/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tela inicial (am/front.h), segundo o design de work/launcher-bg/design_Main.dc.html: fundo "cover" com o scrim da
 * esquerda, menu lateral de 236 px com sete seções, painéis de vidro fosco (miniatura do fundo esticada por trás de
 * cada painel, sem shader), título inclinado e o cartão da pasta do jogo com o botão JOGAR. Desenhada com o Dear ImGui
 * pela plataforma (am/platform_ui.h); teclado e controle navegam pelos itens (foco na cor de destaque). As páginas de
 * configuração estão em launcher_pages.cpp e os componentes em launcher_ui.cpp. */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "am/launcher_gfx.h"
#include "am/pad_input.h"
#include "am/platform.h"
#include "am/platform_gpu.h"
#include "am/platform_ui.h"
#include "am/ui_assets.h"

#include "launcher_state.hpp"

#ifndef AM_VERSION
#define AM_VERSION "?"
#endif

namespace launcher {

State g;

namespace {

using namespace lui;

constexpr const char *kReleasesUrl = "https://github.com/HayatoG/AutoModellista-Recompiled/releases";

struct PageDef {
    const char *label;
    Icon ic;
};
const PageDef kPages[kPageCount] = {
    {"Início", Icon::Home},    {"Gráficos", Icon::Monitor}, {"Áudio", Icon::Speaker}, {"Controles", Icon::Gamepad},
    {"Interface", Icon::Drop}, {"Suporte", Icon::Lifebuoy}, {"Sobre", Icon::Info},
};

/* ---- estado auxiliar ------------------------------------------------------------------------------------------------
 */

void load_art() {
    if (g.dir_state != GAMEDIR_OK) {
        g.have_banner = false;
        g.loaded_art_dir[0] = '\0';
        return;
    }
    if (std::strcmp(g.loaded_art_dir, g.s->game_dir) == 0) {
        return;
    }
    (void)std::snprintf(g.loaded_art_dir, sizeof(g.loaded_art_dir), "%s", g.s->game_dir);
    g.have_banner = banner_load(g.s->game_dir, &g.banner);
    if (!g.have_banner) {
        return;
    }
    if (g.art == 0U) { /* as texturas da interface são poucas: troca a imagem em vez de criar outra */
        g.art = platform_ui_texture_add(g.banner.rgba, BANNER_WIDTH, BANNER_HEIGHT);
    } else {
        (void)platform_ui_texture_update(g.art, g.banner.rgba, BANNER_WIDTH, BANNER_HEIGHT);
    }
    g.have_banner = g.art != 0U;
}

/* O fundo embutido (1080p, ou 4K em janelas grandes) decodificado uma vez; dele sai o `mid` para a miniatura. */
void ensure_background(const ImGuiViewport *vp) {
    const float wpx = vp->Size.x * ImGui::GetIO().DisplayFramebufferScale.x;
    const u32 want = wpx >= 2200.0F ? 1U : 0U;
    if (g.bg_loaded && g.bg_kind == want) {
        return;
    }
    g.bg_loaded = true; /* mesmo em falha: não tenta de novo a cada quadro */
    g.bg_kind = want;
    const UiAsset a = ui_asset(want == 1U ? UI_ASSET_BG_2160 : UI_ASSET_BG_1080);
    u8 *rgba = nullptr;
    u32 w = 0;
    u32 h = 0;
    if (!platform_ui_decode_png(a.data, a.size, &rgba, &w, &h)) {
        return;
    }
    if (g.mid == nullptr) {
        g.mid = static_cast<u8 *>(std::malloc(static_cast<size_t>(kMidW) * kMidH * 4U));
    }
    if (g.mid != nullptr) {
        launcher_box_reduce(rgba, w, h, g.mid, kMidW, kMidH);
        g.thumb_blur = 0; /* refaz a miniatura */
    }
    if (g.bg_handle == 0U) {
        g.bg_handle = platform_ui_texture_add(rgba, w, h);
    } else {
        (void)platform_ui_texture_update(g.bg_handle, rgba, w, h);
    }
    platform_ui_free(rgba);
}

/* A miniatura do vidro (média de caixa do fundo, 16:9, tamanho conforme a intensidade). */
void ensure_thumb() {
    if (!g.s->ui_glass || g.mid == nullptr || g.thumb_blur == g.s->ui_blur) {
        return;
    }
    const u32 w =
        launcher_glass_width(g.s->ui_blur) * 2U; /* células menores: o SDL_Renderer em software amostra sem filtro */
    const u32 h = std::max(8U, w * 9U / 16U);
    u8 *px8 = static_cast<u8 *>(std::malloc(static_cast<size_t>(w) * h * 4U));
    if (px8 == nullptr) {
        return;
    }
    launcher_box_reduce(g.mid, kMidW, kMidH, px8, w, h);
    launcher_glass_finish(px8, w, h, launcher_glass_passes(g.s->ui_blur, w), 1.7F); /* saturate(170%) do design */
    if (g.thumb_handle == 0U) {
        g.thumb_handle = platform_ui_texture_add(px8, w, h);
    } else {
        (void)platform_ui_texture_update(g.thumb_handle, px8, w, h);
    }
    g.thumb_blur = g.s->ui_blur;
    std::free(px8);
}

void play() {
    check_dir(g.s->game_dir);
    if (g.dir_state != GAMEDIR_OK) {
        go(Page::Home);
        (void)std::snprintf(g.message, sizeof(g.message), "Antes de jogar, escolha a pasta do jogo extraído.");
        return;
    }
    g.result = LAUNCHER_PLAY;
    g.done = true;
}

const char *pretty_driver(const char *driver) {
    if (std::strcmp(driver, "direct3d12") == 0) {
        return "D3D12";
    }
    if (std::strcmp(driver, "vulkan") == 0) {
        return "Vulkan";
    }
    return std::strcmp(driver, "metal") == 0 ? "Metal" : driver;
}

void summary(char *out, size_t size) {
    static const char *const filters[] = {"Pixels", "Suave", "Nítido"};
    const char *driver = platform_gpu_driver();
    (void)std::snprintf(out, size, "%s · %s %u× · %s", driver[0] != '\0' ? pretty_driver(driver) : "Software",
                        g.s->fullscreen ? "tela cheia, janela" : "janela", g.s->window_scale,
                        filters[g.s->filter < 3U ? g.s->filter : 0U]);
}

/* ---- barra superior e menu lateral ----------------------------------------------------------------------------------
 */

void draw_topbar(ImDrawList *dl) {
    const float h = px(40.0F);
    const float cy = fr.origin.y + h * 0.5F;
    text_cy(dl, Face::Body, 12.0F, fr.origin.x + px(18.0F), cy, white(0.72F), "AUTO MODELLISTA · LAUNCHER", 0.08F);
    const float bw = px(40.0F);
    const ImVec2 ca(fr.origin.x + fr.size.x - px(8.0F) - bw, fr.origin.y + (h - px(32.0F)) * 0.5F);
    if (icon_button("close", ca, ImVec2(ca.x + bw, ca.y + px(32.0F)), Icon::Close, true)) {
        g.result = LAUNCHER_QUIT;
        g.done = true;
    }
    const char *hint =
        g.capture >= 0 ? "Esc  Cancelar" : "Setas  Navegar   ·   Enter / A  Confirmar   ·   Esc / B  Voltar";
    const float hw = text_w(Face::Body, 12.0F, hint);
    text_cy(dl, Face::Body, 12.0F, ca.x - px(16.0F) - hw, cy, white(0.58F), hint);
}

void draw_nav(ImDrawList *dl, ImVec2 a, ImVec2 b) {
    const float x0 = a.x + px(14.0F);
    const float w = b.x - a.x - px(28.0F);
    float y = a.y + px(20.0F);
    /* logotipo: AM inclinado com sombra dura, e o nome */
    const float lx = x0 + px(6.0F);
    const float box = px(44.0F);
    skewed_box(dl, ImVec2(lx + px(3.0F), y + px(3.0F)), ImVec2(lx + px(3.0F) + box, y + px(3.0F) + box), 12.0F, 8.0F,
               IM_COL32_BLACK);
    skewed_box(dl, ImVec2(lx, y), ImVec2(lx + box, y + box), 12.0F, 8.0F, fr.accent);
    {
        const float tw = text_w(Face::Display, 22.0F, "AM");
        const float base = y + box * 0.5F - line_h(Face::Display, 22.0F) * 0.5F + ascent(Face::Display, 22.0F);
        const SkewLayer l = {ImVec2(0, 0), fr.on_accent};
        text_skewed(dl, Face::Display, 22.0F, ImVec2(lx + (box - tw) * 0.5F, base), y + box * 0.5F, 8.0F, "AM", &l, 1);
    }
    const float tx = lx + box + px(12.0F);
    const float block = px(18.0F * 1.05F + 12.0F * 1.05F);
    const float ty = y + (box - block) * 0.5F;
    text_cy(dl, Face::Display, 18.0F, tx, ty + px(18.0F * 1.05F) * 0.5F, IM_COL32_WHITE, "AUTO MODELLISTA", 0.04F);
    text_cy(dl, Face::Body, 12.0F, tx, ty + px(18.0F * 1.05F) + px(12.0F * 1.05F) * 0.5F, white(0.70F),
            "Port GameCube");
    y += box + px(22.0F);
    for (int i = 0; i < kPageCount; i++) {
        const bool active = static_cast<int>(g.page) == i;
        if (active && g.focus_nav) {
            fr.focus_request = true;
            g.focus_nav = false;
        }
        if (nav_item(kPages[i].label, ImVec2(x0, y), ImVec2(x0 + w, y + px(44.0F)), kPages[i].ic, kPages[i].label,
                     active)) {
            g.page = static_cast<Page>(i);
            g.message[0] = '\0';
            g.focus_content = !ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        }
        y += px(48.0F);
    }
    /* rodapé: versão e link das versões publicadas */
    const float foot = px(14.0F) + line_h(Face::Body, 13.0F) + px(12.0F) + px(44.0F);
    const float fy = b.y - px(20.0F) - foot;
    dl->AddLine(ImVec2(x0, fy), ImVec2(x0 + w, fy), white(0.12F), 1.0F);
    const float ix = x0 + px(6.0F);
    const float iw = w - px(12.0F);
    const float vy = fy + px(14.0F) + line_h(Face::Body, 13.0F) * 0.5F;
    text_cy(dl, Face::Body, 13.0F, ix, vy, white(0.70F), "Versão");
    char ver[32];
    (void)std::snprintf(ver, sizeof(ver), "v%s", AM_VERSION);
    text_cy(dl, Face::Mono, 13.0F, ix + iw - text_w(Face::Mono, 13.0F, ver), vy, IM_COL32_WHITE, ver);
    if (ghost("releases", ImVec2(ix, fy + px(14.0F) + line_h(Face::Body, 13.0F) + px(12.0F)), "Ver versões",
              Icon::Refresh, true, iw / fr.s, nullptr)) {
        (void)platform_open_url(kReleasesUrl);
    }
}

/* ---- Início ---------------------------------------------------------------------------------------------------------
 */

struct HomeLayout {
    float x, right, top, bottom; /* a área útil (px da tela) */
    float chip_y, mid_y, card_y, card_h, h1_h, para_h;
};

constexpr float kTitleSize = 128.0F;
constexpr float kTitleLine = kTitleSize * 0.86F;
constexpr float kParaMax = 460.0F;
const char *const kTagline = "Tuning, drift e cel-shading — agora rodando nativo, sem emulador.";

/* Mesma conta do flex column / space-between do design: o chip no topo, o bloco do título no meio e o cartão embaixo.
 */
HomeLayout home_layout(ImVec2 ma, ImVec2 mb) {
    HomeLayout l{};
    l.x = ma.x + px(24.0F);
    l.right = mb.x;
    l.top = ma.y + px(8.0F);
    l.bottom = mb.y;
    l.card_h = px(40.0F) + px(72.0F) + px(8.0F) + line_h(Face::Body, 12.0F);
    l.h1_h = px(kTitleLine * 2.0F);
    l.para_h = paragraph(nullptr, Face::Body, 18.0F, 18.0F * 1.45F, ImVec2(0, 0), px(kParaMax), 0, kTagline);
    const float chip_h = px(32.0F);
    const float mid_h = px(26.0F + 14.0F) + l.h1_h + px(14.0F + 6.0F) + l.para_h + px(8.0F);
    const float gap = std::max(px(8.0F), (l.bottom - l.top - chip_h - mid_h - l.card_h) * 0.5F);
    l.chip_y = l.top;
    l.mid_y = l.top + chip_h + gap;
    l.card_y = l.bottom - l.card_h;
    return l;
}

void draw_title_block(ImDrawList *dl, const HomeLayout &l) {
    float y = l.mid_y;
    const float chip_w = chip(dl, ImVec2(l.x, y), "PORT NATIVO", Icon::None, 1);
    (void)chip(dl, ImVec2(l.x + chip_w + px(8.0F), y), "GameCube → PC", Icon::None, 0);
    y += px(26.0F + 14.0F);
    /* AUTO / MODELLISTA: Saira Condensed ExtraBold 128 px, linha de 0,86, inclinado -9° com origem no canto inferior
     * esquerdo, sombra dura escura (4,4) e depois a da cor de destaque (9,9) */
    const SkewLayer layers[3] = {
        {ImVec2(9, 9), fr.accent}, {ImVec2(4, 4), IM_COL32(6, 4, 12, 255)}, {ImVec2(0, 0), IM_COL32_WHITE}};
    const float shear_y = y + l.h1_h;
    const float half_lead = (px(kTitleLine) - line_h(Face::Display, kTitleSize)) * 0.5F;
    const char *const lines[2] = {"AUTO", "MODELLISTA"};
    for (int i = 0; i < 2; i++) {
        const float base = y + px(kTitleLine) * static_cast<float>(i) + half_lead + ascent(Face::Display, kTitleSize);
        text_skewed(dl, Face::Display, kTitleSize, ImVec2(l.x, base), shear_y, 9.0F, lines[i], layers, 3);
    }
    y += l.h1_h + px(14.0F + 6.0F);
    (void)paragraph(dl, Face::Body, 18.0F, 18.0F * 1.45F, ImVec2(l.x, y), px(kParaMax), white(0.85F), kTagline);
}

void draw_play(ImDrawList *dl, ImVec2 a, ImVec2 b, bool enabled) {
    if (enabled && g.focus_content) {
        fr.focus_request = true;
        g.focus_content = false;
    }
    Hit h;
    if (enabled) {
        h = hit("play", a, b);
    }
    for (int i = 0; i < 6; i++) { /* 0 12px 30px rgba(0,0,0,.35) */
        const float grow = px(30.0F) * static_cast<float>(i + 1) / 6.0F * 0.5F;
        dl->AddRectFilled(ImVec2(a.x - grow, a.y - grow + px(12.0F)), ImVec2(b.x + grow, b.y + grow + px(12.0F)),
                          black(0.08F), px(18.0F) + grow);
    }
    const ImU32 bg = enabled ? fr.accent : white(0.14F);
    const ImU32 fg = enabled ? fr.on_accent : white(0.50F);
    dl->AddRectFilled(a, b, bg, px(18.0F));
    if (enabled) {
        if (h.hovered) {
            dl->AddRectFilled(a, b, white(0.07F), px(18.0F));
        }
        dl->AddLine(ImVec2(a.x + px(14.0F), a.y + 1.0F), ImVec2(b.x - px(14.0F), a.y + 1.0F), white(0.5F), 1.0F);
    }
    const char *label = "JOGAR";
    const float tw = text_w(Face::Display, 30.0F, label, 0.06F);
    const float cw = px(22.0F + 12.0F) + tw;
    const float cx = (a.x + b.x) * 0.5F - cw * 0.5F;
    const float cy = (a.y + b.y) * 0.5F;
    icon(dl, Icon::Play, ImVec2(cx + px(11.0F), cy), 22.0F, fg, 1.8F);
    text_cy(dl, Face::Display, 30.0F, cx + px(34.0F), cy, fg, label, 0.06F);
    if (h.focused) {
        focus_ring(dl, a, b, 18.0F);
    }
    if (h.pressed) {
        play();
    }
}

void draw_game_card(ImDrawList *dl, const HomeLayout &l) {
    const ImVec2 a(l.x, l.card_y);
    const ImVec2 b(l.right, l.card_y + l.card_h);
    const float cy = (a.y + b.y) * 0.5F;
    const bool ok = g.dir_state == GAMEDIR_OK;
    float x = a.x + px(24.0F);
    /* ícone da pasta do jogo: o banner do disco do jogador, se houver */
    const float box_w = g.have_banner ? px(96.0F) : px(56.0F);
    const ImVec2 ia(x, cy - px(28.0F));
    const ImVec2 ib(x + box_w, cy + px(28.0F));
    dl->AddRectFilled(ia, ib, white(0.10F), px(14.0F));
    rrect_stroke(dl, ia, ib, 14.0F, white(0.18F));
    const u64 tex = g.art != 0U ? platform_ui_texture_id(g.art) : 0U;
    if (g.have_banner && tex != 0U) {
        dl->AddImageRounded(ImTextureRef(static_cast<ImTextureID>(tex)), ImVec2(ia.x + px(6.0F), cy - px(14.0F)),
                            ImVec2(ib.x - px(6.0F), cy + px(14.0F)), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE,
                            px(4.0F));
    } else {
        const ImVec2 c((ia.x + ib.x) * 0.5F, cy);
        dl->AddCircle(c, px(12.0F), IM_COL32_WHITE, 0, px(1.6F));
        dl->AddCircle(c, px(3.0F), IM_COL32_WHITE, 0, px(1.6F));
        dl->PathArcTo(c, px(7.0F), -1.5707F, 0.0F);
        dl->PathStroke(IM_COL32_WHITE, px(1.6F));
    }
    x += box_w + px(24.0F);
    const float right = b.x - px(20.0F);
    const float play_x = right - px(240.0F);
    const float div_x = play_x - px(24.0F);
    Import &im = g.imp;
    const bool running = im.phase == ImportPhase::Running;
    const bool offer = im.phase == ImportPhase::Offer;
    const char *pick_label = ok ? "Trocar pasta" : "Escolher pasta";
    const char *img_label = "Escolher imagem";
    const float btn_h = 36.0F;
    float col_w = std::max(ghost_width(pick_label, Icon::Folder, 0.0F), ghost_width(img_label, Icon::Doc, 0.0F));
    col_w = std::max(col_w, ghost_width("Usar a existente", Icon::Check, 0.0F));
    const float col_x = div_x - px(24.0F) - col_w;
    const float info_w = std::max(px(80.0F), col_x - px(24.0F) - x);
    /* texto: rótulo, caminho (ou "nenhuma") e a linha de situação */
    const float label_h = line_h(Face::Bold, 12.0F);
    const float line1_h = line_h(Face::Mono, 15.0F);
    const float line2_h = px(22.0F);
    const float total = label_h + px(6.0F) + line1_h + px(6.0F) + line2_h;
    float y = cy - total * 0.5F;
    const char *label = running ? "EXTRAINDO A IMAGEM" : (offer ? "EXTRAÇÃO EXISTENTE" : "PASTA DO JOGO");
    text(dl, Face::Bold, 12.0F, ImVec2(x, y), white(0.70F), label, 0.1F);
    y += label_h + px(6.0F);
    const ImU32 amber = IM_COL32(255, 196, 140, 255);
    char cur[sizeof(im.current)] = "";
    u64 done = 0;
    u64 all = 0;
    u32 fdone = 0;
    u32 fall = 0;
    if (running) { /* o progresso é escrito pela thread da extração */
        if (im.lock != nullptr && !im.fake) {
            platform_lock(im.lock);
        }
        (void)std::snprintf(cur, sizeof(cur), "%s", im.current);
        done = im.bytes_done;
        all = im.bytes_total;
        fdone = im.files_done;
        fall = im.files_total;
        if (im.lock != nullptr && !im.fake) {
            platform_unlock(im.lock);
        }
    }
    const bool have_path = ok || g.typed[0] != '\0';
    if (running) {
        text_fit(dl, Face::Mono, 15.0F, x, y + line1_h * 0.5F, info_w, IM_COL32_WHITE, cur[0] != '\0' ? cur : "...");
    } else if (offer) {
        text_fit(dl, Face::Mono, 15.0F, x, y + line1_h * 0.5F, info_w, IM_COL32_WHITE, im.out);
    } else if (have_path) {
        text_fit(dl, Face::Mono, 15.0F, x, y + line1_h * 0.5F, info_w, IM_COL32_WHITE, ok ? g.s->game_dir : g.typed);
    } else {
        text_fit(dl, Face::Semi, 16.0F, x, y + line1_h * 0.5F, info_w, IM_COL32_WHITE, "Nenhuma pasta selecionada");
    }
    y += line1_h + px(6.0F);
    const float y2 = y + line2_h * 0.5F;
    if (running) {
        const float frac = all != 0U ? static_cast<float>(static_cast<double>(done) / static_cast<double>(all)) : 0.0F;
        const float bw = info_w * 0.5F;
        dl->AddRectFilled(ImVec2(x, y2 - px(4.0F)), ImVec2(x + bw, y2 + px(4.0F)), white(0.18F), px(99.0F));
        dl->AddRectFilled(ImVec2(x, y2 - px(4.0F)), ImVec2(x + bw * frac, y2 + px(4.0F)), fr.accent, px(99.0F));
        char st[160];
        (void)std::snprintf(st, sizeof(st), "%.0f%% · %.0f / %.0f MiB · %u/%u arquivos",
                            static_cast<double>(frac) * 100.0, static_cast<double>(done) / 1048576.0,
                            static_cast<double>(all) / 1048576.0, fdone, fall);
        text_fit(dl, Face::Body, 13.0F, x + bw + px(12.0F), y2, info_w - bw - px(12.0F), white(0.80F), st);
    } else if (offer) {
        text_fit(dl, Face::Body, 13.0F, x, y2, info_w, white(0.80F),
                 "Já existe uma extração aqui; nada será sobrescrito.");
    } else if (g.message[0] != '\0') {
        text_fit(dl, Face::Body, 13.0F, x, y2, info_w, amber, g.message);
    } else if (ok) {
        const float cw = chip(dl, ImVec2(x, y), "Verificada", Icon::Check, 2, 22.0F);
        text_fit(dl, Face::Body, 13.0F, x + cw + px(8.0F), y2, info_w - cw - px(8.0F), white(0.75F),
                 "SHA-256 do main.dol confere (GAUE08)");
    } else if (g.dir_state != GAMEDIR_OK && g.typed[0] != '\0') {
        text_fit(dl, Face::Body, 13.0F, x, y2, info_w, amber, gamedir_result_text(g.dir_state));
    } else {
        text_fit(dl, Face::Body, 13.0F, x, y2, info_w, white(0.72F), "Aceita: pasta extraída, ISO, GCM e RVZ (zstd).");
    }
    /* botões empilhados à direita do texto */
    const float bx = col_x;
    const float by1 = cy - px(btn_h) - px(4.0F);
    const float by2 = cy + px(4.0F);
    if (!ok && !running && !offer && g.focus_content) { /* sem pasta, o foco inicial é o botão de escolher */
        fr.focus_request = true;
        g.focus_content = false;
    }
    if (running) {
        const float w = ghost_width("Cancelar", Icon::Close, 0.0F);
        if (ghost("cancel", ImVec2(bx + col_w - w, cy - px(btn_h) * 0.5F), "Cancelar", Icon::Close, true, 0.0F, nullptr,
                  btn_h)) {
            import_cancel();
        }
    } else if (offer) {
        if (ghost("useold", ImVec2(bx, by1), "Usar a existente", Icon::Check, true, col_w / fr.s, nullptr, btn_h)) {
            import_use_existing();
        }
        if (ghost("canceloffer", ImVec2(bx, by2), "Cancelar", Icon::Close, true, col_w / fr.s, nullptr, btn_h)) {
            import_cancel();
        }
    } else {
        if (ghost("img", ImVec2(bx, by1), img_label, Icon::Doc, true, col_w / fr.s, nullptr, btn_h)) {
            g.dialog_image = true;
            platform_dialog_open_file(nullptr, "Imagem do jogo (iso, gcm, rvz)", "iso;gcm;rvz");
        }
        if (ghost("pick", ImVec2(bx, by2), pick_label, Icon::Folder, true, col_w / fr.s, nullptr, btn_h)) {
            g.dialog_folder = true;
            platform_dialog_pick_folder(g.s->game_dir[0] != '\0' ? g.s->game_dir : nullptr);
        }
    }
    dl->AddLine(ImVec2(div_x, a.y + px(20.0F)), ImVec2(div_x, b.y - px(20.0F)), white(0.14F), 1.0F);
    const ImVec2 pa(play_x, a.y + px(20.0F));
    const bool can_play = ok && !running && !offer;
    draw_play(dl, pa, ImVec2(pa.x + px(240.0F), pa.y + px(72.0F)), can_play);
    const char *hint =
        running ? "Extraindo a imagem..." : (ok ? "Pronto para a largada" : "Selecione a pasta para jogar");
    const float hw = text_w(Face::Body, 12.0F, hint);
    text(dl, Face::Body, 12.0F, ImVec2(play_x + (px(240.0F) - hw) * 0.5F, pa.y + px(72.0F + 8.0F)), white(0.75F), hint);
}

void draw_home(ImDrawList *dl, const HomeLayout &l) {
    char sum[160];
    summary(sum, sizeof(sum));
    const float w = chip(dl, ImVec2(0, 0), sum, Icon::Monitor, 0, 32.0F, 13.0F, true);
    const ImVec2 ca(l.right - w, l.chip_y);
    const Hit h = hit("quick", ca, ImVec2(ca.x + w, ca.y + px(32.0F)));
    (void)chip(dl, ca, sum, Icon::Monitor, 0, 32.0F, 13.0F);
    if (h.hovered) {
        dl->AddRectFilled(ca, ImVec2(ca.x + w, ca.y + px(32.0F)), white(0.08F), px(99.0F));
    }
    if (h.focused) {
        focus_ring(dl, ca, ImVec2(ca.x + w, ca.y + px(32.0F)), 16.0F);
    }
    if (h.pressed) {
        g.page = Page::Graphics;
        g.focus_content = !ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    }
    draw_title_block(dl, l);
    draw_game_card(dl, l);
}

/* ---- quadro ---------------------------------------------------------------------------------------------------------
 */

void shot_frame();

void draw(void *user) {
    (void)user;
    const u32 hot = platform_hotkeys_take();
    if ((hot & PLATFORM_HOTKEY_OVERLAY) != 0U) {
        g.s->fps_overlay = !g.s->fps_overlay;
    }
    g.s->fullscreen = platform_window_fullscreen();
    load_art();
    char picked[SETTINGS_PATH_SIZE];
    if (g.dialog_folder && platform_dialog_take(picked, sizeof(picked))) {
        g.dialog_folder = false;
        if (picked[0] != '\0') {
            (void)std::snprintf(g.typed, sizeof(g.typed), "%s", picked);
            check_dir(picked);
            g.message[0] = '\0';
        }
    }
    if (g.dialog_image && platform_dialog_take(picked, sizeof(picked))) {
        g.dialog_image = false;
        if (picked[0] != '\0') {
            import_start(picked);
        }
    }
    import_poll();
    if (g.dialog_card && platform_dialog_take(picked, sizeof(picked))) {
        g.dialog_card = false;
        if (picked[0] != '\0') {
            (void)std::snprintf(g.s->memcard, sizeof(g.s->memcard), "%s", picked);
        }
    }
    if (g.shot_mode) {
        shot_frame();
    }
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ensure_background(vp);
    ensure_thumb();
    const u64 bg_id = g.bg_handle != 0U ? platform_ui_texture_id(g.bg_handle) : 0U;
    const u64 thumb_id = g.thumb_handle != 0U && g.s->ui_glass ? platform_ui_texture_id(g.thumb_handle) : 0U;
    begin_frame(vp, g.s->ui_glass, bg_id, thumb_id, launcher_cover(vp->Size.x, vp->Size.y, 1920U, 1080U));
    if (back_pressed() && g.capture < 0 && g.page != Page::Home) {
        g.page = Page::Home;
        g.focus_nav = true;
    }
    /* painéis de vidro por trás de tudo (lista de desenho do fundo) */
    ImDrawList *bgl = ImGui::GetBackgroundDrawList();
    draw_background(bgl);
    const float top = px(40.0F);
    const float pad = px(16.0F);
    const ImVec2 o = fr.origin;
    const ImVec2 na(o.x + pad, o.y + top);
    const ImVec2 nb(na.x + px(236.0F), o.y + fr.size.y - pad);
    const ImVec2 ma(nb.x + pad, na.y);
    const ImVec2 mb(o.x + fr.size.x - pad, nb.y);
    panel(bgl, na, nb, 22.0F);
    HomeLayout hl{};
    if (g.page == Page::Home) {
        hl = home_layout(ma, mb);
        panel(bgl, ImVec2(hl.x, hl.card_y), ImVec2(hl.right, hl.card_y + hl.card_h), 22.0F);
    } else {
        panel(bgl, ma, mb, 22.0F);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    if (ImGui::GetFrameCount() < 3) {
        ImGui::SetNextWindowFocus();
    }
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##launcher", nullptr, flags)) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        draw_topbar(dl);
        draw_nav(dl, na, nb);
        if (g.page == Page::Home) {
            draw_home(dl, hl);
        } else {
            draw_page(g.page, ma, mb);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    fr.focus_request = false;
    g.focus_content = false; /* o pedido vale só para este quadro (a página Sobre não tem controles) */
    g.focus_nav = false;
}

void apply_pending() {
    if (g.pending == Pending::Backend) {
        if (g.s->gpu) {
            if (!platform_gpu_init(static_cast<PlatformGpuShader>(g.info->gpu_shader))) {
                (void)std::snprintf(g.message, sizeof(g.message), "GPU indisponível (%s): fica o software.",
                                    platform_gpu_error());
                g.s->gpu = false;
            }
        } else {
            platform_gpu_shutdown();
        }
    }
    g.pending = Pending::None;
}

/* ---- captura de desenvolvimento (AM_LAUNCHER_SHOT)
 * ------------------------------------------------------------------- */

/* Roteiro de cada captura: a página, o estilo, a pasta do jogo (válida ou não), o tamanho da janela e as teclas de
 * navegação enviadas ao ImGui (uma por quadro) antes de gravar. Sem janela visível: SDL_VIDEODRIVER=offscreen. */
struct ShotScene {
    const char *name;
    Page page;
    bool glass;
    bool valid_dir;
    u32 w, h; /* 0 = tamanho atual da janela */
    ImGuiKey keys[8];
    int imp = 0; /* 0 = nada; 1 = extraindo 42% (simulado); 2 = oferta de usar a extração existente; 3 = extração real
                  * de uma imagem inválida (erro); 4 = outro jogo (aviso) */
};

const ShotScene kScenes[] = {
    {"01_inicio_vidro", Page::Home, true, true, 1280, 760, {}},
    {"02_graficos_vidro", Page::Graphics, true, true, 1280, 760, {}},
    {"03_audio_vidro", Page::Audio, true, true, 1280, 760, {}},
    {"04_controles_vidro", Page::Controls, true, true, 1280, 760, {}},
    {"05_interface_vidro", Page::Interface, true, true, 1280, 760, {}},
    {"06_suporte_vidro", Page::Support, true, true, 1280, 760, {}},
    {"07_sobre_vidro", Page::About, true, true, 1280, 760, {}},
    {"11_inicio_solido", Page::Home, false, true, 1280, 760, {}},
    {"12_graficos_solido", Page::Graphics, false, true, 1280, 760, {}},
    {"13_audio_solido", Page::Audio, false, true, 1280, 760, {}},
    {"14_controles_solido", Page::Controls, false, true, 1280, 760, {}},
    {"15_interface_solido", Page::Interface, false, true, 1280, 760, {}},
    {"16_suporte_solido", Page::Support, false, true, 1280, 760, {}},
    {"17_sobre_solido", Page::About, false, true, 1280, 760, {}},
    {"21_inicio_sem_pasta", Page::Home, true, false, 1280, 760, {}},
    {"22_inicio_foco_teclado", Page::Home, true, true, 1280, 760, {ImGuiKey_LeftArrow, ImGuiKey_UpArrow}},
    {"23_graficos_foco_teclado",
     Page::Graphics,
     true,
     true,
     1280,
     760,
     {ImGuiKey_RightArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow}},
    {"24_controles_foco_teclado",
     Page::Controls,
     true,
     true,
     1280,
     760,
     {ImGuiKey_RightArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_RightArrow}},
    {"41_inicio_extraindo", Page::Home, true, false, 1280, 760, {}, 1},
    {"42_inicio_extracao_existente", Page::Home, true, false, 1280, 760, {}, 2},
    {"43_inicio_erro_imagem", Page::Home, true, false, 1280, 760, {}, 3},
    {"44_inicio_outro_jogo", Page::Home, true, false, 1280, 760, {}, 4},
    {"31_inicio_pequena", Page::Home, true, true, 640, 448, {}},
    {"32_graficos_pequena", Page::Graphics, true, true, 640, 448, {}},
    {"33_inicio_4k", Page::Home, true, true, 3840, 2160, {}},
    {"34_graficos_4k", Page::Graphics, true, true, 3840, 2160, {}},
    {"35_inicio_16x10", Page::Home, true, true, 1920, 1200, {}},
};

constexpr int kShotKeyStart = 8; /* quadro em que começam as teclas (antes: o ImGui e as fontes assentam) */
constexpr int kShotSettle = 5;   /* quadros depois da última tecla até gravar */

struct ShotState {
    const char *dir;
    int scene;
    int frame;
    Settings base;
    char game_dir[SETTINGS_PATH_SIZE];
};
ShotState s_shot;

/* Estados da extração para as capturas: simulados (sem thread), exceto o erro de imagem, que roda a thread de verdade
 * sobre um arquivo falso, com o destino fora da pasta de dados do jogador. */
void shot_import_scene(int imp) {
    Import &im = g.imp;
    import_shutdown();
    im.fake = false;
    im.out_override[0] = '\0';
    if (imp == 1) {
        im.fake = true;
        im.phase = ImportPhase::Running;
        im.bytes_total = 1459617792ULL;
        im.bytes_done = im.bytes_total * 42ULL / 100ULL;
        im.files_total = 203;
        im.files_done = 87;
        (void)std::snprintf(im.current, sizeof(im.current), "files/afs03.afs");
    } else if (imp == 2) {
        im.phase = ImportPhase::Offer;
        (void)std::snprintf(im.out, sizeof(im.out), "C:/Users/jogador/AppData/Roaming/AutoModellista/jogo");
    } else if (imp == 3) {
        char fake[1200];
        (void)std::snprintf(fake, sizeof(fake), "%s/imagem_falsa.iso", s_shot.dir);
        static const u8 junk[64] = {1, 2, 3};
        (void)platform_save_file(fake, junk, sizeof(junk));
        (void)std::snprintf(im.out_override, sizeof(im.out_override), "%s/jogo_teste", s_shot.dir);
        import_start(fake);
    } else if (imp == 4) {
        (void)std::snprintf(g.message, sizeof(g.message),
                            "Outro jogo ou outra versão (main.dol diferente): a pasta do jogo não foi trocada.");
    }
}

void shot_begin_scene(const ShotScene &sc) {
    *g.s = s_shot.base;
    g.s->ui_glass = sc.glass;
    g.page = sc.page;
    g.capture = -1;
    g.message[0] = '\0';
    g.typed[0] = '\0';
    if (sc.valid_dir) {
        (void)std::snprintf(g.s->game_dir, sizeof(g.s->game_dir), "%s", s_shot.game_dir);
    } else {
        (void)std::snprintf(g.s->game_dir, sizeof(g.s->game_dir), "C:\\Jogos\\Auto Modellista (pasta que nao existe)");
    }
    check_dir(g.s->game_dir);
    g.loaded_art_dir[0] = '\0';
    g.focus_content = false;
    g.focus_nav = sc.page != Page::Home;
    if (sc.page == Page::Home) {
        g.focus_content = true;
    }
    shot_import_scene(sc.imp);
    if (sc.w != 0U) {
        platform_window_set_size(sc.w, sc.h);
    }
}

/* Chamado dentro do quadro do ImGui: avança o roteiro das capturas. */
void shot_frame() {
    const ShotScene &sc = kScenes[s_shot.scene];
    ImGuiIO &io = ImGui::GetIO();
    const int f = s_shot.frame;
    for (int i = 0; i < 8; i++) {
        if (sc.keys[i] == ImGuiKey_None) {
            break;
        }
        if (f == kShotKeyStart + 2 * i) {
            io.AddKeyEvent(sc.keys[i], true);
        } else if (f == kShotKeyStart + 1 + 2 * i) {
            io.AddKeyEvent(sc.keys[i], false);
        }
    }
}

bool run_shots(const char *dir) {
    s_shot.dir = dir;
    s_shot.base = *g.s;
    const char *gd = std::getenv("AM_LAUNCHER_SHOT_GAMEDIR");
    (void)std::snprintf(s_shot.game_dir, sizeof(s_shot.game_dir), "%s", gd != nullptr ? gd : g.s->game_dir);
    s_shot.base.fullscreen = false;
    s_shot.base.gpu = false;
    s_shot.base.ui_blur = 24;
    (void)platform_make_dir(dir);
    g.shot_mode = true;
    const int count = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));
    const char *only = std::getenv("AM_LAUNCHER_SHOT_ONLY"); /* só as cenas cujo nome contém este texto */
    for (s_shot.scene = 0; s_shot.scene < count; s_shot.scene++) {
        const ShotScene &sc = kScenes[s_shot.scene];
        if (only != nullptr && only[0] != '\0' && std::strstr(sc.name, only) == nullptr) {
            continue;
        }
        shot_begin_scene(sc);
        int keys = 0;
        while (keys < 8 && sc.keys[keys] != ImGuiKey_None) {
            keys++;
        }
        const int last = kShotKeyStart + 2 * keys + kShotSettle;
        for (s_shot.frame = 0; s_shot.frame <= last; s_shot.frame++) {
            if (s_shot.frame == last) {
                char path[1200];
                (void)std::snprintf(path, sizeof(path), "%s/%s.png", dir, sc.name);
                platform_window_ui_shot(path);
            }
            if (platform_window_present(nullptr, 0, 0, nullptr) != PLATFORM_WINDOW_OK) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

/* ---- API compartilhada ----------------------------------------------------------------------------------------------
 */

void go(Page page) {
    g.page = page;
    g.focus_content = true;
    g.message[0] = '\0';
}

void check_dir(const char *dir) {
    char root[SETTINGS_PATH_SIZE];
    g.dir_state = gamedir_check(dir, root, sizeof(root), g.dir_hash);
    if (g.dir_state == GAMEDIR_OK) {
        (void)std::snprintf(g.s->game_dir, sizeof(g.s->game_dir), "%s", root);
    }
}

bool back_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
           ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
}

} // namespace launcher

extern "C" LauncherResult launcher_run(Settings *s, const LauncherInfo *info) {
    using namespace launcher;
    if (g.mid != nullptr) {
        std::free(g.mid);
    }
    g = State{};
    g.s = s;
    g.info = info;
    g.capture = -1;
    g.focus_content = true;
    Settings shot_copy = *s; /* as capturas nunca mexem nas configurações do jogador */
    const char *shot_dir = std::getenv("AM_LAUNCHER_SHOT");
    if (shot_dir != nullptr && shot_dir[0] != '\0') {
        g.s = &shot_copy;
    }
    check_dir(g.s->game_dir);
    if (g.dir_state != GAMEDIR_OK && g.s->game_dir[0] != '\0') {
        (void)std::snprintf(g.typed, sizeof(g.typed), "%s", g.s->game_dir);
    }
    platform_ui_set_draw(draw, nullptr);
    if (g.s == &shot_copy) {
        const bool ok = run_shots(shot_dir);
        import_shutdown();
        platform_ui_set_draw(nullptr, nullptr);
        return ok ? LAUNCHER_SHOT : LAUNCHER_ERROR;
    }
    while (!g.done) {
        const PlatformWindowResult r = platform_window_present(nullptr, 0, 0, nullptr);
        if (r != PLATFORM_WINDOW_OK) {
            g.result = r == PLATFORM_WINDOW_CLOSED ? LAUNCHER_QUIT : LAUNCHER_ERROR;
            break;
        }
        apply_pending();
        platform_sleep_ns(8000000ULL); /* a tela inicial não precisa de mais que ~120 quadros/s */
    }
    import_shutdown(); /* cancela e espera a extração: nunca sai com a pasta pela metade */
    platform_ui_set_draw(nullptr, nullptr);
    return g.result;
}
