// SPDX-License-Identifier: GPL-3.0-or-later
// M8: ubershader do GX para a GPU (SDL3 GPU API sobre Metal). Um só par de shaders para todo desenho: o estado do
// TEV, do Z e do blend vem por desenho num uniform (DrawState = GpuDrawUniforms de am/gpu_batch.h, mesma ordem de
// campos) e o shader de fragmento faz o que o backend em software faz por pixel (gfx/gx_tev.c, gx_tex.c gx_efb.c,
// que são a tradução do backend Software do Dolphin 2609a): TEV em aritmética inteira, amostragem das texturas já
// decodificadas em RGBA (vêm de um buffer, não de texturas da GPU, para repetir o wrap e o filtro bilinear em s17.7),
// teste de alfa, textura de Z, neblina, teste de Z e blend do Pixel Engine. O EFB são duas imagens R32Uint (cor
// empacotada em 24 bits como em gx_efb.c e Z de 24 bits) lidas e escritas pelo próprio shader (framebuffer fetch das
// GPUs da Apple, que garante a ordem dos fragmentos que se sobrepõem): sem blend nem teste de profundidade fixos.
// Compilado offline com o compilador Metal (scripts/build_shaders.sh); a runtime só carrega o metallib.
#include <metal_stdlib>
using namespace metal;

constant uint STAGES = 16;

struct DrawState {
    uint stages, texgens, ind_stages, colchans;
    uint zmode, pe_control, blend, dst_alpha;
    uint alpha_test, fog3, fog_color, fog_bmag;
    uint fog_bshift, ztex2, ztex_bias, flags;
    float fog_a, fog_c, vp_wd, pad0;
    uint fog_range[8];
    uint cc[16];
    uint ac[16];
    uint order[16];
    uint ksel[8];
    int regs[16];
    int konst[16];
    uint tex_size[8];
    uint tex_mode0[8];
    uint tex_mode1[8];
    uint tex_levels[8];
    uint tex_base[8];
};

struct VIn {
    float4 pos [[attribute(0)]]; // x, y em pixels do EFB; z de 24 bits; w de recorte
    float4 c0 [[attribute(1)]];
    float4 c1 [[attribute(2)]];
    float4 t0 [[attribute(3)]];
    float4 t1 [[attribute(4)]];
    float4 t2 [[attribute(5)]];
    float4 t3 [[attribute(6)]];
    float4 t4 [[attribute(7)]];
    float4 t5 [[attribute(8)]];
    float4 t6 [[attribute(9)]];
    float4 t7 [[attribute(10)]];
};

struct VOut {
    float4 position [[position]];
    float z [[center_no_perspective]];
    float4 c0 [[center_no_perspective]]; // o rasterizador do GX interpola cor sem correção de perspectiva
    float4 c1 [[center_no_perspective]];
    float3 t0;
    float3 t1;
    float3 t2;
    float3 t3;
    float3 t4;
    float3 t5;
    float3 t6;
    float3 t7;
};

vertex VOut gx_vs(VIn v [[stage_in]]) {
    VOut o;
    const float w = v.pos.w > 0.0 ? v.pos.w : 1.0;
    o.position = float4((v.pos.x / 320.0 - 1.0) * w, (1.0 - v.pos.y / 264.0) * w, 0.5 * w, w);
    o.z = v.pos.z;
    o.c0 = v.c0;
    o.c1 = v.c1;
    o.t0 = v.t0.xyz;
    o.t1 = v.t1.xyz;
    o.t2 = v.t2.xyz;
    o.t3 = v.t3.xyz;
    o.t4 = v.t4.xyz;
    o.t5 = v.t5.xyz;
    o.t6 = v.t6.xyz;
    o.t7 = v.t7.xyz;
    return o;
}

struct FOut {
    uint color [[color(0)]];
    uint depth [[color(1)]];
};

static int sign11(uint v) { return int(v << 21) >> 21; }
static int sign24(int v) { return int(uint(v) << 8) >> 8; }

