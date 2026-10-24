/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/platform_gpu.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "../shaders/gx_ubershader_metallib.h"

#define EFB_PIXELS (GPU_EFB_WIDTH * GPU_EFB_HEIGHT)
#define EFB_BYTES  (EFB_PIXELS * 4U)

static struct {
    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUTexture *efb_color;
    SDL_GPUTexture *efb_depth;
    SDL_GPUBuffer *arena;
    SDL_GPUBuffer *vertices;
    u32 vertex_cap; /* em bytes */
    SDL_GPUTransferBuffer *upload;
    u32 upload_cap;
    SDL_GPUTransferBuffer *download;
    u64 wait_ns; /* tempo esperando a GPU nas leituras do EFB */
    u64 readbacks;
    char name[128];
    char error[256];
} s_gpu;

static bool fail(const char *what) {
    (void)snprintf(s_gpu.error, sizeof(s_gpu.error), "%s: %s", what, SDL_GetError());
    SDL_Log("GPU: %s", s_gpu.error);
    return false;
}

static SDL_GPUShader *shader(const char *entry, SDL_GPUShaderStage stage, u32 uniforms, u32 storage) {
    const SDL_GPUShaderCreateInfo info = {.code_size = sizeof(GX_UBERSHADER_METALLIB),
                                          .code = GX_UBERSHADER_METALLIB,
                                          .entrypoint = entry,
                                          .format = SDL_GPU_SHADERFORMAT_METALLIB,
                                          .stage = stage,
                                          .num_uniform_buffers = uniforms,
                                          .num_storage_buffers = storage};
    return SDL_CreateGPUShader(s_gpu.device, &info);
}

static bool create_pipeline(void) {
    SDL_GPUShader *vs = shader("gx_vs", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader *fs = shader("gx_fs", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (vs == NULL || fs == NULL) {
        return fail("SDL_CreateGPUShader");
    }
    SDL_GPUVertexAttribute attrs[3 + GPU_TEXCOORDS];
    for (u32 i = 0; i < 3U + GPU_TEXCOORDS; i++) {
        attrs[i] = (SDL_GPUVertexAttribute){
            .location = i, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = i * 16U};
    }
    const SDL_GPUVertexBufferDescription vb = {.slot = 0, .pitch = sizeof(GpuVertex)};
    const SDL_GPUColorTargetDescription targets[2] = {{.format = SDL_GPU_TEXTUREFORMAT_R32_UINT},
                                                      {.format = SDL_GPU_TEXTUREFORMAT_R32_UINT}};
    const SDL_GPUGraphicsPipelineCreateInfo info = {
        .vertex_shader = vs,
        .fragment_shader = fs,
        .vertex_input_state = {.vertex_buffer_descriptions = &vb,
                               .num_vertex_buffers = 1,
                               .vertex_attributes = attrs,
                               .num_vertex_attributes = 3U + GPU_TEXCOORDS},
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE},
        .target_info = {.color_target_descriptions = targets, .num_color_targets = 2}};
    s_gpu.pipeline = SDL_CreateGPUGraphicsPipeline(s_gpu.device, &info);
    SDL_ReleaseGPUShader(s_gpu.device, vs);
    SDL_ReleaseGPUShader(s_gpu.device, fs);
    return s_gpu.pipeline != NULL || fail("SDL_CreateGPUGraphicsPipeline");
}

static SDL_GPUTexture *efb_texture(void) {
    const SDL_GPUTextureCreateInfo info = {.type = SDL_GPU_TEXTURETYPE_2D,
                                           .format = SDL_GPU_TEXTUREFORMAT_R32_UINT,
                                           .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                           .width = GPU_EFB_WIDTH,
                                           .height = GPU_EFB_HEIGHT,
                                           .layer_count_or_depth = 1,
                                           .num_levels = 1};
    return SDL_CreateGPUTexture(s_gpu.device, &info);
}

bool platform_gpu_init(void) {
    if (s_gpu.device != NULL) {
        return true;
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { /* a SDL só cria o dispositivo com o vídeo iniciado */
        return fail("SDL_InitSubSystem(VIDEO)");
    }
    s_gpu.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_METALLIB, false, NULL);
    if (s_gpu.device == NULL) {
        return fail("SDL_CreateGPUDevice (metallib)");
    }
    (void)snprintf(s_gpu.name, sizeof(s_gpu.name), "%s", SDL_GetGPUDeviceDriver(s_gpu.device));
    s_gpu.efb_color = efb_texture();
    s_gpu.efb_depth = efb_texture();
    const SDL_GPUBufferCreateInfo arena = {.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                                           .size = GPU_ARENA_TEXELS * 4U};
    s_gpu.arena = SDL_CreateGPUBuffer(s_gpu.device, &arena);
    const SDL_GPUTransferBufferCreateInfo down = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                                                  .size = 2U * EFB_BYTES};
    s_gpu.download = SDL_CreateGPUTransferBuffer(s_gpu.device, &down);
    if (s_gpu.efb_color == NULL || s_gpu.efb_depth == NULL || s_gpu.arena == NULL || s_gpu.download == NULL) {
        return fail("recursos da GPU");
    }
    return create_pipeline();
}

