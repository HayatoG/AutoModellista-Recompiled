/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tema e componentes da tela inicial (launcher_ui.hpp). */
#include "launcher_ui.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "am/platform_ui.h"

#include "imgui_internal.h"

namespace lui {

Frame fr;

namespace {

constexpr float kPi = 3.14159265F;

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

ImU32 rgb(int r, int g, int b, float a) {
    return IM_COL32(r, g, b, static_cast<int>(clampf(a, 0.0F, 1.0F) * 255.0F + 0.5F));
}

ImVec2 snap(ImVec2 p) {
    return ImVec2(std::floor(p.x + 0.5F), std::floor(p.y + 0.5F));
}

} // namespace

void begin_frame(const ImGuiViewport *vp, bool glass, ImU64 bg_tex, ImU64 thumb_tex, const LauncherRect &cover) {
    fr.origin = vp->Pos;
    fr.size = vp->Size;
    fr.s = launcher_scale(vp->Size.x, vp->Size.y);
    fr.glass = glass && thumb_tex != 0U;
    fr.accent = IM_COL32(0xFF, 0x6A, 0x1A, 255); /* #FF6A1A: a cor de destaque padrão do design */
    fr.on_accent = IM_COL32(0x12, 0x0D, 0x08, 255);
    fr.bg_tex = bg_tex;
    fr.thumb_tex = thumb_tex;
    fr.cover = cover;
}

ImU32 white(float alpha) {
    return rgb(255, 255, 255, alpha);
}

ImU32 black(float alpha) {
    return rgb(0, 0, 0, alpha);
}

ImU32 scale_alpha(ImU32 c, float f) {
    const float a = static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFFU) * f;
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(clampf(a, 0.0F, 255.0F) + 0.5F) << IM_COL32_A_SHIFT);
}

/* ---- texto ---------------------------------------------------------------------------------------------------------
 */

ImFont *font(Face f) {
    static const PlatformUiFont map[] = {PLATFORM_UI_FONT_BODY,     PLATFORM_UI_FONT_BOLD,    PLATFORM_UI_FONT_REGULAR,
                                         PLATFORM_UI_FONT_SEMIBOLD, PLATFORM_UI_FONT_DISPLAY, PLATFORM_UI_FONT_MONO};
    return static_cast<ImFont *>(platform_ui_font(static_cast<u32>(map[static_cast<int>(f)])));
}

namespace {

/* hhea da Saira/Saira Condensed: 1,135 + 0,439 = 1,574 em; JetBrains Mono: 1,02 + 0,3. */
float natural_lh(Face f) {
    return f == Face::Mono ? 1.32F : 1.574F;
}

float asc_px(Face f, float size_px) {
    return font(f)->GetFontBaked(size_px)->Ascent;
}

} // namespace

float ascent(Face f, float size_d) {
    return asc_px(f, px(size_d) * natural_lh(f));
}

float line_h(Face f, float size_d) {
    return px(size_d) * natural_lh(f);
}

float text_w(Face f, float size_d, const char *t, float tracking) {
    const float size = px(size_d) * natural_lh(f); /* o stb_truetype escala por (ascent+descent): tamanho CSS x 1,574 */
    const float w = font(f)->CalcTextSizeA(size, FLT_MAX, 0.0F, t).x;
    if (tracking == 0.0F) {
        return w;
    }
    int n = 0;
    for (const char *p = t; *p != '\0'; p++) {
        n += (static_cast<unsigned char>(*p) & 0xC0U) != 0x80U ? 1 : 0;
    }
    return w + tracking * px(size_d) * static_cast<float>(n);
}

void text(ImDrawList *dl, Face f, float size_d, ImVec2 top_left, ImU32 col, const char *t, float tracking) {
    const float size = px(size_d) * natural_lh(f); /* o stb_truetype escala por (ascent+descent): tamanho CSS x 1,574 */
    ImFont *ft = font(f);
    top_left = snap(top_left);
    if (tracking == 0.0F) {
        dl->AddText(ft, size, top_left, col, t);
        return;
    }
    /* letter-spacing: um caractere por vez (perde o kerning, aceitável nos rótulos curtos em maiúsculas) */
    float x = top_left.x;
    for (const char *p = t; *p != '\0';) {
        unsigned int c = 0;
        const int len = ImTextCharFromUtf8(&c, p, nullptr);
        char one[8] = {};
        std::memcpy(one, p, static_cast<size_t>(len > 0 ? (len < 7 ? len : 7) : 1));
        dl->AddText(ft, size, ImVec2(x, top_left.y), col, one);
        x += ft->CalcTextSizeA(size, FLT_MAX, 0.0F, one).x + tracking * px(size_d);
        p += len > 0 ? len : 1;
    }
}

void text_cy(ImDrawList *dl, Face f, float size_d, float x, float cy, ImU32 col, const char *t, float tracking) {
    text(dl, f, size_d, ImVec2(x, cy - line_h(f, size_d) * 0.5F), col, t, tracking);
}

