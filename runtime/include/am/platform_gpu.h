/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M8: GPU pela SDL3 GPU API (Metal no macOS), para o backend de GX em GPU (gfx/gx_gpu.c). */
#ifndef AM_PLATFORM_GPU_H
#define AM_PLATFORM_GPU_H

#include "am/gpu_batch.h"

/* Cria o dispositivo, o pipeline do ubershader (metallib embutido) e o EFB da GPU. false = sem GPU utilizável
 * (platform_last_error diz por quê). */
bool platform_gpu_init(void);
/* Envia o lote, desenha e lê o EFB de volta (bloqueia até a GPU terminar). */
bool platform_gpu_run(const GpuBatch *batch);
void platform_gpu_shutdown(void);
/* Nome do driver ("metal"...) e do aparelho, para o log. */
const char *platform_gpu_name(void);
/* Tempo total (µs) esperando a GPU terminar nas leituras do EFB e quantas leituras houve. */
u64 platform_gpu_wait_us(u64 *readbacks);

#endif /* AM_PLATFORM_GPU_H */