// ---- texturas (gx_tex.c: wrap, sample_mip, gx_tex_sample) ----
static int wrap_coord(int coord, uint mode, int size) { // 0 clamp, 1 repeat, 2 mirror (3 = clamp)
    if (mode == 1u) {
        return coord & (size - 1);
    }
    if (mode == 2u) {
        if ((coord & size) != 0) {
            coord = ~coord;
        }
        return coord & (size - 1);
    }
    return clamp(coord, 0, size - 1);
}

static uint4 unpack_rgba(uint v) { return uint4(v & 0xFFu, (v >> 8) & 0xFFu, (v >> 16) & 0xFFu, v >> 24); }

static uint4 sample_mip(constant DrawState &d, device const uint *arena, uint m, int s, int t, uint mip, bool linear) {
    const uint wm1_0 = d.tex_size[m] & 0xFFFFu;
    const uint hm1_0 = d.tex_size[m] >> 16;
    if (mip >= d.tex_levels[m]) {
        return uint4(0);
    }
    uint base = d.tex_base[m];
    for (uint l = 0; l < mip; l++) {
        base += ((wm1_0 >> l) + 1u) * ((hm1_0 >> l) + 1u);
    }
    const int w = int(wm1_0 >> mip) + 1;
    const int h = int(hm1_0 >> mip) + 1;
    const uint ws = d.tex_mode0[m] & 3u;
    const uint wt = (d.tex_mode0[m] >> 2) & 3u;
    if (!linear) {
        return unpack_rgba(arena[base + uint(wrap_coord(t >> 7, wt, h) * w + wrap_coord(s >> 7, ws, w))]);
    }
    s -= 64;
    t -= 64;
    const int s0 = wrap_coord(s >> 7, ws, w);
    const int t0 = wrap_coord(t >> 7, wt, h);
    const int s1 = wrap_coord((s >> 7) + 1, ws, w);
    const int t1 = wrap_coord((t >> 7) + 1, wt, h);
    const uint fs = uint(s) & 0x7Fu;
    const uint ft = uint(t) & 0x7Fu;
    const uint4 a = unpack_rgba(arena[base + uint(t0 * w + s0)]);
    const uint4 b = unpack_rgba(arena[base + uint(t0 * w + s1)]);
    const uint4 c = unpack_rgba(arena[base + uint(t1 * w + s0)]);
    const uint4 e = unpack_rgba(arena[base + uint(t1 * w + s1)]);
    const uint4 v = a * (128u - fs) * (128u - ft) + b * fs * (128u - ft) + c * (128u - fs) * ft + e * fs * ft;
    return v >> 14;
}

static uint4 sample_tex(constant DrawState &d, device const uint *arena, uint m, int s, int t, int lod, bool linear) {
    const uint mip_filter = (d.tex_mode0[m] >> 5) & 3u;
    const int frac = lod & 0xF;
    uint base = 0;
    bool mip_linear = false;
    if (lod > 0 && mip_filter != 0u) {
        base = uint(lod >> 4);
        mip_linear = frac != 0 && mip_filter == 2u;
        if (mip_filter == 1u && frac >= 8) {
            base++;
        }
    }
    if (!mip_linear) {
        return sample_mip(d, arena, m, s, t, base, linear);
    }
    const uint4 a = sample_mip(d, arena, m, s, t, base, linear);
    const uint4 b = sample_mip(d, arena, m, s, t, base + 1u, linear);
    return (a * uint(16 - frac) + b * uint(frac)) >> 4;
}

// ---- LOD (gx_raster.c: fixed_log2, calc_lod) ----
static int fixed_log2(float f) {
    const uint x = as_type<uint>(f);
    return int((x & 0x7F800000u) >> 19) - 2032 + int((x & 0x007FFFFFu) >> 19);
}

static int calc_lod(uint m0, uint m1, float2 ddx, float2 ddy, thread bool &linear) {
    const float dudx = fabs(ddx.x);
    const float dvdx = fabs(ddx.y);
    const float dudy = fabs(ddy.x);
    const float dvdy = fabs(ddy.y);
    float sd;
    float td;
    if (((m0 >> 8) & 1u) != 0u) {
        sd = dudx + dudy;
        td = dvdx + dvdy;
    } else {
        sd = max(dudx, dudy);
        td = max(dvdx, dvdy);
    }
    int lod = fixed_log2(max(sd, td));
    lod += (int(m0 << 15) >> 24) >> 1;
    linear = (lod > 0 && ((m0 >> 7) & 1u) == 1u) || (lod <= 0 && ((m0 >> 4) & 1u) == 1u);
    const int max_lod = int((m1 >> 8) & 0xFFu);
    const int min_lod = int(m1 & 0xFFu);
    if (lod > max_lod) {
        lod = max_lod;
    } else if (lod < min_lod) {
        lod = min_lod;
    }
    return lod;
}