void text_fit(ImDrawList *dl, Face f, float size_d, float x, float cy, float max_w, ImU32 col, const char *t) {
    if (text_w(f, size_d, t) <= max_w) {
        text_cy(dl, f, size_d, x, cy, col, t);
        return;
    }
    char buf[512];
    (void)std::snprintf(buf, sizeof(buf), "%s", t);
    size_t n = std::strlen(buf);
    while (n > 1U) {
        n--;
        while (n > 1U && (static_cast<unsigned char>(buf[n]) & 0xC0U) == 0x80U) {
            n--;
        }
        char cut[520];
        (void)std::snprintf(cut, sizeof(cut), "%.*s\xE2\x80\xA6", static_cast<int>(n), buf);
        if (text_w(f, size_d, cut) <= max_w) {
            text_cy(dl, f, size_d, x, cy, col, cut);
            return;
        }
    }
}

float paragraph(ImDrawList *dl, Face f, float size_d, float lh_d, ImVec2 top_left, float max_w, ImU32 col,
                const char *t) {
    const float size = px(size_d) * natural_lh(f); /* o stb_truetype escala por (ascent+descent): tamanho CSS x 1,574 */
    const float lh = px(lh_d);
    const float lead = (lh - size) * 0.5F;
    ImFont *ft = font(f);
    float y = top_left.y;
    const char *p = t;
    while (*p != '\0') {
        /* guloso: palavras enquanto couberem (a primeira sempre entra, mesmo maior que a largura) */
        const char *line_end = p;
        const char *cursor = p;
        for (;;) {
            const char *we = cursor;
            while (*we != '\0' && *we != ' ' && *we != '\n') {
                we++;
            }
            if (line_end != p && ft->CalcTextSizeA(size, FLT_MAX, 0.0F, p, we).x > max_w) {
                break;
            }
            line_end = we;
            if (*we == '\0' || *we == '\n' || we[1] == '\0') {
                break;
            }
            cursor = we + 1;
        }
        if (dl != nullptr) {
            dl->AddText(ft, size, snap(ImVec2(top_left.x, y + lead)), col, p, line_end);
        }
        y += lh;
        p = line_end;
        if (*p == ' ' || *p == '\n') {
            p++;
        }
    }
    return y - top_left.y;
}

void text_skewed(ImDrawList *dl, Face f, float size_d, ImVec2 baseline_left, float shear_y, float skew_deg,
                 const char *t, const SkewLayer *layers, int n_layers) {
    const float size = px(size_d) * natural_lh(f); /* o stb_truetype escala por (ascent+descent): tamanho CSS x 1,574 */
    const float k = std::tan(skew_deg * kPi / 180.0F);
    const float asc = asc_px(f, size);
    for (int i = 0; i < n_layers; i++) {
        const int v0 = dl->VtxBuffer.Size;
        const ImVec2 pos(baseline_left.x + px(layers[i].offset.x), baseline_left.y - asc + px(layers[i].offset.y));
        dl->AddText(font(f), size, pos, layers[i].col, t);
        for (int v = v0; v < dl->VtxBuffer.Size; v++) {
            ImDrawVert &dv = dl->VtxBuffer[v];
            dv.pos.x += k * (shear_y - dv.pos.y);
        }
    }
}

/* ---- fundo e painéis ------------------------------------------------------------------------------------------------
 */

namespace {

ImVec2 bg_uv(ImVec2 p) {
    const LauncherRect &c = fr.cover;
    return ImVec2((p.x - fr.origin.x - c.x) / c.w, (p.y - fr.origin.y - c.y) / c.h);
}

/* Cor de cada vértice a partir de uma função da posição (rampa linear em alpha; preserva a franja do antialiasing). */
void shade_ramp(ImDrawList *dl, int v0, int v1, ImVec2 p0, ImVec2 dir, float len, ImU32 rgb_col, float a0, float a1) {
    for (int v = v0; v < v1; v++) {
        ImDrawVert &dv = dl->VtxBuffer[v];
        if ((dv.col >> IM_COL32_A_SHIFT) == 0U) {
            continue;
        }
        const float t = clampf(((dv.pos.x - p0.x) * dir.x + (dv.pos.y - p0.y) * dir.y) / len, 0.0F, 1.0F);
        dv.col = (rgb_col & ~IM_COL32_A_MASK) |
                 (static_cast<ImU32>(clampf(a0 + (a1 - a0) * t, 0.0F, 1.0F) * 255.0F + 0.5F) << IM_COL32_A_SHIFT);
    }
}

/* O scrim da esquerda do design (#05040c, 0,85 -> 0,15 em 55% -> 0) como fração de escurecimento em x. */
float scrim_alpha(float x) {
    const float t = clampf((x - fr.origin.x) / fr.size.x, 0.0F, 1.0F);
    return t < 0.55F ? 0.85F + (0.15F - 0.85F) * (t / 0.55F) : 0.15F * (1.0F - (t - 0.55F) / 0.45F);
}

/* Uma faixa da miniatura do vidro (UV igual ao da posição na tela), escurecida pelo scrim no mesmo x. */
void glass_piece(ImDrawList *dl, ImVec2 a, ImVec2 b, float r, ImDrawFlags flags) {
    const int v0 = dl->VtxBuffer.Size;
    dl->AddImageRounded(ImTextureRef(static_cast<ImTextureID>(fr.thumb_tex)), a, b, bg_uv(a), bg_uv(b), IM_COL32_WHITE,
                        r, flags);
    for (int v = v0; v < dl->VtxBuffer.Size; v++) {
        ImDrawVert &dv = dl->VtxBuffer[v];
        const int g = static_cast<int>((1.0F - scrim_alpha(dv.pos.x) * 0.97F) * 255.0F);
        dv.col = IM_COL32(g, g, g, static_cast<int>(dv.col >> IM_COL32_A_SHIFT));
    }
}

} // namespace

