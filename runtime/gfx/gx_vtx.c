/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Vertex loader. Regras de leitura (escala de posição/texcoord por PosFrac/TexFrac, frações fixas das normais, cores
 * expandidas por replicação de bits, índice 3 do NBT) conforme Dolphin VertexLoader_*.cpp e SWVertexLoader.cpp. */
#include "am/gx_vtx.h"

#include <string.h>

#include "am/log.h"
#include "am/mem.h"

#define GX_FORMAT_COUNT  5U
#define GX_ATTR_NONE     0U
#define GX_ATTR_DIRECT   1U
#define GX_ATTR_INDEX8   2U
#define GX_ATTR_INDEX16  3U
#define GX_MISSING_COLOR 0xFFU /* g_ActiveConfig.iMissingColorValue: branco opaco */

static struct {
    u32 matindex_a;
    u32 matindex_b;
    u32 vcd_lo;
    u32 vcd_hi;
    u32 vat_a[GX_VAT_COUNT];
    u32 vat_b[GX_VAT_COUNT];
    u32 vat_c[GX_VAT_COUNT];
    u32 array_base[GX_ARRAY_COUNT];
    u32 array_stride[GX_ARRAY_COUNT];
    f32 nrm_cache[3][3]; /* última normal lida: vale para vértices sem normal (VertexLoaderManager::normal_cache) */
} s_vtx;

/* Leitor sequencial do vértice no fluxo. */
typedef struct Reader {
    const u8 *p;
} Reader;

void gx_vtx_reset(void) {
    memset(&s_vtx, 0, sizeof(s_vtx));
    s_vtx.nrm_cache[0][2] = 1.0F; /* HIPÓTESE: valor inicial irrelevante; nenhum vértice sem normal acende luz */
}

void gx_vtx_load_cp(u8 reg, u32 value) {
    const u32 low = reg & 0x0FU;
    switch (reg & 0xF0U) {
    case GX_CP_MATINDEX_A:
        s_vtx.matindex_a = value;
        return;
    case GX_CP_MATINDEX_B:
        s_vtx.matindex_b = value;
        return;
    case GX_CP_VCD_LO:
        s_vtx.vcd_lo = value;
        return;
    case GX_CP_VCD_HI:
        s_vtx.vcd_hi = value;
        return;
    case GX_CP_VAT_A:
        s_vtx.vat_a[low & 7U] = value;
        return;
    case GX_CP_VAT_B:
        s_vtx.vat_b[low & 7U] = value;
        return;
    case GX_CP_VAT_C:
        s_vtx.vat_c[low & 7U] = value;
        return;
    case GX_CP_ARRAY_BASE:
        s_vtx.array_base[low] = value & 0x03FFFFFFU;
        return;
    case GX_CP_ARRAY_STRIDE:
        s_vtx.array_stride[low] = value & 0xFFU;
        return;
    default:
        return; /* registradores sem efeito no desenho (Dolphin: "unknown CP reg" só avisa) */
    }
}

u32 gx_vtx_cp(u8 reg) {
    const u32 low = reg & 0x0FU;
    switch (reg & 0xF0U) {
    case GX_CP_MATINDEX_A:
        return s_vtx.matindex_a;
    case GX_CP_MATINDEX_B:
        return s_vtx.matindex_b;
    case GX_CP_VCD_LO:
        return s_vtx.vcd_lo;
    case GX_CP_VCD_HI:
        return s_vtx.vcd_hi;
    case GX_CP_VAT_A:
        return s_vtx.vat_a[low & 7U];
    case GX_CP_VAT_B:
        return s_vtx.vat_b[low & 7U];
    case GX_CP_VAT_C:
        return s_vtx.vat_c[low & 7U];
    case GX_CP_ARRAY_BASE:
        return s_vtx.array_base[low];
    case GX_CP_ARRAY_STRIDE:
        return s_vtx.array_stride[low];
    default:
        return 0;
    }
}

u32 gx_vtx_array_addr(u32 array, u32 index) {
    AM_ASSERT(array < GX_ARRAY_COUNT, "array %u", array);
    return (s_vtx.array_base[array] + (index * s_vtx.array_stride[array])) & 0x03FFFFFFU;
}

/* u8, s8, u16, s16, f32 (Dolphin VertexLoader GetElementSize; 5..7 se comportam como f32). */
static u32 component_size(u32 format) {
    static const u8 s_sizes[8] = {1, 1, 2, 2, 4, 4, 4, 4};
    return s_sizes[format & 7U];
}