// ---- TEV (gx_tev.c) ----
constant int KONST_FIXED[8] = {255, 223, 191, 159, 128, 96, 64, 32};
constant int BIAS[4] = {0, 128, -128, 0};
constant int LSHIFT[4] = {0, 1, 2, 0};
constant int RSHIFT[4] = {0, 0, 0, 1};

static int combine(uint env, int a, int b, int c, int dd, bool alpha) {
    const uint bias = (env >> 16) & 3u;
    const bool sub = ((env >> 18) & 1u) != 0u;
    const uint scale = (env >> 20) & 3u;
    a &= 0xFF;
    b &= 0xFF;
    c &= 0xFF;
    dd = sign11(uint(dd));
    const int cc = c + (c >> 7);
    int temp = (a * (256 - cc)) + (b * cc);
    temp <<= LSHIFT[scale];
    if (scale != 3u) {
        temp += sub ? 127 : 128;
    }
    if (alpha) {
        temp = sub ? (-temp >> 8) : (temp >> 8);
    } else {
        temp >>= 8;
        temp = sub ? -temp : temp;
    }
    const int result = ((dd + BIAS[bias]) << LSHIFT[scale]) + temp;
    return result >> RSHIFT[scale];
}

static bool tev_compare(uint env, int4 a, int4 b, uint ch) {
    const uint mode = (env >> 20) & 3u;
    uint va;
    uint vb;
    if (mode == 0u) {
        va = uint(a.x) & 0xFFu;
        vb = uint(b.x) & 0xFFu;
    } else if (mode == 1u) {
        va = ((uint(a.y) & 0xFFu) << 8) | (uint(a.x) & 0xFFu);
        vb = ((uint(b.y) & 0xFFu) << 8) | (uint(b.x) & 0xFFu);
    } else if (mode == 2u) {
        va = ((uint(a.z) & 0xFFu) << 16) | ((uint(a.y) & 0xFFu) << 8) | (uint(a.x) & 0xFFu);
        vb = ((uint(b.z) & 0xFFu) << 16) | ((uint(b.y) & 0xFFu) << 8) | (uint(b.x) & 0xFFu);
    } else {
        va = uint(a[ch]) & 0xFFu;
        vb = uint(b[ch]) & 0xFFu;
    }
    return ((env >> 18) & 1u) != 0u ? va == vb : va > vb;
}

static int clamp_out(int v, bool c) { return c ? clamp(v, 0, 255) : clamp(v, -1024, 1023); }

static bool alpha_cmp(int alpha, int ref, uint comp) {
    switch (comp) {
    case 0u: return false;
    case 1u: return alpha < ref;
    case 2u: return alpha == ref;
    case 3u: return alpha <= ref;
    case 4u: return alpha > ref;
    case 5u: return alpha != ref;
    case 6u: return alpha >= ref;
    default: return true;
    }
}

static bool alpha_test(uint t, int alpha) {
    const bool c0 = alpha_cmp(alpha, int(t & 0xFFu), (t >> 16) & 7u);
    const bool c1 = alpha_cmp(alpha, int((t >> 8) & 0xFFu), (t >> 19) & 7u);
    switch ((t >> 22) & 3u) {
    case 0u: return c0 && c1;
    case 1u: return c0 || c1;
    case 2u: return c0 != c1;
    default: return c0 == c1;
    }
}