void draw_background(ImDrawList *dl) {
    const ImVec2 a = fr.origin;
    const ImVec2 b(a.x + fr.size.x, a.y + fr.size.y);
    if (fr.bg_tex == 0U) {
        dl->AddRectFilled(a, b, IM_COL32(0x07, 0x06, 0x0D, 255));
        return;
    }
    /* Em ladrilhos: o rasterizador de triângulos do SDL_Renderer em software não desenha o quadrilátero único de um
     * fundo grande (a janela de 1280x760 ficava preta); com ladrilhos de ~256 px sai certo em todos os tamanhos. */
    const int nx = std::max(1, static_cast<int>(std::ceil(fr.size.x / 256.0F)));
    const int ny = std::max(1, static_cast<int>(std::ceil(fr.size.y / 256.0F)));
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            const ImVec2 ta(std::floor(a.x + fr.size.x * static_cast<float>(i) / static_cast<float>(nx)),
                            std::floor(a.y + fr.size.y * static_cast<float>(j) / static_cast<float>(ny)));
            const ImVec2 tb(
                i == nx - 1 ? b.x : std::floor(a.x + fr.size.x * static_cast<float>(i + 1) / static_cast<float>(nx)),
                j == ny - 1 ? b.y : std::floor(a.y + fr.size.y * static_cast<float>(j + 1) / static_cast<float>(ny)));
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(fr.bg_tex)), ta, tb, bg_uv(ta), bg_uv(tb));
        }
    }
    const float knee = a.x + fr.size.x * 0.55F;
    const ImU32 hi = rgb(5, 4, 12, 0.85F);
    const ImU32 mid = rgb(5, 4, 12, 0.15F);
    const ImU32 lo = rgb(5, 4, 12, 0.0F);
    dl->AddRectFilledMultiColor(a, ImVec2(knee, b.y), hi, mid, mid, hi);
    dl->AddRectFilledMultiColor(ImVec2(knee, a.y), b, mid, lo, lo, mid);
}

void rrect_fill(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d, ImU32 col) {
    dl->AddRectFilled(a, b, col, px(radius_d));
}

void rrect_stroke(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d, ImU32 col, float thick_d) {
    const float t = std::max(1.0F, std::floor(px(thick_d) + 0.5F));
    const float h = t * 0.5F; /* a borda fica dentro do retângulo, como o border do CSS */
    dl->AddRect(ImVec2(a.x + h, a.y + h), ImVec2(b.x - h, b.y - h), col, std::max(0.0F, px(radius_d) - h), t);
}

void focus_ring(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d) {
    const float g = px(3.0F);
    dl->AddRect(ImVec2(a.x - g, a.y - g), ImVec2(b.x + g, b.y + g), fr.accent, px(radius_d) + g,
                std::max(2.0F, px(2.0F)));
}

