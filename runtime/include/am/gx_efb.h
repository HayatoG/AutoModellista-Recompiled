/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Embedded framebuffer (EFB) do GX: 640x528 pixels de cor (RGB8, RGBA6 ou RGB565) e Z de 24 bits, com o teste de Z,
 * o blend/operação lógica/dither do Pixel Engine e as cópias que o BP 0x52 dispara: EFB -> XFB (YUV 4:2:2 na
 * memória do guest, o que o VI mostra) e EFB -> textura (formatos de cópia do GX), com limpeza opcional.
 *
 * O pixel é guardado empacotado em 24 bits como no Dolphin (SWEfbInterface.cpp). A conversão RGB -> YUV da cópia
 * para o XFB segue o shader de codificação do Dolphin (TextureConversionShader.cpp: WriteSampleFunction e
 * WriteXFBEncoder), que é o que o oráculo (backend Metal com o XFB na RAM) executa; o filtro vertical de cópia e a
 * gama também. Fontes: YAGCD 5.13; Dolphin VideoBackends/Software/{SWEfbInterface,TextureEncoder,EfbCopy}.cpp,
 * VideoCommon/BPStructs.cpp (BPMEM_TRIGGER_EFB_COPY), tag 2609a. */
#ifndef AM_GX_EFB_H
#define AM_GX_EFB_H

#include "am/types.h"

#define GX_EFB_WIDTH  640U
#define GX_EFB_HEIGHT 528U

/* PEControl.pixel_format (BPMemory.h). */
#define GX_PIXEL_RGB8_Z24   0U
#define GX_PIXEL_RGBA6_Z24  1U
#define GX_PIXEL_RGB565_Z16 2U
#define GX_PIXEL_Z24        3U

/* Um tipo de cópia para textura (formato, tamanho de origem, meia escala, Z), para estudar o que o jogo copia. */
typedef struct GxEfbCopyKind {
    u32 format; /* formato de cópia real (0..12) */
    u32 width;
    u32 height;
    bool half;
    bool depth;
    u64 count;
} GxEfbCopyKind;

#define GX_EFB_COPY_KINDS 16U

typedef struct GxEfbStats {
    u64 xfb_copies;
    u64 texture_copies;
    u64 clears;
    u32 last_xfb_addr;
    u32 last_xfb_width;
    u32 last_xfb_height;
    u32 copy_formats; /* bit n: cópia para textura no formato de cópia n */
    u64 depth_copies;
    GxEfbCopyKind kinds[GX_EFB_COPY_KINDS];
    u32 kind_count; /* tipos distintos guardados (os que não couberem só entram nas contagens) */
} GxEfbStats;

void gx_efb_reset(void);

/* Lê do BP o estado de Z, blend e formato de pixel para os pixels do próximo desenho. */
void gx_efb_begin_draw(void);

/* Teste de Z no pixel (x, y) com z de 24 bits; grava o Z se passar e a escrita estiver ligada. */
bool gx_efb_z_test(u32 x, u32 y, u32 z);
/* Pixel do TEV (RGBA) para o EFB: blend ou operação lógica, alfa constante, dither e máscaras de escrita. */
void gx_efb_blend(u32 x, u32 y, const u8 *rgba);

/* BP 0x52: cópia (XFB ou textura) e limpeza, com os parâmetros dos registradores de BP. */
void gx_efb_copy(void);
/* M8: a cópia sem a limpeza (o backend de GPU limpa o EFB dele). */
void gx_efb_copy_ex(bool do_clear);
/* M8: linhas do EFB que a cópia pedida agora lê (com o filtro vertical) e se a fonte é o Z. false = fora do EFB. */
bool gx_efb_copy_rows(u32 *top, u32 *bottom, bool *depth);
/* M8: a limpeza que a cópia pedida agora faz (área, cor, Z, máscaras e formato), para fazê-la na GPU. */
typedef struct GxEfbClear {
    u32 left, top, right, bottom;
    u8 rgba[4];
    u32 z;
    bool color, alpha, z_update;
    u32 format;
} GxEfbClear;
bool gx_efb_clear_params(GxEfbClear *c);
/* M9 (--internal-res N): a cópia para o XFB pedida agora, feita do EFB grande (hi: N*640 de largura) direto em RGBA, sem o
 * YUV. rgba tem cap bytes; out recebe o endereço físico do XFB e o tamanho da imagem em pixels grandes. false = a
 * cópia não é para o XFB, é do Z ou não cabe. Não escreve na memória do guest. */
typedef struct GxEfbHiXfb {
    u32 dest; /* endereço físico (como GxEfbStats.last_xfb_addr) */
    u32 width, height;
} GxEfbHiXfb;
bool gx_efb_copy_xfb_hires(const u32 *hi, u32 n, u8 *rgba, u32 cap, GxEfbHiXfb *out);
/* A cópia pedida agora (BP COPY_EXECUTE) é para o XFB. */
bool gx_efb_copy_is_xfb(void);
/* A cópia pedida agora limpa o EFB depois (COPY_EXECUTE.clear). */
bool gx_efb_copy_clears(void);

/* Cor do EFB em RGBA (alfa 255 nos formatos sem alfa) e Z; para testes e depuração. */
void gx_efb_peek(u32 x, u32 y, u8 *rgba, u32 *z);

/* XFB (YUYV) da memória do guest para RGBA, com a mesma conversão do oráculo (scripts/dolphin_trace.py:
 * write_xfb_png, BT.601 de faixa limitada truncado). `stride` em bytes. */
void gx_efb_xfb_to_rgba(GuestAddr addr, u32 width, u32 height, u32 stride, u8 *rgba);

const GxEfbStats *gx_efb_stats(void);
/* M8: o EFB da CPU (GX_EFB_WIDTH x GX_EFB_HEIGHT, linha a linha): cor empacotada em 24 bits no formato do PE e Z de
 * 24 bits. O backend de GPU traz o EFB da GPU para cá antes de uma cópia e devolve depois da limpeza. */
u32 *gx_efb_color_buffer(void);
u32 *gx_efb_depth_buffer(void);

#endif /* AM_GX_EFB_H */