/* Tipo de atributo no VCD: 0 ausente, 1 direto, 2 índice de 8 bits, 3 índice de 16 bits. */
static u32 attr_size(u32 type, u32 direct_size) {
    switch (type) {
    case GX_ATTR_DIRECT:
        return direct_size;
    case GX_ATTR_INDEX8:
        return 1U;
    case GX_ATTR_INDEX16:
        return 2U;
    default:
        return 0U;
    }
}

/* Cor direta: RGB565, RGB888, RGB888x, RGBA4444, RGBA6666, RGBA8888. */
static u32 color_size(u32 comp) {
    static const u8 s_sizes[8] = {2, 3, 4, 2, 3, 4, 4, 4}; /* 6 e 7: Dolphin lê como 8888 */
    return s_sizes[comp & 7U];
}

typedef struct TexFormat {
    u32 elements; /* 0 = s, 1 = st */
    u32 format;
    u32 frac;
} TexFormat;

/* Coordenada de textura n no VAT (Dolphin CPMemory.h: UVAT_group0/1/2). */
static TexFormat tex_format(u32 vat, u32 n) {
    const u32 a = s_vtx.vat_a[vat];
    const u32 b = s_vtx.vat_b[vat];
    const u32 c = s_vtx.vat_c[vat];
    static const u8 s_shift_b[3] = {0, 9, 18};  /* Tex1..Tex3 em VAT_B */
    static const u8 s_shift_c[3] = {5, 14, 23}; /* Tex5..Tex7 em VAT_C */
    TexFormat f;
    if (n == 0U) {
        f = (TexFormat){(a >> 21) & 1U, (a >> 22) & 7U, (a >> 25) & 31U};
    } else if (n <= 3U) {
        const u32 s = s_shift_b[n - 1U];
        f = (TexFormat){(b >> s) & 1U, (b >> (s + 1U)) & 7U, (b >> (s + 4U)) & 31U};
    } else if (n == 4U) {
        f = (TexFormat){(b >> 27) & 1U, (b >> 28) & 7U, c & 31U};
    } else {
        const u32 s = s_shift_c[n - 5U];
        f = (TexFormat){(c >> s) & 1U, (c >> (s + 1U)) & 7U, (c >> (s + 4U)) & 31U};
    }
    return f;
}

static bool nrm_index3(u32 vat) {
    const u32 a = s_vtx.vat_a[vat];
    return ((a >> 9) & 1U) != 0U && ((a >> 31) & 1U) != 0U;
}

u32 gx_vtx_size(u32 vat) {
    AM_ASSERT(vat < GX_VAT_COUNT, "VAT %u", vat);
    const u32 lo = s_vtx.vcd_lo;
    const u32 hi = s_vtx.vcd_hi;
    const u32 a = s_vtx.vat_a[vat];
    u32 size = 0;
    for (u32 bit = 0; bit < 9U; bit++) { /* PosNrmMtxIdx e Tex0..7MtxIdx: 1 byte cada, sempre diretos */
        size += (lo >> bit) & 1U;
    }
    const u32 pos_elems = (a & 1U) != 0U ? 3U : 2U;
    size += attr_size((lo >> 9) & 3U, pos_elems * component_size((a >> 1) & 7U));
    const u32 nrm_type = (lo >> 11) & 3U;
    const bool nbt = ((a >> 9) & 1U) != 0U;
    if (nrm_index3(vat) && nrm_type >= GX_ATTR_INDEX8) {
        size += 3U * attr_size(nrm_type, 0U); /* NBT com um índice por vetor */
    } else {
        size += attr_size(nrm_type, (nbt ? 9U : 3U) * component_size((a >> 10) & 7U));
    }
    size += attr_size((lo >> 13) & 3U, color_size((a >> 14) & 7U));
    size += attr_size((lo >> 15) & 3U, color_size((a >> 18) & 7U));
    for (u32 n = 0; n < GX_TEXCOORD_MAX; n++) {
        const TexFormat f = tex_format(vat, n);
        size += attr_size((hi >> (n * 2U)) & 3U, (f.elements != 0U ? 2U : 1U) * component_size(f.format));
    }
    return size;
}

static u8 read8(Reader *r) {
    return *r->p++;
}