void panel(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d) {
    const float r = px(radius_d);
    /* sombra 0 24px 60px rgba(0,0,0,.45): camadas crescentes, sem desfoque de verdade */
    for (int i = 0; i < 8; i++) {
        const float grow = px(60.0F) * static_cast<float>(i + 1) / 8.0F * 0.55F;
        const float dy = px(24.0F);
        dl->AddRectFilled(ImVec2(a.x - grow, a.y - grow + dy), ImVec2(b.x + grow, b.y + grow + dy), black(0.07F),
                          r + grow);
    }
    if (!fr.glass) {
        dl->AddRectFilled(a, b, IM_COL32(0x15, 0x13, 0x1E, 255), r);
        rrect_stroke(dl, a, b, radius_d, IM_COL32(0x2B, 0x28, 0x39, 255));
        return;
    }
    /* vidro: a miniatura esticada com a posição da tela; se o painel cruza o joelho do scrim, em duas faixas */
    const float knee = std::floor(fr.origin.x + fr.size.x * 0.55F);
    if (a.x + r < knee && knee < b.x - r) {
        glass_piece(dl, a, ImVec2(knee, b.y), r, ImDrawFlags_RoundCornersLeft);
        glass_piece(dl, ImVec2(knee, a.y), b, r, ImDrawFlags_RoundCornersRight);
    } else {
        glass_piece(dl, a, b, r, ImDrawFlags_RoundCornersAll);
    }
    dl->AddRectFilled(a, b, rgb(14, 11, 26, 0.42F), r);
    /* linear-gradient(140deg, branco .14 -> .04 -> rgba(18,14,32,.38)), como duas rampas lineares */
    const float ang = 140.0F * kPi / 180.0F;
    const ImVec2 dir(std::sin(ang), -std::cos(ang));
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    const float len = std::fabs(w * dir.x) + std::fabs(h * dir.y);
    const ImVec2 c((a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F);
    const ImVec2 p0(c.x - dir.x * len * 0.5F, c.y - dir.y * len * 0.5F);
    int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, r);
    shade_ramp(dl, v0, dl->VtxBuffer.Size, p0, dir, len, IM_COL32_WHITE, 0.13F, 0.0F);
    v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, r);
    shade_ramp(dl, v0, dl->VtxBuffer.Size, p0, dir, len, IM_COL32(18, 14, 32, 255), 0.0F, 0.30F);
    rrect_stroke(dl, a, b, radius_d, white(0.22F));
    /* realces internos: topo forte, esquerda e base fracos */
    const float in = px(1.0F);
    dl->PathArcTo(ImVec2(a.x + r, a.y + r), r - in, kPi, kPi * 1.5F);
    dl->PathLineTo(ImVec2(b.x - r, a.y + in));
    dl->PathArcTo(ImVec2(b.x - r, a.y + r), r - in, kPi * 1.5F, kPi * 2.0F);
    dl->PathStroke(white(0.45F), in);
    dl->AddLine(ImVec2(a.x + in, a.y + r), ImVec2(a.x + in, b.y - r), white(0.14F), in);
    dl->AddLine(ImVec2(a.x + r, b.y - in * 1.5F), ImVec2(b.x - r, b.y - in * 1.5F), white(0.07F), in);
}

void skewed_box(ImDrawList *dl, ImVec2 a, ImVec2 b, float radius_d, float skew_deg, ImU32 col) {
    constexpr int kSeg = 6;
    const float r = px(radius_d);
    const float k = std::tan(skew_deg * kPi / 180.0F);
    const float cy = (a.y + b.y) * 0.5F;
    ImVec2 pts[4 * (kSeg + 1)];
    int n = 0;
    const ImVec2 centers[4] = {ImVec2(b.x - r, a.y + r), ImVec2(b.x - r, b.y - r), ImVec2(a.x + r, b.y - r),
                               ImVec2(a.x + r, a.y + r)};
    for (int corner = 0; corner < 4; corner++) {
        for (int i = 0; i <= kSeg; i++) {
            const float ang = (-0.5F + 0.5F * static_cast<float>(corner) + 0.5F * static_cast<float>(i) / kSeg) * kPi;
            ImVec2 p(centers[corner].x + std::cos(ang) * r, centers[corner].y + std::sin(ang) * r);
            p.x += k * (cy - p.y);
            pts[n++] = p;
        }
    }
    dl->AddConvexPolyFilled(pts, n, col);
}

/* ---- ícones (traços em 24x24, como os SVG do design) ----------------------------------------------------------------
 */

