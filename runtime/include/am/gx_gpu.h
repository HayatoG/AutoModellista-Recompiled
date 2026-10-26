/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M8: backend de GX na GPU (--gfx gpu). O caminho até o vértice transformado (FIFO, vertex loader, XF, recorte,
 * descarte pela face) é o mesmo do software; daí em diante, em vez do rasterizador e do TEV da CPU, cada desenho vira
 * um comando com os vértices em pixels do EFB e o estado do TEV/PE num uniform (am/gpu_batch.h), e a GPU desenha com o
 * ubershader. Numa cópia do EFB o lote vai para a GPU, as linhas copiadas voltam para a CPU e a cópia (XFB ou textura)
 * roda no código do software (gx_efb.c); a limpeza vira um desenho na GPU.
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
    u64 partial_batches; /* enviados no meio do quadro, sem esperar a GPU */
    u64 copies;
    u64 draws;
    u64 triangles;
    u64 texture_uploads;
    u64 uploaded_texels;
    u64 arena_resets;
    u64 indirect_draws; /* desenhos com estágios indiretos (não feitos na GPU) */
    u64 zfreeze_draws;
    u64 merged_draws; /* M9 parte 6: desenhos que continuaram o comando anterior (mesmo estado) */
} GxGpuStats;

/* Quem executa o lote (platform_gpu_run; nos testes, um falso). NULL desliga o backend (volta ao software). */
typedef bool (*GxGpuRunFn)(const GpuBatch *batch);
void gx_gpu_set_backend(GxGpuRunFn run);
bool gx_gpu_active(void);
void gx_gpu_reset(void);

/* Chamados pelo GX quando ativo: estado do desenho (depois de gx_tex_prepare e gx_raster_begin_draw), cada
 * triângulo de frente, fim do desenho. */
void gx_gpu_begin_draw(void);
/* O mesmo, com o estado de BP/XF do desenho anterior (gx_raster_begin_draw_same): reaproveita a parte do TEV. */
void gx_gpu_begin_draw_same(void);
void gx_gpu_triangle(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2);
void gx_gpu_end_draw(void);
/* Desenha o que falta e traz o EFB inteiro da GPU para a CPU (gx_efb.c). */
void gx_gpu_sync(void);
/* BP 0x52 com o backend ativo: desenha o que falta, traz só as linhas que a cópia lê (e o Z se a fonte for o Z),
 * faz a cópia na CPU e põe a limpeza no lote seguinte, na GPU. */
void gx_gpu_copy(void);

const GxGpuStats *gx_gpu_stats(void);

#endif /* AM_GX_GPU_H */
