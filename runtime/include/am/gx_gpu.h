/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M8: backend de GX na GPU (--gfx gpu). O caminho até o vértice transformado (FIFO, vertex loader, XF, recorte,
 * descarte pela face) é o mesmo do software; daí em diante, em vez do rasterizador e do TEV da CPU, cada desenho vira
 * um comando com os vértices em pixels do EFB e o estado do TEV/PE num uniform (am/gpu_batch.h), e a GPU desenha com o
 * ubershader. Numa cópia do EFB o lote vai para a GPU, o EFB volta para a CPU e a cópia (XFB ou textura) e a limpeza
 * rodam no código do software (gx_efb.c); o EFB limpo volta para a GPU no lote seguinte.
 *
 * Fora do que a GPU faz: estágios indiretos (o estágio usa a coordenada sem deslocamento) e zfreeze (o Z interpolado
 * do próprio triângulo); os dois ficam contados em GxGpuStats. */
#ifndef AM_GX_GPU_H
#define AM_GX_GPU_H

#include "am/gpu_batch.h"
#include "am/gx_xf.h"
#include "am/types.h"

typedef struct GxGpuStats {
    u64 batches;
    u64 draws;
    u64 triangles;
    u64 texture_uploads;
    u64 uploaded_texels;
    u64 arena_resets;
    u64 indirect_draws; /* desenhos com estágios indiretos (não feitos na GPU) */
    u64 zfreeze_draws;
} GxGpuStats;

/* Quem executa o lote (platform_gpu_run; nos testes, um falso). NULL desliga o backend (volta ao software). */
typedef bool (*GxGpuRunFn)(const GpuBatch *batch);
void gx_gpu_set_backend(GxGpuRunFn run);
bool gx_gpu_active(void);
void gx_gpu_reset(void);

/* Chamados pelo GX quando ativo: estado do desenho (depois de gx_tex_prepare e gx_raster_begin_draw), cada
 * triângulo de frente, fim do desenho. */
void gx_gpu_begin_draw(void);
void gx_gpu_triangle(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2);
void gx_gpu_end_draw(void);
/* Antes de uma cópia do EFB: desenha o que falta e traz o EFB da GPU para a CPU (gx_efb.c). */
void gx_gpu_sync(void);
/* Depois de uma cópia com limpeza: o EFB da CPU mudou e tem de voltar para a GPU. */
void gx_gpu_efb_changed(void);

const GxGpuStats *gx_gpu_stats(void);

#endif /* AM_GX_GPU_H */