void icon(ImDrawList *dl, Icon ic, ImVec2 center, float size_d, ImU32 col, float stroke_d) {
    const float u = px(size_d) / 24.0F;
    const float th = std::max(1.0F, px(stroke_d) * size_d / 18.0F * 0.75F);
    auto P = [&](float x, float y) { return ImVec2(center.x + (x - 12.0F) * u, center.y + (y - 12.0F) * u); };
    auto line = [&](float x0, float y0, float x1, float y1) { dl->AddLine(P(x0, y0), P(x1, y1), col, th); };
    auto poly = [&](const float *xy, int n, bool closed) {
        for (int i = 0; i < n; i++) {
            dl->PathLineTo(P(xy[i * 2], xy[i * 2 + 1]));
        }
        dl->PathStroke(col, th, closed ? ImDrawFlags_Closed : 0);
    };
    switch (ic) {
    case Icon::None:
        break;
    case Icon::Home: {
        const float roof[] = {3, 10.5F, 12, 3, 21, 10.5F};
        const float body[] = {5, 9.5F, 5, 21, 19, 21, 19, 9.5F};
        poly(roof, 3, false);
        poly(body, 4, false);
        break;
    }
    case Icon::Monitor:
        dl->AddRect(P(3, 4), P(21, 16), col, 2.0F * u, th);
        line(8, 20, 16, 20);
        line(12, 16, 12, 20);
        break;
    case Icon::Speaker: {
        const float cone[] = {4, 9, 8, 9, 13, 5, 13, 19, 8, 15, 4, 15};
        poly(cone, 6, true);
        dl->PathArcTo(P(13, 12), 4.2F * u, -0.9F, 0.9F);
        dl->PathStroke(col, th);
        dl->PathArcTo(P(13, 12), 8.2F * u, -0.9F, 0.9F);
        dl->PathStroke(col, th);
        break;
    }
    case Icon::Gamepad:
        dl->AddRect(P(2.5F, 7.5F), P(21.5F, 16.5F), col, 4.5F * u, th);
        line(7, 10.5F, 7, 13.5F);
        line(5.5F, 12, 8.5F, 12);
        dl->AddCircleFilled(P(16, 11), 1.0F * u, col);
        dl->AddCircleFilled(P(18.5F, 13), 1.0F * u, col);
        break;
    case Icon::Drop:
        dl->PathLineTo(P(12, 3));
        dl->PathLineTo(P(18, 14));
        dl->PathArcTo(P(12, 14), 6.0F * u, 0.0F, kPi);
        dl->PathLineTo(P(12, 3));
        dl->PathStroke(col, th);
        break;
    case Icon::Lifebuoy:
        dl->AddCircle(P(12, 12), 9.0F * u, col, 0, th);
        dl->AddCircle(P(12, 12), 4.0F * u, col, 0, th);
        line(5.6F, 5.6F, 9.2F, 9.2F);
        line(14.8F, 14.8F, 18.4F, 18.4F);
        line(18.4F, 5.6F, 14.8F, 9.2F);
        line(9.2F, 14.8F, 5.6F, 18.4F);
        break;
    case Icon::Info:
        dl->AddCircle(P(12, 12), 9.0F * u, col, 0, th);
        line(12, 11, 12, 17);
        dl->AddCircleFilled(P(12, 7.8F), 1.1F * u, col);
        break;
    case Icon::Folder: {
        const float tab[] = {3, 8, 3, 5.5F, 9, 5.5F, 11, 8};
        poly(tab, 4, false);
        dl->AddRect(P(3, 8), P(21, 19), col, 2.0F * u, th);
        break;
    }
    case Icon::Play:
        dl->AddTriangleFilled(P(7, 4.5F), P(7, 19.5F), P(19.5F, 12), col);
        break;
    case Icon::Check: {
        const float tick[] = {5, 12.5F, 9.5F, 17, 19, 7.5F};
        poly(tick, 3, false);
        break;
    }
    case Icon::Refresh: {
        dl->PathArcTo(P(12, 12), 8.0F * u, -0.3F, 5.2F);
        dl->PathStroke(col, th);
        const float head[] = {20, 5, 20, 11, 14, 11};
        poly(head, 3, false);
        break;
    }
    case Icon::Close:
        line(6, 6, 18, 18);
        line(18, 6, 6, 18);
        break;
    case Icon::Copy: {
        dl->AddRect(P(8, 8), P(20, 20), col, 2.0F * u, th);
        const float back[] = {8, 16, 5, 16, 4, 15, 4, 5, 5, 4, 15, 4, 16, 5, 16, 8};
        poly(back, 8, false);
        break;
    }
    case Icon::Bug:
        dl->AddRect(P(7, 8), P(17, 20), col, 5.0F * u, th);
        line(9, 8, 9, 6);
        line(15, 8, 15, 6);
        line(3, 13, 7, 13);
        line(17, 13, 21, 13);
        line(4, 8, 7, 10);
        line(20, 8, 17, 10);
        line(4, 19, 7, 17);
        line(20, 19, 17, 17);
        break;
    case Icon::Doc: {
        const float page[] = {7, 3, 14, 3, 19, 8, 19, 21, 7, 21};
        poly(page, 5, true);
        const float fold[] = {14, 3, 14, 8, 19, 8};
        poly(fold, 3, false);
        line(10, 13, 16, 13);
        line(10, 17, 16, 17);
        break;
    }
    case Icon::Help:
        dl->AddCircle(P(12, 12), 9.0F * u, col, 0, th);
        {
            const float sz = 14.0F * u * natural_lh(Face::Bold);
            ImFont *ft = font(Face::Bold);
            const float w = ft->CalcTextSizeA(sz, FLT_MAX, 0.0F, "?").x;
            dl->AddText(ft, sz, ImVec2(center.x - w * 0.5F, center.y - sz * 0.5F), col, "?");
        }
        break;
    case Icon::Link: {
        line(7, 17, 17, 7);
        const float head[] = {9, 7, 17, 7, 17, 15};
        poly(head, 3, false);
        break;
    }
    case Icon::Chevron: {
        const float v[] = {6, 9, 12, 15, 18, 9};
        poly(v, 3, false);
        break;
    }
    }
}

/* ---- itens navegáveis -----------------------------------------------------------------------------------------------
 */

