/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M8: GPU pela SDL3 GPU API (Metal no macOS), para o backend de GX em GPU (gfx/gx_gpu.c). M9: também D3D12 e Vulkan,
 * com o ubershader em HLSL (docs/M9_PARTE1B_HLSL.md). */
#ifndef AM_PLATFORM_GPU_H
#define AM_PLATFORM_GPU_H

#include "am/gpu_batch.h"

/* Qual ubershader usar. HAND: o MSL à mão do M8 (runtime/shaders/gx_ubershader.metal), EFB em dois color targets
 * lidos por framebuffer fetch: só Metal em GPU da Apple. HLSL: o de runtime/shaders/gx_ubershader.hlsl (DXIL,
 * SPIR-V ou metallib, conforme o backend), EFB em duas texturas de storage graváveis na ordem da rasterização: exige
 * a SDL com patches/sdl e um device com ROV/interlock/raster order groups. AUTO: no Metal, o mais rápido dos dois
 * (dão a mesma imagem byte a byte; a escolha está em platform_gpu.c, pick_shader); nos outros backends, HLSL. */
typedef enum PlatformGpuShader {
    PLATFORM_GPU_SHADER_AUTO = 0,
    PLATFORM_GPU_SHADER_HAND,
    PLATFORM_GPU_SHADER_HLSL,
} PlatformGpuShader;

/* Cria o dispositivo, o pipeline do ubershader pedido e o EFB da GPU. false = sem GPU utilizável para esse shader
 * (platform_gpu_error diz por quê; quem chama cai no software). */
bool platform_gpu_init(PlatformGpuShader shader);
const char *platform_gpu_error(void);
/* Envia o lote, desenha e lê o EFB de volta (bloqueia até a GPU terminar). */
bool platform_gpu_run(const GpuBatch *batch);
void platform_gpu_shutdown(void);
/* Driver, shader e formato ("metal, HLSL (metallib), raster order groups"...), para o log. */
const char *platform_gpu_name(void);
/* Tempo total (µs) esperando a GPU terminar nas leituras do EFB e quantas leituras houve. */
u64 platform_gpu_wait_us(u64 *readbacks);

#endif /* AM_PLATFORM_GPU_H */