static uint3 apply_fog(constant DrawState &d, int px, int z, uint3 c) {
    const uint fsel = (d.fog3 >> 21) & 7u;
    float ze;
    if (((d.fog3 >> 20) & 1u) == 0u) {
        const int denom = int(d.fog_bmag) - (z >> d.fog_bshift);
        ze = (d.fog_a * 16777215.0) / float(denom);
    } else {
        ze = d.fog_a * (float(z) / 16777215.0);
    }
    if (((d.fog_range[0] >> 10) & 1u) != 0u) {
        const float offset = (float(px) - (float(d.fog_range[0] & 0x3FFu) - 342.0)) / d.vp_wd;
        const float fi = clamp(9.0 - (fabs(offset) * 9.0), 0.0, 9.0);
        const int lo = int(fi);
        const int hi = lo + 1;
        const uint klo_reg = d.fog_range[1 + (lo / 2)];
        const uint khi_reg = d.fog_range[1 + ((hi > 9 ? 9 : hi) / 2)];
        const float klo = float((lo % 2) != 0 ? (klo_reg & 0xFFFu) : (klo_reg >> 12) & 0xFFFu) / 256.0 * 4.0;
        const float khi = float((hi % 2) != 0 ? (khi_reg & 0xFFFu) : (khi_reg >> 12) & 0xFFFu) / 256.0 * 4.0;
        const float factor = float(hi) - fi;
        const float k = (klo * factor) + (khi * (1.0 - factor));
        ze *= sqrt((offset * offset) + (k * k)) / k;
    }
    ze -= d.fog_c;
    float fog = clamp(ze, 0.0, 1.0);
    if (fsel == 4u) {
        fog = 1.0 - exp2(-8.0 * fog);
    } else if (fsel == 5u) {
        fog = 1.0 - exp2(-8.0 * fog * fog);
    } else if (fsel == 6u) {
        fog = exp2(-8.0 * (1.0 - fog));
    } else if (fsel == 7u) {
        fog = exp2(-8.0 * (1.0 - fog) * (1.0 - fog));
    }
    const uint f = uint(fog * 256.0);
    const uint inv = 256u - f;
    const uint3 rgb = uint3((d.fog_color >> 16) & 0xFFu, (d.fog_color >> 8) & 0xFFu, d.fog_color & 0xFFu);
    return ((c * inv) + (f * rgb)) >> 8;
}

// ---- Pixel Engine (gx_efb.c) ----
static uint expand6(uint v) { return (v << 2) | (v >> 4); }

static uint4 efb_unpack(uint format, uint v) {
    if (format == 1u) {
        return uint4(expand6((v >> 18) & 63u), expand6((v >> 12) & 63u), expand6((v >> 6) & 63u), expand6(v & 63u));
    }
    return uint4((v >> 16) & 0xFFu, (v >> 8) & 0xFFu, v & 0xFFu, 0xFFu);
}

static uint efb_store(uint format, uint v, uint4 c, bool color, bool alpha) {
    if (format == 1u) {
        if (color) {
            v = (v & 0x3Fu) | ((c.r >> 2) << 18) | ((c.g >> 2) << 12) | ((c.b >> 2) << 6);
        }
        if (alpha) {
            v = (v & ~0x3Fu) | (c.a >> 2);
        }
    } else if (color) {
        v = (c.r << 16) | (c.g << 8) | c.b;
    }
    return v & 0xFFFFFFu;
}

static bool z_compare(uint func, uint z, uint dz) {
    switch (func) {
    case 0u: return false;
    case 1u: return z < dz;
    case 2u: return z == dz;
    case 3u: return z <= dz;
    case 4u: return z > dz;
    case 5u: return z != dz;
    case 6u: return z >= dz;
    default: return true;
    }
}

static uint blend_factor(uint mode, bool src_side, uint4 src, uint4 dst, uint ch) {
    switch (mode) {
    case 0u: return 0u;
    case 1u: return 255u;
    case 2u: return src_side ? dst[ch] : src[ch];
    case 3u: return 255u - (src_side ? dst[ch] : src[ch]);
    case 4u: return src.a;
    case 5u: return 255u - src.a;
    case 6u: return dst.a;
    default: return 255u - dst.a;
    }
}