Hit hit(const char *id, ImVec2 a, ImVec2 b) {
    Hit h;
    ImGui::SetCursorScreenPos(a);
    h.pressed = ImGui::InvisibleButton(id, ImVec2(std::max(1.0F, b.x - a.x), std::max(1.0F, b.y - a.y)));
    h.hovered = ImGui::IsItemHovered();
    h.held = ImGui::IsItemActive();
    h.focused = ImGui::IsItemFocused() && ImGui::GetCurrentContext()->NavCursorVisible;
    if (h.hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (fr.focus_request) {
        fr.focus_request = false;
        ImGui::SetKeyboardFocusHere(-1);
        ImGui::SetNavCursorVisible(true);
        h.focused = true;
    }
    return h;
}

float chip(ImDrawList *dl, ImVec2 pos, const char *label, Icon ic, int kind, float height_d, float size_d,
           bool measure_only) {
    const float tracking = 0.02F;
    const float tw = text_w(Face::Semi, size_d, label, tracking);
    const float icon_w = ic != Icon::None ? px(size_d + 6.0F) : 0.0F;
    const float w = px(20.0F) + icon_w + tw;
    if (measure_only) {
        return w;
    }
    const ImVec2 b(pos.x + w, pos.y + px(height_d));
    ImU32 bg = white(0.12F);
    ImU32 border = white(0.18F);
    ImU32 fg = IM_COL32_WHITE;
    if (kind == 1) {
        bg = fr.accent;
        border = 0;
        fg = fr.on_accent;
    } else if (kind == 2) {
        bg = IM_COL32(0x7E, 0xE2, 0xA0, 255);
        border = 0;
        fg = IM_COL32(0x0D, 0x2A, 0x14, 255);
    }
    dl->AddRectFilled(pos, b, bg, px(99.0F));
    if (border != 0U) {
        dl->AddRect(pos, b, border, px(99.0F), std::max(1.0F, px(1.0F)));
    }
    const float cy = (pos.y + b.y) * 0.5F;
    float x = pos.x + px(10.0F);
    if (ic != Icon::None) {
        icon(dl, ic, ImVec2(x + px((size_d) * 0.5F), cy), size_d, fg, 3.0F);
        x += icon_w;
    }
    text_cy(dl, Face::Semi, size_d, x, cy, fg, label, tracking);
    return w;
}

float soon_badge(ImDrawList *dl, ImVec2 pos) {
    const float size_d = 10.0F;
    const float tracking = 0.08F;
    const char *label = "EM BREVE";
    const float w = px(16.0F) + text_w(Face::Bold, size_d, label, tracking);
    const float h = px(18.0F);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), white(0.10F), px(99.0F));
    dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), white(0.35F), px(99.0F), std::max(1.0F, px(1.0F)));
    text_cy(dl, Face::Bold, size_d, pos.x + px(8.0F), pos.y + h * 0.5F, white(0.85F), label, tracking);
    return w;
}

bool nav_item(const char *id, ImVec2 a, ImVec2 b, Icon ic, const char *label, bool active) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const Hit h = hit(id, a, b);
    if (active) {
        if (fr.glass) {
            dl->AddRectFilled(a, b, white(0.16F), px(12.0F));
            dl->AddLine(ImVec2(a.x + px(10.0F), a.y + 1.0F), ImVec2(b.x - px(10.0F), a.y + 1.0F), white(0.35F), 1.0F);
        } else {
            dl->AddRectFilled(a, b, IM_COL32(0x26, 0x23, 0x38, 255), px(12.0F));
        }
    } else if (h.hovered || h.held) {
        dl->AddRectFilled(a, b, white(0.08F), px(12.0F));
    }
    const ImU32 col = active || h.hovered ? IM_COL32_WHITE : white(0.78F);
    const float cy = (a.y + b.y) * 0.5F;
    icon(dl, ic, ImVec2(a.x + px(14.0F + 9.0F), cy), 18.0F, col, 1.8F);
    text_cy(dl, Face::Body, 15.0F, a.x + px(14.0F + 18.0F + 12.0F), cy, col, label);
    if (h.focused) {
        focus_ring(dl, a, b, 12.0F);
    }
    return h.pressed;
}

float ghost_width(const char *label, Icon ic, float min_w_d) {
    const float w = px(36.0F) + (ic != Icon::None ? px(24.0F) : 0.0F) + text_w(Face::Semi, 14.0F, label);
    return std::max(w, px(min_w_d));
}

bool ghost(const char *id, ImVec2 pos, const char *label, Icon ic, bool enabled, float min_w_d, float *out_w,
           float height_d) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float w = ghost_width(label, ic, min_w_d);
    const ImVec2 b(pos.x + w, pos.y + px(height_d));
    if (out_w != nullptr) {
        *out_w = w;
    }
    Hit h;
    if (enabled) {
        h = hit(id, pos, b);
    }
    const float dim = enabled ? 1.0F : 0.4F;
    dl->AddRectFilled(pos, b, white((h.hovered ? 0.20F : 0.12F) * dim), px(12.0F));
    rrect_stroke(dl, pos, b, 12.0F, white(0.20F * dim));
    const float cy = (pos.y + b.y) * 0.5F;
    const float tw = text_w(Face::Semi, 14.0F, label);
    const float icw = ic != Icon::None ? px(24.0F) : 0.0F;
    float x = pos.x + (w - tw - icw) * 0.5F;
    if (ic != Icon::None) {
        icon(dl, ic, ImVec2(x + px(8.0F), cy), 16.0F, white(dim), 2.0F);
        x += icw;
    }
    text_cy(dl, Face::Semi, 14.0F, x, cy, white(dim), label);
    if (h.focused) {
        focus_ring(dl, pos, b, 12.0F);
    }
    return h.pressed;
}