static u32 read_index(Reader *r, u32 type) {
    if (type == GX_ATTR_INDEX8) {
        return read8(r);
    }
    const u32 hi = read8(r);
    return (hi << 8) | read8(r);
}

/* Copia `size` bytes do elemento `index` do array (memória do guest, big-endian) para `buf`. */
static void fetch(u32 array, u32 index, u8 *buf, u32 size) {
    const GuestAddr base = MEM_CACHED_BASE | gx_vtx_array_addr(array, index);
    for (u32 i = 0; i < size; i++) {
        buf[i] = mem_read8(NULL, base + i);
    }
}

/* Componente em big-endian no formato `format`, sem escala (inteiros) ou como f32. */
static f32 component(const u8 *p, u32 format) {
    switch (format) {
    case 0U:
        return (f32)p[0];
    case 1U:
        return (f32)(s8)p[0];
    case 2U:
        return (f32)(u16)((p[0] << 8) | p[1]);
    case 3U:
        return (f32)(s16)(u16)((p[0] << 8) | p[1]);
    default: {
        const u32 bits = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        f32 f;
        memcpy(&f, &bits, sizeof(f));
        return f;
    }
    }
}

/* Dados de `count` componentes do atributo: diretos do fluxo ou do array. Devolve o ponteiro para os bytes. */
static const u8 *attr_data(Reader *r, u32 type, u32 array, u32 bytes, u8 *buf, bool *skipped) {
    if (type == GX_ATTR_DIRECT) {
        const u8 *p = r->p;
        r->p += bytes;
        return p;
    }
    const u32 index = read_index(r, type);
    if (skipped != NULL) {
        *skipped = index == (type == GX_ATTR_INDEX8 ? 0xFFU : 0xFFFFU);
    }
    fetch(array, index, buf, bytes);
    return buf;
}

static void decode_position(u32 vat, Reader *r, GxVertex *v) {
    const u32 a = s_vtx.vat_a[vat];
    const u32 type = (s_vtx.vcd_lo >> 9) & 3U;
    const u32 elems = (a & 1U) != 0U ? 3U : 2U;
    const u32 fmt = (a >> 1) & 7U;
    const u32 size = component_size(fmt);
    const f32 scale = fmt < 4U ? 1.0F / (f32)(1U << ((a >> 4) & 31U)) : 1.0F;
    u8 buf[12] = {0};
    if (type == GX_ATTR_NONE) {
        am_fatal("GX: desenho sem posição no VCD (0x%08X)", s_vtx.vcd_lo);
    }
    const u8 *p = attr_data(r, type, 0, elems * size, buf, &v->skip);
    for (u32 i = 0; i < elems; i++) {
        v->pos[i] = component(p + ((size_t)i * size), fmt) * scale;
    }
}

/* Frações fixas das normais (VertexLoader_Normal.cpp FracAdjust): s8 6, u8 7, s16 14, u16 15 bits. */
static f32 normal_scale(u32 fmt) {
    static const f32 s_scale[4] = {1.0F / 128.0F, 1.0F / 64.0F, 1.0F / 32768.0F, 1.0F / 16384.0F};
    return fmt < 4U ? s_scale[fmt] : 1.0F;
}

static void decode_normal(u32 vat, Reader *r, GxVertex *v) {
    const u32 a = s_vtx.vat_a[vat];
    const u32 type = (s_vtx.vcd_lo >> 11) & 3U;
    const u32 fmt = (a >> 10) & 7U;
    const u32 size = component_size(fmt);
    const u32 vectors = ((a >> 9) & 1U) != 0U ? 3U : 1U;
    u8 buf[36] = {0};
    if (type != GX_ATTR_NONE) {
        if (vectors == 3U && nrm_index3(vat) && type != GX_ATTR_DIRECT) {
            for (u32 n = 0; n < 3U; n++) { /* um índice por vetor; o vetor n começa no componente 3n do elemento */
                const u32 index = read_index(r, type);
                fetch(1, index, buf, 9U * size);
                for (u32 i = 0; i < 3U; i++) {
                    s_vtx.nrm_cache[n][i] = component(buf + ((((size_t)n * 3U) + i) * size), fmt) * normal_scale(fmt);
                }
            }
        } else {
            const u8 *p = attr_data(r, type, 1, vectors * 3U * size, buf, NULL);
            for (u32 n = 0; n < vectors; n++) {
                for (u32 i = 0; i < 3U; i++) {
                    s_vtx.nrm_cache[n][i] = component(p + ((((size_t)n * 3U) + i) * size), fmt) * normal_scale(fmt);
                }
            }
        }
    }
    memcpy(v->nrm, s_vtx.nrm_cache, sizeof(v->nrm));
}

