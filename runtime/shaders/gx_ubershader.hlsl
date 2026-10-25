// SPDX-License-Identifier: GPL-3.0-or-later
// M9: o ubershader do GX em HLSL, fonte único para D3D12 (DXIL), Vulkan (SPIR-V) e Metal (MSL 2.3), compilado
// offline com o SDL_shadercross (scripts/build_shaders.sh). É a tradução de gx_ubershader.metal (M8), com a mesma
// aritmética inteira: TEV (gfx/gx_tev.c), amostragem em s17.7 com wrap e filtro bilinear (gx_tex.c), LOD pelas
// derivadas (gx_raster.c), teste de alfa, textura de Z, neblina, teste de Z e blend/lógica/dither do Pixel Engine
// (gx_efb.c). A diferença está no EFB: em vez dos dois color targets lidos por framebuffer fetch (só GPU da Apple),
// são duas texturas R32_UINT de storage graváveis, declaradas como RasterizerOrderedTexture2D. O compilador
// transforma o ROV em ROV do D3D12, em VK_EXT_fragment_shader_interlock no SPIR-V e em raster_order_group(0) no MSL:
// os fragmentos que se sobrepõem leem e gravam o EFB na ordem da API, como no hardware (docs/M9_PARTE1B_HLSL.md).
//
// Registros (layout da SDL3 GPU com o patch de patches/sdl, docs/M9_PARTE1B_SDL.md §2):
//   uniform do desenho     cbuffer b0, space3            SPIR-V set 3 binding 0   MSL [[buffer(0)]]
//   texels decodificados   ByteAddressBuffer t0, space2  SPIR-V set 2 binding 0   MSL [[buffer(1)]]
//   EFB cor / Z            RasterizerOrdered u0/u1,      SPIR-V set 2 binding 1/2 MSL [[texture(0)]]/[[texture(1)]]
//                          space2
// O SDL_shadercross numera as texturas do MSL pelo binding do SPIR-V e põe os storage buffers depois delas; o patch
// da SDL põe as graváveis depois dos storage buffers. Por isso o MSL é gerado com -DAM_MSL_LAYOUT, que só troca os
// bindings do SPIR-V intermediário (EFB em 0 e 1, texels em 2) para o MSL sair com os índices da tabela.
//
// Vértices: TEXCOORD0..10 = location 0..10, os mesmos atributos do pipeline em platform/platform_gpu.c.

static const uint STAGES = 16;

#ifdef AM_MSL_LAYOUT
#define AM_BIND_EFB_COLOR 0
#define AM_BIND_EFB_DEPTH 1
#define AM_BIND_ARENA     2
#else
#define AM_BIND_ARENA     0
#define AM_BIND_EFB_COLOR 1
#define AM_BIND_EFB_DEPTH 2
#endif

// GpuDrawUniforms (am/gpu_batch.h), 624 bytes na mesma ordem. Os vetores de 32 bits do C viram uint4 porque um
// elemento de array num cbuffer ocupa 16 bytes no HLSL: uint4 x[n] tem o mesmo layout que u32 x[4n] no C, em DXIL e
// em SPIR-V. O elemento i está em x[i >> 2][i & 3] (funções at4 abaixo).
cbuffer DrawState : register(b0, space3) {
    uint d_stages, d_texgens, d_ind_stages, d_colchans;
    uint d_zmode, d_pe_control, d_blend, d_dst_alpha;
    uint d_alpha_test, d_fog3, d_fog_color, d_fog_bmag;
    uint d_fog_bshift, d_ztex2, d_ztex_bias, d_flags;
    float d_fog_a, d_fog_c, d_vp_wd, d_pad0;
    uint4 d_fog_range[2];
    uint4 d_cc[4];
    uint4 d_ac[4];
    uint4 d_order[4];
    uint4 d_ksel[2];
    int4 d_regs[4];
    int4 d_konst[4];
    uint4 d_tex_size[2];
    uint4 d_tex_mode0[2];
    uint4 d_tex_mode1[2];
    uint4 d_tex_levels[2];
    uint4 d_tex_base[2];
};

[[vk::binding(AM_BIND_ARENA, 2)]] ByteAddressBuffer arena : register(t0, space2);
[[vk::binding(AM_BIND_EFB_COLOR, 2)]] [[vk::image_format("r32ui")]] RasterizerOrderedTexture2D<uint> efb_color
    : register(u0, space2);