bool icon_button(const char *id, ImVec2 a, ImVec2 b, Icon ic, bool danger_hover) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const Hit h = hit(id, a, b);
    if (h.hovered) {
        dl->AddRectFilled(a, b, danger_hover ? IM_COL32(0xD9, 0x3A, 0x3A, 255) : white(0.12F), px(8.0F));
    }
    icon(dl, ic, ImVec2((a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F), 14.0F, h.hovered ? IM_COL32_WHITE : white(0.8F), 2.0F);
    if (h.focused) {
        focus_ring(dl, a, b, 8.0F);
    }
    return h.pressed;
}

/* ---- linhas de formulário -------------------------------------------------------------------------------------------
 */

Row row_begin(Form &f, const char *title, const char *desc, float ctrl_w, float ctrl_h, bool soon, bool dim) {
    Row r{};
    r.dim = dim;
    r.top = f.y;
    r.left_w = std::max(px(120.0F), f.w - ctrl_w - px(24.0F));
    const float a = dim ? 0.55F : 1.0F;
    float left_h = line_h(Face::Semi, 15.0F);
    float desc_h = 0.0F;
    if (desc != nullptr && desc[0] != '\0') {
        desc_h = paragraph(nullptr, Face::Body, 13.0F, 13.0F * 1.574F, ImVec2(0, 0), r.left_w, 0, desc);
        left_h += px(2.0F) + desc_h;
    }
    r.h = px(32.0F) + std::max(left_h, ctrl_h);
    r.cy = f.y + r.h * 0.5F;
    r.right = f.x + f.w;
    if (!f.first) {
        f.dl->AddLine(ImVec2(f.x, f.y), ImVec2(f.x + f.w, f.y), white(0.09F), 1.0F);
    }
    float ty = r.cy - left_h * 0.5F;
    text(f.dl, Face::Semi, 15.0F, ImVec2(f.x, ty), white(a), title);
    if (soon) {
        (void)soon_badge(f.dl, ImVec2(f.x + text_w(Face::Semi, 15.0F, title) + px(10.0F),
                                      ty + (line_h(Face::Semi, 15.0F) - px(18.0F)) * 0.5F));
    }
    ty += line_h(Face::Semi, 15.0F) + px(2.0F);
    if (desc_h > 0.0F) {
        (void)paragraph(f.dl, Face::Body, 13.0F, 13.0F * 1.574F, ImVec2(f.x, ty), r.left_w, white(0.70F * a), desc);
    }
    return r;
}

void row_end(Form &f, const Row &r) {
    f.y += r.h;
    f.first = false;
}

float seg_width(const char *const *labels, int n) {
    float w = px(8.0F) + px(4.0F) * static_cast<float>(n - 1);
    for (int i = 0; i < n; i++) {
        w += std::max(px(64.0F), px(28.0F) + text_w(Face::Semi, 13.0F, labels[i]));
    }
    return w;
}

int seg(Form &f, const Row &r, const char *id, const char *const *labels, int n, int selected, unsigned enabled_mask) {
    ImDrawList *dl = f.dl;
    const float w = seg_width(labels, n);
    const float h = px(44.0F);
    const ImVec2 a(r.right - w, r.cy - h * 0.5F);
    const float dim = r.dim ? 0.4F : 1.0F;
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), black(0.28F * (r.dim ? 0.6F : 1.0F)), px(12.0F));
    rrect_stroke(dl, a, ImVec2(a.x + w, a.y + h), 12.0F, white(0.10F * dim));
    int picked = -1;
    float x = a.x + px(4.0F);
    ImGui::PushID(id);
    for (int i = 0; i < n; i++) {
        const float bw = std::max(px(64.0F), px(28.0F) + text_w(Face::Semi, 13.0F, labels[i]));
        const ImVec2 ba(x, a.y + px(4.0F));
        const ImVec2 bb(x + bw, a.y + h - px(4.0F));
        const bool ok = ((enabled_mask >> i) & 1U) != 0U && f.nav;
        Hit ht;
        if (ok) {
            ImGui::PushID(i);
            ht = hit("##o", ba, bb);
            ImGui::PopID();
            if (ht.pressed) {
                picked = i;
            }
        }
        const bool on = i == selected;
        const float opt_dim = ((enabled_mask >> i) & 1U) != 0U ? 1.0F : 0.4F;
        if (on) {
            dl->AddRectFilled(ba, bb, scale_alpha(fr.accent, r.dim ? 0.5F : opt_dim), px(9.0F));
        } else if (ht.hovered) {
            dl->AddRectFilled(ba, bb, white(0.08F), px(9.0F));
        }
        const ImU32 fg = on ? fr.on_accent : white(0.82F * opt_dim * (r.dim ? 0.6F : 1.0F));
        const float tw = text_w(Face::Semi, 13.0F, labels[i]);
        text_cy(dl, Face::Semi, 13.0F, ba.x + (bw - tw) * 0.5F, (ba.y + bb.y) * 0.5F, fg, labels[i]);
        if (ht.focused) {
            focus_ring(dl, ba, bb, 9.0F);
        }
        x += bw + px(4.0F);
    }
    ImGui::PopID();
    return picked;
}