static u8 expand4(u32 v) {
    return (u8)((v << 4) | v);
}

static u8 expand5(u32 v) {
    return (u8)((v << 3) | (v >> 2));
}

static u8 expand6(u32 v) {
    return (u8)((v << 2) | (v >> 4));
}

/* VertexLoader_Color.cpp: SetCol565/4444/6666; 888 e 888x com alfa 255. */
static void decode_color_bytes(const u8 *p, u32 comp, u8 *out) {
    switch (comp) {
    case 0U: { /* RGB565 */
        const u32 c = ((u32)p[0] << 8) | p[1];
        out[0] = expand5(c >> 11);
        out[1] = expand6((c >> 5) & 63U);
        out[2] = expand5(c & 31U);
        out[3] = 0xFF;
        return;
    }
    case 1U:
    case 2U: /* RGB888, RGB888x */
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        out[3] = 0xFF;
        return;
    case 3U: /* RGBA4444 */
        out[0] = expand4(p[0] >> 4U);
        out[1] = expand4(p[0] & 15U);
        out[2] = expand4(p[1] >> 4U);
        out[3] = expand4(p[1] & 15U);
        return;
    case 4U: { /* RGBA6666 */
        const u32 c = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
        out[0] = expand6(c >> 18);
        out[1] = expand6((c >> 12) & 63U);
        out[2] = expand6((c >> 6) & 63U);
        out[3] = expand6(c & 63U);
        return;
    }
    default: /* RGBA8888 */
        memcpy(out, p, 4);
        return;
    }
}

/* Com só uma cor no VCD ela vai para o canal 0 (SWVertexLoader ParseColorAttributes). */
static void decode_colors(u32 vat, Reader *r, GxVertex *v) {
    const u32 a = s_vtx.vat_a[vat];
    u32 filled = 0;
    memset(v->color, GX_MISSING_COLOR, sizeof(v->color));
    for (u32 c = 0; c < 2U; c++) {
        const u32 type = (s_vtx.vcd_lo >> (13U + (2U * c))) & 3U;
        if (type == GX_ATTR_NONE) {
            continue;
        }
        const u32 comp = (a >> (14U + (4U * c))) & 7U;
        u8 buf[4] = {0};
        const u8 *p = attr_data(r, type, 2U + c, color_size(comp), buf, NULL);
        decode_color_bytes(p, comp, v->color[filled++]);
    }
}

static void decode_texcoords(u32 vat, Reader *r, GxVertex *v) {
    for (u32 n = 0; n < GX_TEXCOORD_MAX; n++) {
        const u32 type = (s_vtx.vcd_hi >> (n * 2U)) & 3U;
        if (type == GX_ATTR_NONE) {
            continue;
        }
        const TexFormat f = tex_format(vat, n);
        const u32 size = component_size(f.format);
        const u32 elems = f.elements != 0U ? 2U : 1U;
        const f32 scale = f.format < 4U ? 1.0F / (f32)(1U << f.frac) : 1.0F;
        u8 buf[8] = {0};
        const u8 *p = attr_data(r, type, 4U + n, elems * size, buf, NULL);
        v->tex[n][0] = component(p, f.format) * scale;
        v->tex[n][1] = elems == 2U ? component(p + size, f.format) * scale : 0.0F;
    }
}

void gx_vtx_decode(u32 vat, const u8 *data, GxVertex *out) {
    AM_ASSERT(vat < GX_VAT_COUNT, "VAT %u", vat);
    Reader r = {data};
    memset(out->pos, 0, sizeof(out->pos));
    memset(out->tex, 0, sizeof(out->tex));
    out->skip = false;
    if ((s_vtx.vcd_lo & 1U) != 0U) {
        out->pos_mtx = (u8)(read8(&r) & 0x3FU);
    }
    for (u32 n = 0; n < GX_TEXCOORD_MAX; n++) {
        if (((s_vtx.vcd_lo >> (n + 1U)) & 1U) != 0U) {
            out->tex_mtx[n] = (u8)(read8(&r) & 0x3FU);
        }
    }
    decode_position(vat, &r, out);
    decode_normal(vat, &r, out);
    decode_colors(vat, &r, out);
    decode_texcoords(vat, &r, out);
}