static uint logic_op(uint op, uint s, uint dd) {
    switch (op) {
    case 0u: return 0u;
    case 1u: return s & dd;
    case 2u: return s & ~dd;
    case 3u: return s;
    case 4u: return ~s & dd;
    case 5u: return dd;
    case 6u: return s ^ dd;
    case 7u: return s | dd;
    case 8u: return ~(s | dd);
    case 9u: return ~(s ^ dd);
    case 10u: return ~dd;
    case 11u: return s | ~dd;
    case 12u: return ~s;
    case 13u: return ~s | dd;
    case 14u: return ~(s & dd);
    default: return 0xFFFFFFFFu;
    }
}

static uint efb_blend(constant DrawState &d, uint2 p, uint dst_packed, uint4 src) {
    const uint format = d.pe_control & 7u;
    const uint mode = d.blend;
    uint4 dst = efb_unpack(format, dst_packed);
    if ((mode & 1u) != 0u) {
        if (((mode >> 11) & 1u) != 0u) {
            dst = uint4(select(uint4(0), dst - src, dst > src));
        } else {
            const uint df_mode = (mode >> 5) & 7u;
            const uint sf_mode = (mode >> 8) & 7u;
            uint4 o;
            for (uint i = 0; i < 4u; i++) {
                uint sf = blend_factor(sf_mode, true, src, dst, i);
                uint df = blend_factor(df_mode, false, src, dst, i);
                sf += sf >> 7;
                df += df >> 7;
                o[i] = min((src[i] * sf + dst[i] * df) >> 8, 255u);
            }
            dst = o;
        }
    } else if (((mode >> 1) & 1u) != 0u) {
        for (uint i = 0; i < 4u; i++) {
            dst[i] = logic_op((mode >> 12) & 15u, src[i], dst[i]) & 0xFFu;
        }
    } else {
        dst = src;
    }
    if (((d.dst_alpha >> 8) & 1u) != 0u) {
        dst.a = d.dst_alpha & 0xFFu;
    }
    const bool color = ((mode >> 3) & 1u) != 0u;
    const bool alpha = ((mode >> 4) & 1u) != 0u;
    if (color && ((mode >> 2) & 1u) != 0u && format == 1u) {
        const uint dither[4] = {0u, 2u, 3u, 1u}; // Bayer 2x2: [y & 1][x & 1]
        const uint k = dither[((p.y & 1u) << 1) | (p.x & 1u)];
        for (uint c = 0; c < 3u; c++) {
            dst[c] = ((dst[c] - (dst[c] >> 6)) + k) & 0xFCu;
        }
    }
    return (color || alpha) ? efb_store(format, dst_packed, dst, color, alpha) : dst_packed;
}

static int z_texture(constant DrawState &d, int z, int4 raw) {
    const uint op = (d.ztex2 >> 2) & 3u;
    if (op == 0u) {
        return z;
    }
    uint ztex = d.ztex_bias;
    const uint fmt = d.ztex2 & 3u;
    if (fmt == 0u) {
        ztex += uint(raw.a);
    } else if (fmt == 1u) {
        ztex += (uint(raw.a) << 8) | uint(raw.r);
    } else {
        ztex += (uint(raw.r) << 16) | (uint(raw.g) << 8) | uint(raw.b);
    }
    if (op == 1u) {
        ztex += uint(z);
    }
    return int(ztex & 0xFFFFFFu);
}

static int4 konst_color(constant DrawState &d, uint kc, uint ka) {
    int4 k = int4(0);
    if (kc < 8u) {
        k.rgb = int3(KONST_FIXED[kc]);
    } else if (kc >= 12u && kc < 16u) {
        const uint n = kc - 12u;
        k.rgb = int3(d.konst[n * 4u], d.konst[n * 4u + 1u], d.konst[n * 4u + 2u]);
    } else if (kc >= 16u) {
        const uint c = (kc - 16u) >> 2;
        const uint n = (kc - 16u) & 3u;
        k.rgb = int3(d.konst[n * 4u + c]);
    }
    if (ka < 8u) {
        k.a = KONST_FIXED[ka];
    } else if (ka >= 16u) {
        const uint c = (ka - 16u) >> 2;
        const uint n = (ka - 16u) & 3u;
        k.a = d.konst[n * 4u + c];
    }
    return k;
}