[[vk::binding(AM_BIND_EFB_DEPTH, 2)]] [[vk::image_format("r32ui")]] RasterizerOrderedTexture2D<uint> efb_depth
    : register(u1, space2);

struct VIn {
    float4 pos : TEXCOORD0; // x, y em pixels do EFB; z de 24 bits; w de recorte
    float4 c0 : TEXCOORD1;
    float4 c1 : TEXCOORD2;
    float4 t0 : TEXCOORD3;
    float4 t1 : TEXCOORD4;
    float4 t2 : TEXCOORD5;
    float4 t3 : TEXCOORD6;
    float4 t4 : TEXCOORD7;
    float4 t5 : TEXCOORD8;
    float4 t6 : TEXCOORD9;
    float4 t7 : TEXCOORD10;
};

struct VOut {
    float4 position : SV_Position;
    noperspective float z : TEXCOORD0;
    noperspective float4 c0 : TEXCOORD1; // o rasterizador do GX interpola cor sem correção de perspectiva
    noperspective float4 c1 : TEXCOORD2;
    float3 t0 : TEXCOORD3;
    float3 t1 : TEXCOORD4;
    float3 t2 : TEXCOORD5;
    float3 t3 : TEXCOORD6;
    float3 t4 : TEXCOORD7;
    float3 t5 : TEXCOORD8;
    float3 t6 : TEXCOORD9;
    float3 t7 : TEXCOORD10;
};

VOut gx_vs(VIn v) {
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

static uint at4(uint4 x[2], uint i) { return x[i >> 2][i & 3u]; }
static uint cc_of(uint i) { return d_cc[i >> 2][i & 3u]; }
static uint ac_of(uint i) { return d_ac[i >> 2][i & 3u]; }
static uint order_of(uint i) { return d_order[i >> 2][i & 3u]; }
static int konst_of(uint i) { return d_konst[i >> 2][i & 3u]; }

static int sign11(uint v) { return asint(v << 21) >> 21; }
static int sign24(int v) { return asint(asuint(v) << 8) >> 8; }

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

static uint texel_at(uint i) { return arena.Load(i * 4u); }

static uint4 sample_mip(uint m, int s, int t, uint mip, bool use_bilinear) {
    const uint wm1_0 = at4(d_tex_size, m) & 0xFFFFu;
    const uint hm1_0 = at4(d_tex_size, m) >> 16;
    if (mip >= at4(d_tex_levels, m)) {
        return uint4(0, 0, 0, 0);
    }
    uint base = at4(d_tex_base, m);
    for (uint l = 0; l < mip; l++) {
        base += ((wm1_0 >> l) + 1u) * ((hm1_0 >> l) + 1u);
    }
    const int w = int(wm1_0 >> mip) + 1;
    const int h = int(hm1_0 >> mip) + 1;
    const uint mode0 = at4(d_tex_mode0, m);
    const uint ws = mode0 & 3u;
    const uint wt = (mode0 >> 2) & 3u;
    if (!use_bilinear) {
        return unpack_rgba(texel_at(base + uint(wrap_coord(t >> 7, wt, h) * w + wrap_coord(s >> 7, ws, w))));
    }
    s -= 64;
    t -= 64;
    const int s0 = wrap_coord(s >> 7, ws, w);
    const int t0 = wrap_coord(t >> 7, wt, h);
    const int s1 = wrap_coord((s >> 7) + 1, ws, w);
    const int t1 = wrap_coord((t >> 7) + 1, wt, h);
    const uint fs = asuint(s) & 0x7Fu;
    const uint ft = asuint(t) & 0x7Fu;
    const uint4 a = unpack_rgba(texel_at(base + uint(t0 * w + s0)));
    const uint4 b = unpack_rgba(texel_at(base + uint(t0 * w + s1)));
    const uint4 c = unpack_rgba(texel_at(base + uint(t1 * w + s0)));
    const uint4 e = unpack_rgba(texel_at(base + uint(t1 * w + s1)));
    const uint4 v = a * (128u - fs) * (128u - ft) + b * fs * (128u - ft) + c * (128u - fs) * ft + e * fs * ft;
    return v >> 14;
}

static uint4 sample_tex(uint m, int s, int t, int lod, bool use_bilinear) {
    const uint mip_filter = (at4(d_tex_mode0, m) >> 5) & 3u;
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
        return sample_mip(m, s, t, base, use_bilinear);
    }
    const uint4 a = sample_mip(m, s, t, base, use_bilinear);
    const uint4 b = sample_mip(m, s, t, base + 1u, use_bilinear);
    return (a * uint(16 - frac) + b * uint(frac)) >> 4;
}