const char *platform_gpu_name(void) {
    return s_gpu.name;
}

u64 platform_gpu_wait_us(u64 *readbacks) {
    *readbacks = s_gpu.readbacks;
    return s_gpu.wait_ns / 1000U;
}

static bool ensure_upload(u32 bytes) {
    if (bytes <= s_gpu.upload_cap) {
        return true;
    }
    if (s_gpu.upload != NULL) {
        SDL_ReleaseGPUTransferBuffer(s_gpu.device, s_gpu.upload);
    }
    u32 cap = s_gpu.upload_cap != 0U ? s_gpu.upload_cap : 4U * 1024U * 1024U;
    while (cap < bytes) {
        cap *= 2U;
    }
    const SDL_GPUTransferBufferCreateInfo info = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = cap};
    s_gpu.upload = SDL_CreateGPUTransferBuffer(s_gpu.device, &info);
    s_gpu.upload_cap = s_gpu.upload != NULL ? cap : 0U;
    return s_gpu.upload != NULL || fail("SDL_CreateGPUTransferBuffer");
}

static bool ensure_vertices(u32 bytes) {
    if (bytes <= s_gpu.vertex_cap) {
        return true;
    }
    if (s_gpu.vertices != NULL) {
        SDL_ReleaseGPUBuffer(s_gpu.device, s_gpu.vertices);
    }
    u32 cap = s_gpu.vertex_cap != 0U ? s_gpu.vertex_cap : 1024U * 1024U;
    while (cap < bytes) {
        cap *= 2U;
    }
    const SDL_GPUBufferCreateInfo info = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = cap};
    s_gpu.vertices = SDL_CreateGPUBuffer(s_gpu.device, &info);
    s_gpu.vertex_cap = s_gpu.vertices != NULL ? cap : 0U;
    return s_gpu.vertices != NULL || fail("SDL_CreateGPUBuffer (vértices)");
}

/* Vértices, texturas novas e (se pedido) o EFB da CPU num só buffer de transferência. */
static bool upload(SDL_GPUCommandBuffer *cmd, const GpuBatch *b) {
    const u32 vbytes = b->vertex_count * (u32)sizeof(GpuVertex);
    u32 tbytes = 0;
    for (u32 i = 0; i < b->upload_count; i++) {
        tbytes += b->uploads[i].texels * 4U;
    }
    const u32 ebytes = b->efb_color_in != NULL ? 2U * EFB_BYTES : 0U;
    const u32 total = vbytes + tbytes + ebytes;
    if (total == 0U) {
        return true;
    }
    if (!ensure_upload(total) || !ensure_vertices(vbytes)) {
        return false;
    }
    u8 *map = SDL_MapGPUTransferBuffer(s_gpu.device, s_gpu.upload, true);
    if (map == NULL) {
        return fail("SDL_MapGPUTransferBuffer");
    }
    if (vbytes != 0U) {
        memcpy(map, b->vertices, vbytes);
    }
    u32 at = vbytes;
    for (u32 i = 0; i < b->upload_count; i++) {
        memcpy(map + at, b->uploads[i].rgba, (size_t)b->uploads[i].texels * 4U);
        at += b->uploads[i].texels * 4U;
    }
    if (ebytes != 0U) {
        memcpy(map + at, b->efb_color_in, EFB_BYTES);
        memcpy(map + at + EFB_BYTES, b->efb_depth_in, EFB_BYTES);
    }
    SDL_UnmapGPUTransferBuffer(s_gpu.device, s_gpu.upload);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (vbytes != 0U) {
        const SDL_GPUTransferBufferLocation src = {.transfer_buffer = s_gpu.upload, .offset = 0};
        const SDL_GPUBufferRegion dst = {.buffer = s_gpu.vertices, .offset = 0, .size = vbytes};
        SDL_UploadToGPUBuffer(copy, &src, &dst, true); /* o lote anterior pode estar lendo: troca de buffer */
    }
    at = vbytes;
    for (u32 i = 0; i < b->upload_count; i++) {
        const SDL_GPUTransferBufferLocation src = {.transfer_buffer = s_gpu.upload, .offset = at};
        const SDL_GPUBufferRegion dst = {
            .buffer = s_gpu.arena, .offset = b->uploads[i].offset * 4U, .size = b->uploads[i].texels * 4U};
        SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        at += b->uploads[i].texels * 4U;
    }
    for (u32 k = 0; k < (ebytes != 0U ? 2U : 0U); k++) {
        const SDL_GPUTextureTransferInfo src = {.transfer_buffer = s_gpu.upload,
                                                .offset = at + (k * EFB_BYTES),
                                                .pixels_per_row = GPU_EFB_WIDTH,
                                                .rows_per_layer = GPU_EFB_HEIGHT};
        const SDL_GPUTextureRegion dst = {
            .texture = k == 0U ? s_gpu.efb_color : s_gpu.efb_depth, .w = GPU_EFB_WIDTH, .h = GPU_EFB_HEIGHT, .d = 1};
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
    }
    SDL_EndGPUCopyPass(copy);
    return true;
}