static uint swap_sel(constant DrawState &d, uint table, uint c) {
    const uint reg = d.ksel[table * 2u + (c >> 1)];
    return (reg >> ((c & 1u) * 2u)) & 3u;
}

fragment FOut gx_fs(VOut in [[stage_in]], uint dst_c [[color(0)]], uint dst_z [[color(1)]],
                    constant DrawState &d [[buffer(0)]], device const uint *arena [[buffer(1)]]) {
    FOut o;
    o.color = dst_c;
    o.depth = dst_z;
    if ((d.flags & 1u) != 0u) { // limpeza da cópia (ClearEfb): cor em regs, Z em ztex_bias, máscaras em blend/zmode
        const bool color = ((d.blend >> 3) & 1u) != 0u;
        const bool alpha = ((d.blend >> 4) & 1u) != 0u;
        const uint4 c = uint4(uint(d.regs[0]), uint(d.regs[1]), uint(d.regs[2]), uint(d.regs[3]));
        o.color = (color || alpha) ? efb_store(d.pe_control & 7u, dst_c, c, color, alpha) : dst_c;
        o.depth = ((d.zmode >> 4) & 1u) != 0u ? d.ztex_bias : dst_z;
        return o;
    }
    const uint2 p = uint2(in.position.xy);
    // Coordenadas de textura em texels (com a escala do SU já aplicada na CPU) e o LOD do bloco 2x2: as derivadas
    // saem antes de qualquer desvio que dependa do pixel.
    float2 uv[8];
    const float3 tc[8] = {in.t0, in.t1, in.t2, in.t3, in.t4, in.t5, in.t6, in.t7};
    float2 ddx[8];
    float2 ddy[8];
    for (uint i = 0; i < 8u; i++) {
        uv[i] = tc[i].z != 0.0 ? tc[i].xy / tc[i].z : tc[i].xy;
        ddx[i] = dfdx(uv[i]);
        ddy[i] = dfdy(uv[i]);
    }
    const int z0 = int(clamp(in.z, 0.0, 16777215.0));
    const uint zfunc = (d.zmode >> 1) & 7u;
    const bool zupdate = ((d.zmode >> 4) & 1u) != 0u;
    const bool ztest = (d.zmode & 1u) != 0u;
    const bool early = ztest && ((d.pe_control >> 6) & 1u) != 0u;
    if (early) {
        if (!z_compare(zfunc, uint(z0), dst_z)) {
            return o;
        }
        if (zupdate) {
            o.depth = uint(z0) & 0xFFFFFFu;
        }
    }
    int4 col[2] = {int4(0), int4(0)};
    if (d.colchans > 0u) {
        col[0] = int4(clamp(in.c0, 0.0, 255.0));
    }
    if (d.colchans > 1u) {
        col[1] = int4(clamp(in.c1, 0.0, 255.0));
    }
    int4 v[10];
    for (uint n = 0; n < 4u; n++) {
        v[n] = int4(d.regs[n * 4u], d.regs[n * 4u + 1u], d.regs[n * 4u + 2u], d.regs[n * 4u + 3u]);
    }
    v[4] = int4(0);
    v[5] = int4(0);
    v[6] = int4(0);
    v[7] = int4(255);
    v[8] = int4(128);
    v[9] = int4(0);
    int4 raw = int4(0);
    for (uint i = 0; i < d.stages && i < STAGES; i++) {
        const uint cc = d.cc[i];
        const uint ac = d.ac[i];
        const uint ord = d.order[i];
        const uint texmap = ord & 7u;
        uint texcoord = (ord >> 3) & 7u;
        if (texcoord >= d.texgens) {
            texcoord = 0u;
        }
        if (((ord >> 6) & 1u) != 0u) {
            uint4 texel = uint4(0);
            if (d.texgens > 0u) {
                bool linear = false;
                const int lod = calc_lod(d.tex_mode0[texmap], d.tex_mode1[texmap], ddx[texcoord], ddy[texcoord], linear);
                const int s = sign24(int(uv[texcoord].x * 128.0));
                const int t = sign24(int(uv[texcoord].y * 128.0));
                texel = sample_tex(d, arena, texmap, s, t, lod, linear);
            }
            raw = int4(texel);
            const uint sw = (ac >> 2) & 3u;
            v[4] = int4(texel[swap_sel(d, sw, 0u)], texel[swap_sel(d, sw, 1u)], texel[swap_sel(d, sw, 2u)],
                        texel[swap_sel(d, sw, 3u)]);
        }
        const uint ksel = d.ksel[i >> 1];
        const uint kc = (i & 1u) != 0u ? (ksel >> 14) & 31u : (ksel >> 4) & 31u;
        const uint ka = (i & 1u) != 0u ? (ksel >> 19) & 31u : (ksel >> 9) & 31u;
        v[6] = konst_color(d, kc, ka);
        const uint chan = (ord >> 7) & 7u;
        if (chan <= 1u) {
            const uint rs = ac & 3u;
            const int4 c = col[chan];
            v[5] = int4(c[swap_sel(d, rs, 0u)], c[swap_sel(d, rs, 1u)], c[swap_sel(d, rs, 2u)], c[swap_sel(d, rs, 3u)]);
        } else {
            v[5] = int4(0); // 5 e 6 = bump do indireto (sem estágios indiretos na GPU: zero); 7 = zero
        }
        // Seletores (decode_selectors) e combinação (run_combiners).
        int4 args[4];
        for (uint k = 0; k < 4u; k++) {
            const uint arg = (cc >> (12u - (4u * k))) & 15u;
            int3 rgb;
            if ((arg >> 1) < 6u) {
                const int4 src = v[arg >> 1];
                rgb = (arg & 1u) != 0u ? int3(src.a) : src.rgb;
            } else {
                const uint cs[4] = {7u, 8u, 6u, 9u};
                rgb = v[cs[arg - 12u]].rgb;
            }
            const uint aarg = (ac >> (13u - (3u * k))) & 7u;
            args[k] = int4(rgb, v[aarg == 7u ? 9u : aarg].a);
        }
        int4 outv;
        const bool ccmp = ((cc >> 16) & 3u) == 3u;
        for (uint ch = 0; ch < 3u; ch++) {
            outv[ch] = ccmp ? sign11(uint(args[3][ch])) + (tev_compare(cc, args[0], args[1], ch) ? (args[2][ch] & 0xFF) : 0)
                            : combine(cc, args[0][ch], args[1][ch], args[2][ch], args[3][ch], false);
        }
        const bool acmp = ((ac >> 16) & 3u) == 3u;
        outv.a = acmp ? sign11(uint(args[3].a)) + (tev_compare(ac, args[0], args[1], 3u) ? (args[2].a & 0xFF) : 0)
                      : combine(ac, args[0].a, args[1].a, args[2].a, args[3].a, true);
        const uint cdest = (cc >> 22) & 3u;
        const uint adest = (ac >> 22) & 3u;
        const bool cclamp = ((cc >> 19) & 1u) != 0u;
        const bool aclamp = ((ac >> 19) & 1u) != 0u;
        v[cdest].rgb = int3(clamp_out(outv.r, cclamp), clamp_out(outv.g, cclamp), clamp_out(outv.b, cclamp));
        v[adest].a = clamp_out(outv.a, aclamp);
    }
    const uint last = clamp(d.stages, 1u, STAGES) - 1u;
    const uint cdest = (d.cc[last] >> 22) & 3u;
    const uint adest = (d.ac[last] >> 22) & 3u;
    uint4 px = uint4(uint(v[cdest].r) & 0xFFu, uint(v[cdest].g) & 0xFFu, uint(v[cdest].b) & 0xFFu,
                     uint(v[adest].a) & 0xFFu);
    if (!alpha_test(d.alpha_test, int(px.a))) {
        return o;
    }
    const int z = z_texture(d, z0, raw);
    if (((d.fog3 >> 21) & 7u) != 0u) {
        px.rgb = apply_fog(d, int(p.x), z, px.rgb);
    }
    if (ztest && !early) {
        if (!z_compare(zfunc, uint(z), dst_z)) {
            return o;
        }
        if (zupdate) {
            o.depth = uint(z) & 0xFFFFFFu;
        }
    }
    o.color = efb_blend(d, p, dst_c, px);
    return o;
}