bool toggle(const Form &f, const Row &r, const char *id, bool on, bool enabled) {
    ImDrawList *dl = f.dl;
    const float w = px(46.0F);
    const float h = px(26.0F);
    const ImVec2 a(r.right - w, r.cy - h * 0.5F);
    const ImVec2 b(a.x + w, a.y + h);
    Hit ht;
    if (enabled && f.nav) {
        ht = hit(id, a, b);
    }
    const float dim = enabled ? 1.0F : 0.4F;
    dl->AddRectFilled(a, b, on ? scale_alpha(fr.accent, dim) : white(0.2F * dim), px(99.0F));
    const float kx = a.x + px(3.0F) + (on ? px(20.0F) : 0.0F);
    dl->AddCircleFilled(ImVec2(kx + px(10.0F), a.y + px(13.0F) + px(2.0F)), px(10.0F), black(0.25F * dim));
    dl->AddCircleFilled(ImVec2(kx + px(10.0F), a.y + px(13.0F)), px(10.0F), white(dim));
    if (ht.focused) {
        focus_ring(dl, a, b, 13.0F);
    }
    return ht.pressed;
}

int slider(const Form &f, const Row &r, const char *id, int value, int lo, int hi, const char *suffix, bool enabled) {
    ImDrawList *dl = f.dl;
    const float tw = px(220.0F);
    const float vw = px(44.0F);
    const float x1 = r.right - vw - px(12.0F);
    const float x0 = x1 - tw;
    const float dim = enabled ? 1.0F : 0.35F;
    const float pct = hi > lo ? static_cast<float>(value - lo) / static_cast<float>(hi - lo) : 0.0F;
    const float ty = r.cy;
    const ImVec2 a(x0, ty - px(12.0F));
    const ImVec2 b(x1, ty + px(12.0F));
    int out = value;
    Hit ht;
    if (enabled && f.nav) {
        ht = hit(id, a, b);
        if (ht.held) {
            const float m = (ImGui::GetIO().MousePos.x - x0) / tw;
            out = lo + static_cast<int>(clampf(m, 0.0F, 1.0F) * static_cast<float>(hi - lo) + 0.5F);
        }
        if (ht.focused || ht.held) {
            const bool left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft);
            const bool right =
                ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight);
            if (left || right) {
                const int step = std::max(1, (hi - lo) / 20);
                out = std::min(hi, std::max(lo, value + (right ? step : -step)));
                ImGui::NavMoveRequestCancel(); /* esquerda/direita mexem na barra, não no foco */
            }
        }
    }
    const float cp = hi > lo ? static_cast<float>(out - lo) / static_cast<float>(hi - lo) : pct;
    const float th = px(6.0F);
    const float fx = x0 + tw * cp;
    dl->AddRectFilled(ImVec2(x0, ty - th * 0.5F), ImVec2(x1, ty + th * 0.5F), white(0.18F * dim), px(99.0F));
    dl->AddRectFilled(ImVec2(x0, ty - th * 0.5F), ImVec2(fx, ty + th * 0.5F), scale_alpha(fr.accent, dim), px(99.0F));
    dl->AddCircleFilled(ImVec2(fx, ty + px(2.0F)), px(9.0F), black(0.3F * dim));
    dl->AddCircleFilled(ImVec2(fx, ty), px(9.0F), white(dim));
    if (ht.focused) {
        dl->AddCircle(ImVec2(fx, ty), px(13.0F), fr.accent, 0, std::max(2.0F, px(2.0F)));
    }
    char buf[32];
    (void)std::snprintf(buf, sizeof(buf), "%d%s", out, suffix);
    const float lw = text_w(Face::Mono, 13.0F, buf);
    text_cy(dl, Face::Mono, 13.0F, r.right - lw, ty, white(0.85F * dim), buf);
    return out;
}

bool row_ghost(const Form &f, const Row &r, const char *id, const char *label, Icon ic, bool enabled, float min_w_d) {
    (void)f;
    const float w = ghost_width(label, ic, min_w_d);
    const ImVec2 pos(r.right - w, r.cy - px(22.0F));
    return ghost(id, pos, label, ic, enabled && f.nav, min_w_d, nullptr);
}

} // namespace lui