static void render(SDL_GPUCommandBuffer *cmd, const GpuBatch *b) {
    if (b->draw_count == 0U) {
        return;
    }
    const SDL_GPUColorTargetInfo targets[2] = {
        {.texture = s_gpu.efb_color, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE},
        {.texture = s_gpu.efb_depth, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE}};
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, targets, 2, NULL);
    SDL_BindGPUGraphicsPipeline(pass, s_gpu.pipeline);
    const SDL_GPUBufferBinding vb = {.buffer = s_gpu.vertices, .offset = 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_BindGPUFragmentStorageBuffers(pass, 0, &s_gpu.arena, 1);
    for (u32 i = 0; i < b->draw_count; i++) {
        const GpuDraw *d = &b->draws[i];
        if (d->vertex_count == 0U || d->scissor[2] <= 0 || d->scissor[3] <= 0) {
            continue;
        }
        const SDL_Rect sc = {d->scissor[0], d->scissor[1], d->scissor[2], d->scissor[3]};
        SDL_SetGPUScissor(pass, &sc);
        SDL_PushGPUFragmentUniformData(cmd, 0, &d->u, sizeof(d->u));
        SDL_DrawGPUPrimitives(pass, d->vertex_count, 1, d->first_vertex, 0);
    }
    SDL_EndGPURenderPass(pass);
}

bool platform_gpu_run(const GpuBatch *b) {
    if (s_gpu.device == NULL) {
        return false;
    }
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(s_gpu.device);
    if (cmd == NULL) {
        return fail("SDL_AcquireGPUCommandBuffer");
    }
    if (!upload(cmd, b)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    render(cmd, b);
    if (b->efb_color_out == NULL) { /* lote parcial: a GPU desenha enquanto a CPU segue */
        return SDL_SubmitGPUCommandBuffer(cmd) || fail("SDL_SubmitGPUCommandBuffer");
    }
    const u32 top = b->readback_top;
    const u32 rows = b->readback_bottom > top ? b->readback_bottom - top : 0U;
    const u32 bytes = rows * GPU_EFB_WIDTH * 4U;
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    for (u32 k = 0; k < (b->efb_depth_out != NULL ? 2U : 1U) && rows != 0U; k++) {
        const SDL_GPUTextureRegion src = {
            .texture = k == 0U ? s_gpu.efb_color : s_gpu.efb_depth, .y = top, .w = GPU_EFB_WIDTH, .h = rows, .d = 1};
        const SDL_GPUTextureTransferInfo dst = {.transfer_buffer = s_gpu.download,
                                                .offset = k * EFB_BYTES,
                                                .pixels_per_row = GPU_EFB_WIDTH,
                                                .rows_per_layer = rows};
        SDL_DownloadFromGPUTexture(copy, &src, &dst);
    }
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == NULL) {
        return fail("SDL_SubmitGPUCommandBufferAndAcquireFence");
    }
    const u64 t0 = SDL_GetTicksNS();
    const bool ok = SDL_WaitForGPUFences(s_gpu.device, true, &fence, 1);
    s_gpu.wait_ns += SDL_GetTicksNS() - t0;
    s_gpu.readbacks++;
    SDL_ReleaseGPUFence(s_gpu.device, fence);
    if (!ok) {
        return fail("SDL_WaitForGPUFences");
    }
    const u8 *map = SDL_MapGPUTransferBuffer(s_gpu.device, s_gpu.download, false);
    if (map == NULL) {
        return fail("SDL_MapGPUTransferBuffer (leitura)");
    }
    const size_t at = (size_t)top * GPU_EFB_WIDTH;
    memcpy(b->efb_color_out + at, map, bytes);
    if (b->efb_depth_out != NULL) {
        memcpy(b->efb_depth_out + at, map + EFB_BYTES, bytes);
    }
    SDL_UnmapGPUTransferBuffer(s_gpu.device, s_gpu.download);
    return true;
}

void platform_gpu_shutdown(void) {
    if (s_gpu.device == NULL) {
        return;
    }
    SDL_WaitForGPUIdle(s_gpu.device);
    SDL_ReleaseGPUTransferBuffer(s_gpu.device, s_gpu.download);
    if (s_gpu.upload != NULL) {
        SDL_ReleaseGPUTransferBuffer(s_gpu.device, s_gpu.upload);
    }
    if (s_gpu.vertices != NULL) {
        SDL_ReleaseGPUBuffer(s_gpu.device, s_gpu.vertices);
    }
    SDL_ReleaseGPUBuffer(s_gpu.device, s_gpu.arena);
    SDL_ReleaseGPUTexture(s_gpu.device, s_gpu.efb_color);
    SDL_ReleaseGPUTexture(s_gpu.device, s_gpu.efb_depth);
    SDL_ReleaseGPUGraphicsPipeline(s_gpu.device, s_gpu.pipeline);
    SDL_DestroyGPUDevice(s_gpu.device);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    memset(&s_gpu, 0, sizeof(s_gpu));
}