// ---- LOD (gx_raster.c: fixed_log2, calc_lod) ----
static int fixed_log2(float f) {
    const uint x = asuint(f);
    return int((x & 0x7F800000u) >> 19) - 2032 + int((x & 0x007FFFFFu) >> 19);
}

static int calc_lod(uint m0, uint m1, float2 dx, float2 dy, out bool use_bilinear) {
    const float dudx = abs(dx.x);
    const float dvdx = abs(dx.y);
    const float dudy = abs(dy.x);
    const float dvdy = abs(dy.y);
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
    lod += (asint(m0 << 15) >> 24) >> 1;
    use_bilinear = (lod > 0 && ((m0 >> 7) & 1u) == 1u) || (lod <= 0 && ((m0 >> 4) & 1u) == 1u);
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
static const int KONST_FIXED[8] = {255, 223, 191, 159, 128, 96, 64, 32};
static const int BIAS[4] = {0, 128, -128, 0};
static const int LSHIFT[4] = {0, 1, 2, 0};
static const int RSHIFT[4] = {0, 0, 0, 1};

static int combine(uint env, int a, int b, int c, int dd, bool alpha) {
    const uint bias = (env >> 16) & 3u;
    const bool sub = ((env >> 18) & 1u) != 0u;
    const uint scale = (env >> 20) & 3u;
    a &= 0xFF;
    b &= 0xFF;
    c &= 0xFF;
    dd = sign11(asuint(dd));
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
        va = asuint(a.x) & 0xFFu;
        vb = asuint(b.x) & 0xFFu;
    } else if (mode == 1u) {
        va = ((asuint(a.y) & 0xFFu) << 8) | (asuint(a.x) & 0xFFu);
        vb = ((asuint(b.y) & 0xFFu) << 8) | (asuint(b.x) & 0xFFu);
    } else if (mode == 2u) {
        va = ((asuint(a.z) & 0xFFu) << 16) | ((asuint(a.y) & 0xFFu) << 8) | (asuint(a.x) & 0xFFu);
        vb = ((asuint(b.z) & 0xFFu) << 16) | ((asuint(b.y) & 0xFFu) << 8) | (asuint(b.x) & 0xFFu);
    } else {
        va = asuint(a[ch]) & 0xFFu;
        vb = asuint(b[ch]) & 0xFFu;
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

static uint fog_range_of(uint i) { return d_fog_range[i >> 2][i & 3u]; }

static uint3 apply_fog(int px, int z, uint3 c) {
    const uint fsel = (d_fog3 >> 21) & 7u;
    float ze;
    if (((d_fog3 >> 20) & 1u) == 0u) {
        const int denom = int(d_fog_bmag) - (z >> d_fog_bshift);
        ze = (d_fog_a * 16777215.0) / float(denom);
    } else {
        ze = d_fog_a * (float(z) / 16777215.0);
    }
    const uint range0 = fog_range_of(0u);
    if (((range0 >> 10) & 1u) != 0u) {
        const float offset = (float(px) - (float(range0 & 0x3FFu) - 342.0)) / d_vp_wd;
        const float fi = clamp(9.0 - (abs(offset) * 9.0), 0.0, 9.0);
        const int lo = int(fi);
        const int hi = lo + 1;
        const uint klo_reg = fog_range_of(uint(1 + (lo / 2)));
        const uint khi_reg = fog_range_of(uint(1 + ((hi > 9 ? 9 : hi) / 2)));
        const float klo = float((lo % 2) != 0 ? (klo_reg & 0xFFFu) : (klo_reg >> 12) & 0xFFFu) / 256.0 * 4.0;
        const float khi = float((hi % 2) != 0 ? (khi_reg & 0xFFFu) : (khi_reg >> 12) & 0xFFFu) / 256.0 * 4.0;
        const float factor = float(hi) - fi;
        const float k = (klo * factor) + (khi * (1.0 - factor));
        ze *= sqrt((offset * offset) + (k * k)) / k;
    }
    ze -= d_fog_c;
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
    const uint3 rgb = uint3((d_fog_color >> 16) & 0xFFu, (d_fog_color >> 8) & 0xFFu, d_fog_color & 0xFFu);
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

static const uint DITHER[4] = {0u, 2u, 3u, 1u}; // Bayer 2x2: [y & 1][x & 1]

static uint efb_blend(uint2 p, uint dst_packed, uint4 src) {
    const uint format = d_pe_control & 7u;
    const uint mode = d_blend;
    uint4 dst = efb_unpack(format, dst_packed);
    if ((mode & 1u) != 0u) {
        if (((mode >> 11) & 1u) != 0u) {
            for (uint i = 0; i < 4u; i++) { // subtração: dst > src ? dst - src : 0, canal a canal
                dst[i] = dst[i] > src[i] ? dst[i] - src[i] : 0u;
            }
        } else {
            const uint df_mode = (mode >> 5) & 7u;
            const uint sf_mode = (mode >> 8) & 7u;
            uint4 o = uint4(0, 0, 0, 0);
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
    if (((d_dst_alpha >> 8) & 1u) != 0u) {
        dst.a = d_dst_alpha & 0xFFu;
    }
    const bool color = ((mode >> 3) & 1u) != 0u;
    const bool alpha = ((mode >> 4) & 1u) != 0u;
    if (color && ((mode >> 2) & 1u) != 0u && format == 1u) {
        const uint k = DITHER[((p.y & 1u) << 1) | (p.x & 1u)];
        for (uint c = 0; c < 3u; c++) {
            dst[c] = ((dst[c] - (dst[c] >> 6)) + k) & 0xFCu;
        }
    }
    return (color || alpha) ? efb_store(format, dst_packed, dst, color, alpha) : dst_packed;
}

static int z_texture(int z, int4 raw) {
    const uint op = (d_ztex2 >> 2) & 3u;
    if (op == 0u) {
        return z;
    }
    uint ztex = d_ztex_bias;
    const uint fmt = d_ztex2 & 3u;
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

static int4 konst_color(uint kc, uint ka) {
    int4 k = int4(0, 0, 0, 0);
    if (kc < 8u) {
        k.rgb = int3(KONST_FIXED[kc], KONST_FIXED[kc], KONST_FIXED[kc]);
    } else if (kc >= 12u && kc < 16u) {
        k.rgb = d_konst[kc - 12u].rgb;
    } else if (kc >= 16u) {
        const uint c = (kc - 16u) >> 2;
        const uint n = (kc - 16u) & 3u;
        const int kv = konst_of(n * 4u + c);
        k.rgb = int3(kv, kv, kv);
    }
    if (ka < 8u) {
        k.a = KONST_FIXED[ka];
    } else if (ka >= 16u) {
        const uint c = (ka - 16u) >> 2;
        const uint n = (ka - 16u) & 3u;
        k.a = konst_of(n * 4u + c);
    }
    return k;
}

static uint swap_sel(uint table, uint c) {
    const uint reg = at4(d_ksel, table * 2u + (c >> 1));
    return (reg >> ((c & 1u) * 2u)) & 3u;
}

static int4 swizzle(int4 c, uint table) {
    return int4(c[swap_sel(table, 0u)], c[swap_sel(table, 1u)], c[swap_sel(table, 2u)], c[swap_sel(table, 3u)]);
}

// TEV completo (decode_selectors e run_combiners de gx_tev.c): devolve a cor final e, em raw, o último texel lido
// (para a textura de Z).
static uint4 run_tev(VOut i, float2 uv[8], float2 dx[8], float2 dy[8], out int4 raw) {
    int4 col[2] = {int4(0, 0, 0, 0), int4(0, 0, 0, 0)};
    if (d_colchans > 0u) {
        col[0] = int4(clamp(i.c0, 0.0, 255.0));
    }
    if (d_colchans > 1u) {
        col[1] = int4(clamp(i.c1, 0.0, 255.0));
    }
    int4 v[10];
    for (uint n = 0; n < 4u; n++) {
        v[n] = d_regs[n];
    }
    v[4] = int4(0, 0, 0, 0);
    v[5] = int4(0, 0, 0, 0);
    v[6] = int4(0, 0, 0, 0);
    v[7] = int4(255, 255, 255, 255);
    v[8] = int4(128, 128, 128, 128);
    v[9] = int4(0, 0, 0, 0);
    raw = int4(0, 0, 0, 0);
    for (uint st = 0; st < d_stages && st < STAGES; st++) {
        const uint cc = cc_of(st);
        const uint ac = ac_of(st);
        const uint ord = order_of(st);
        const uint texmap = ord & 7u;
        uint texcoord = (ord >> 3) & 7u;
        if (texcoord >= d_texgens) {
            texcoord = 0u;
        }
        if (((ord >> 6) & 1u) != 0u) {
            uint4 texel = uint4(0, 0, 0, 0);
            if (d_texgens > 0u) {
                bool use_bilinear = false;
                const int lod = calc_lod(at4(d_tex_mode0, texmap), at4(d_tex_mode1, texmap), dx[texcoord],
                                         dy[texcoord], use_bilinear);
                const int s = sign24(int(uv[texcoord].x * 128.0));
                const int t = sign24(int(uv[texcoord].y * 128.0));
                texel = sample_tex(texmap, s, t, lod, use_bilinear);
            }
            raw = int4(texel);
            v[4] = swizzle(int4(texel), (ac >> 2) & 3u);
        }
        const uint ksel = at4(d_ksel, st >> 1);
        const uint kc = (st & 1u) != 0u ? (ksel >> 14) & 31u : (ksel >> 4) & 31u;
        const uint ka = (st & 1u) != 0u ? (ksel >> 19) & 31u : (ksel >> 9) & 31u;
        v[6] = konst_color(kc, ka);
        const uint chan = (ord >> 7) & 7u;
        if (chan <= 1u) {
            v[5] = swizzle(col[chan], ac & 3u);
        } else {
            v[5] = int4(0, 0, 0, 0); // 5 e 6 = bump do indireto (sem estágios indiretos na GPU: zero); 7 = zero
        }
        int4 args[4];
        for (uint k = 0; k < 4u; k++) {
            const uint arg = (cc >> (12u - (4u * k))) & 15u;
            int3 rgb;
            if ((arg >> 1) < 6u) {
                const int4 src = v[arg >> 1];
                rgb = (arg & 1u) != 0u ? int3(src.a, src.a, src.a) : src.rgb;
            } else {
                static const uint CS[4] = {7u, 8u, 6u, 9u};
                rgb = v[CS[arg - 12u]].rgb;
            }
            const uint aarg = (ac >> (13u - (3u * k))) & 7u;
            args[k] = int4(rgb, v[aarg == 7u ? 9u : aarg].a);
        }
        int4 outv = int4(0, 0, 0, 0);
        const bool ccmp = ((cc >> 16) & 3u) == 3u;
        for (uint ch = 0; ch < 3u; ch++) {
            outv[ch] = ccmp ? sign11(asuint(args[3][ch])) + (tev_compare(cc, args[0], args[1], ch) ? (args[2][ch] & 0xFF) : 0)
                            : combine(cc, args[0][ch], args[1][ch], args[2][ch], args[3][ch], false);
        }
        const bool acmp = ((ac >> 16) & 3u) == 3u;
        outv.a = acmp ? sign11(asuint(args[3].a)) + (tev_compare(ac, args[0], args[1], 3u) ? (args[2].a & 0xFF) : 0)
                      : combine(ac, args[0].a, args[1].a, args[2].a, args[3].a, true);
        const uint cdest = (cc >> 22) & 3u;
        const uint adest = (ac >> 22) & 3u;
        const bool cclamp = ((cc >> 19) & 1u) != 0u;
        const bool aclamp = ((ac >> 19) & 1u) != 0u;
        v[cdest].rgb = int3(clamp_out(outv.r, cclamp), clamp_out(outv.g, cclamp), clamp_out(outv.b, cclamp));
        v[adest].a = clamp_out(outv.a, aclamp);
    }
    const uint last = clamp(d_stages, 1u, STAGES) - 1u;
    const uint cdest = (cc_of(last) >> 22) & 3u;
    const uint adest = (ac_of(last) >> 22) & 3u;
    return uint4(asuint(v[cdest].r) & 0xFFu, asuint(v[cdest].g) & 0xFFu, asuint(v[cdest].b) & 0xFFu,
                 asuint(v[adest].a) & 0xFFu);
}

// Resultado do fragmento antes de olhar o EFB: tudo o que não depende dele (TEV, teste de alfa, textura de Z,
// neblina) sai antes da região ordenada.
struct Frag {
    uint4 px;  // cor final, já com a neblina
    uint z0;   // Z interpolado (teste antes da textura)
    uint z;    // Z depois da textura de Z (teste depois)
    bool alpha_ok;
};

static Frag shade(VOut i, uint2 p) {
    // Coordenadas de textura em texels (com a escala do SU já aplicada na CPU) e o LOD do bloco 2x2: as derivadas
    // saem antes de qualquer desvio que dependa do pixel.
    float2 uv[8];
    const float3 tc[8] = {i.t0, i.t1, i.t2, i.t3, i.t4, i.t5, i.t6, i.t7};
    float2 dx[8];
    float2 dy[8];
    for (uint n = 0; n < 8u; n++) {
        uv[n] = tc[n].z != 0.0 ? tc[n].xy / tc[n].z : tc[n].xy;
        dx[n] = ddx(uv[n]);
        dy[n] = ddy(uv[n]);
    }
    Frag f;
    const int z0 = int(clamp(i.z, 0.0, 16777215.0));
    int4 raw;
    f.px = run_tev(i, uv, dx, dy, raw);
    f.alpha_ok = alpha_test(d_alpha_test, int(f.px.a));
    const int z = z_texture(z0, raw);
    if (((d_fog3 >> 21) & 7u) != 0u) {
        f.px.rgb = apply_fog(int(p.x), z, f.px.rgb);
    }
    f.z0 = uint(z0);
    f.z = uint(z);
    return f;
}

// Pixel Engine sobre o valor atual do EFB (dc, dz): devolve os novos em oc, oz. Mesma ordem do backend em software
// (gx_efb.c): Z antes do TEV (zcomploc), teste de alfa, Z depois, blend.
static void pe_resolve(Frag f, uint2 p, uint dc, uint dz, out uint oc, out uint oz) {
    oc = dc;
    oz = dz;
    const uint zfunc = (d_zmode >> 1) & 7u;
    const bool zupdate = ((d_zmode >> 4) & 1u) != 0u;
    const bool ztest = (d_zmode & 1u) != 0u;
    const bool early = ztest && ((d_pe_control >> 6) & 1u) != 0u;
    if (early) {
        if (!z_compare(zfunc, f.z0, dz)) {
            return;
        }
        if (zupdate) {
            oz = f.z0 & 0xFFFFFFu;
        }
    }
    if (!f.alpha_ok) {
        return;
    }
    if (ztest && !early) {
        if (!z_compare(zfunc, f.z, dz)) {
            return;
        }
        if (zupdate) {
            oz = f.z & 0xFFFFFFu;
        }
    }
    oc = efb_blend(p, dc, f.px);
}

// Limpeza da cópia (ClearEfb): cor em regs, Z em ztex_bias, máscaras em blend/zmode.
static void clear_resolve(uint dc, uint dz, out uint oc, out uint oz) {
    const bool color = ((d_blend >> 3) & 1u) != 0u;
    const bool alpha = ((d_blend >> 4) & 1u) != 0u;
    oc = (color || alpha) ? efb_store(d_pe_control & 7u, dc, asuint(d_regs[0]), color, alpha) : dc;
    oz = ((d_zmode >> 4) & 1u) != 0u ? d_ztex_bias : dz;
}

// Sem saída de cor: o EFB é lido e gravado só pelos ROVs (o color target do passe é falso, com máscara zero).
// A região ordenada é uma só e fica fora de qualquer desvio: lê as duas imagens, resolve e grava as duas de volta
// (o mesmo valor quando nada muda, como o shader do framebuffer fetch). Com acessos dentro de desvios o DXC parte o
// interlock em vários pares Begin/End, e o SPIRV-Cross (1a616956) então não marca nenhuma textura com
// raster_order_group no MSL: o Metal perderia a ordem sem aviso (docs/M9_PARTE1B_HLSL.md).
void gx_fs(VOut i) {
    const uint2 p = uint2(i.position.xy);
    const bool clear = (d_flags & 1u) != 0u;
    Frag f = (Frag)0;
    if (!clear) {
        f = shade(i, p);
    }
    const uint dc = efb_color[p];
    const uint dz = efb_depth[p];
    uint oc;
    uint oz;
    if (clear) {
        clear_resolve(dc, dz, oc, oz);
    } else {
        pe_resolve(f, p, dc, dz, oc, oz);
    }
    efb_color[p] = oc;
    efb_depth[p] = oz;
}
