/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/platform_gpu.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "../shaders/gx_ubershader_hlsl.h"
#include "../shaders/gx_ubershader_metallib.h"
#include "platform_internal.h"

/* M9: o caminho do HLSL só existe com a SDL de patches/sdl (sem o patch, a flag não é definida). */
#ifdef SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE
#define AM_GPU_HAVE_RW 1
#else
#define AM_GPU_HAVE_RW 0
#endif

#include "am/gpu_scale.h"

/* M9: com --internal-res N o EFB da GPU tem (N * 640) x (N * 528) pixels; N = 1 é o de sempre. */
#define EFB_PIXELS  (GPU_EFB_WIDTH * GPU_EFB_HEIGHT * s_gpu.scale * s_gpu.scale)
#define EFB_BYTES   (EFB_PIXELS * 4U)
#define EFB_BYTES_Z ((size_t)EFB_BYTES)
#define EFB_W       (GPU_EFB_WIDTH * s_gpu.scale)
#define EFB_H       (GPU_EFB_HEIGHT * s_gpu.scale)

/* Um ubershader embutido num formato: código e entrada dos dois estágios. */
typedef struct ShaderBlob {
    SDL_GPUShaderFormat format;
    const u8 *vs;
    size_t vs_size;
    const u8 *fs;
    size_t fs_size;
    const char *label;
} ShaderBlob;

static struct {
    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUTexture *efb_color;
    SDL_GPUTexture *efb_depth;
    SDL_GPUTexture *dummy; /* HLSL: color target falso do passe (máscara de escrita zero; o EFB vai pelos ROVs) */
    bool hlsl;
    SDL_GPUBuffer *arena;
    SDL_GPUBuffer *vertices;
    u32 vertex_cap; /* em bytes */
    SDL_GPUTransferBuffer *upload;
    u32 upload_cap;
    SDL_GPUTransferBuffer *download;
    u64 wait_ns; /* tempo esperando a GPU nas leituras do EFB */
    u64 readbacks;
    u32 scale; /* resolução interna 1..GPU_SCALE_MAX (platform_gpu_scale); 0 = não pedida */
    char name[128];
    char error[256];
} s_gpu;

void platform_gpu_set_scale(u32 n) {
    if (s_gpu.device == NULL) { /* o tamanho do EFB é fixado na criação dos recursos */
        s_gpu.scale = gpu_scale_clamp((s32)n);
    }
}

u32 platform_gpu_scale(void) {
    return s_gpu.scale != 0U ? s_gpu.scale : 1U;
}

static bool fail(const char *what) {
    (void)snprintf(s_gpu.error, sizeof(s_gpu.error), "%s: %s", what, SDL_GetError());
    SDL_Log("GPU: %s", s_gpu.error);
    return false;
}

/* rw = texturas de storage graváveis do fragment shader (o EFB no caminho do HLSL). */
static SDL_GPUShader *shader(const ShaderBlob *blob, bool fragment, u32 rw) {
    SDL_PropertiesID props = 0;
#if AM_GPU_HAVE_RW
    if (rw != 0U) {
        props = SDL_CreateProperties();
        (void)SDL_SetNumberProperty(props, SDL_PROP_GPU_SHADER_CREATE_NUM_READWRITE_STORAGE_TEXTURES_NUMBER, rw);
    }
#endif
    const SDL_GPUShaderCreateInfo info = {.code_size = fragment ? blob->fs_size : blob->vs_size,
                                          .code = fragment ? blob->fs : blob->vs,
                                          .entrypoint = fragment ? "gx_fs" : "gx_vs",
                                          .format = blob->format,
                                          .stage = fragment ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX,
                                          .num_uniform_buffers = fragment ? 1U : 0U,
                                          .num_storage_buffers = fragment ? 1U : 0U,
                                          .props = props};
    SDL_GPUShader *sh = SDL_CreateGPUShader(s_gpu.device, &info);
    if (props != 0) {
        SDL_DestroyProperties(props);
    }
    return sh;
}

static bool create_pipeline(const ShaderBlob *blob) {
    SDL_GPUShader *vs = shader(blob, false, 0);
    SDL_GPUShader *fs = shader(blob, true, s_gpu.hlsl ? 2U : 0U);
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
    const SDL_GPUColorTargetDescription dummy = {
        .format = SDL_GPU_TEXTUREFORMAT_R8_UNORM,
        .blend_state = {.enable_color_write_mask = true, .color_write_mask = 0}};
    const SDL_GPUGraphicsPipelineCreateInfo info = {
        .vertex_shader = vs,
        .fragment_shader = fs,
        .vertex_input_state = {.vertex_buffer_descriptions = &vb,
                               .num_vertex_buffers = 1,
                               .vertex_attributes = attrs,
                               .num_vertex_attributes = 3U + GPU_TEXCOORDS},
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE},
        .target_info = {.color_target_descriptions = s_gpu.hlsl ? &dummy : targets,
                        .num_color_targets = s_gpu.hlsl ? 1U : 2U}};
    s_gpu.pipeline = SDL_CreateGPUGraphicsPipeline(s_gpu.device, &info);
    SDL_ReleaseGPUShader(s_gpu.device, vs);
    SDL_ReleaseGPUShader(s_gpu.device, fs);
    return s_gpu.pipeline != NULL || fail("SDL_CreateGPUGraphicsPipeline");
}

static SDL_GPUTexture *texture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage) {
    const SDL_GPUTextureCreateInfo info = {.type = SDL_GPU_TEXTURETYPE_2D,
                                           .format = format,
                                           .usage = usage,
                                           .width = EFB_W,
                                           .height = EFB_H,
                                           .layer_count_or_depth = 1,
                                           .num_levels = 1};
    return SDL_CreateGPUTexture(s_gpu.device, &info);
}

/* Formatos que a runtime oferece à SDL: o metallib do M8 sempre; com o patch, também SPIR-V e DXIL (do HLSL), e o
 * pedido do recurso de storage gravável no fragmento (docs/M9_PARTE1B_SDL.md §1). */
static SDL_GPUDevice *create_device(void) {
    const SDL_PropertiesID props = SDL_CreateProperties();
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, true);
    /* M9 parte 5: o backend do ImGui para a SDL GPU traz DXBC para o D3D12 (o device do D3D12 aceita os dois). */
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN, true);
#if AM_GPU_HAVE_RW
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_GRAPHICS_STORAGE_READ_WRITE_BOOLEAN, true);
#endif
    SDL_GPUDevice *device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    return device;
}

/* O device lê e grava storage no fragmento na ordem da rasterização (ROV, interlock, raster order groups). */
static bool raster_ordered(void) {
#if AM_GPU_HAVE_RW
    const SDL_PropertiesID dev = SDL_GetGPUDeviceProperties(s_gpu.device);
    return SDL_GetBooleanProperty(dev, SDL_PROP_GPU_DEVICE_RASTER_ORDERED_STORAGE_BOOLEAN, false);
#else
    return false;
#endif
}

static ShaderBlob blob_of(SDL_GPUShaderFormat format, const u8 *vs, size_t vs_size, const u8 *fs, size_t fs_size,
                          const char *label) {
    return (ShaderBlob){.format = format, .vs = vs, .vs_size = vs_size, .fs = fs, .fs_size = fs_size, .label = label};
}

/* Escolhe o shader e o formato. false = o pedido não roda neste device (o erro diz por quê). */
static bool pick_shader(PlatformGpuShader want, ShaderBlob *blob) {
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(s_gpu.device);
    const bool metal = (formats & SDL_GPU_SHADERFORMAT_METALLIB) != 0U;
    const bool rov = raster_ordered();
    if (want == PLATFORM_GPU_SHADER_AUTO) {
        want = metal ? PLATFORM_GPU_SHADER_HAND : PLATFORM_GPU_SHADER_HLSL;
    }
    if (want == PLATFORM_GPU_SHADER_HAND) {
        if (!metal) {
            (void)snprintf(s_gpu.error, sizeof(s_gpu.error), "o shader à mão é só para Metal (driver %s)",
                           SDL_GetGPUDeviceDriver(s_gpu.device));
            return false;
        }
        *blob = blob_of(SDL_GPU_SHADERFORMAT_METALLIB, s_gx_ubershader_metallib, sizeof(s_gx_ubershader_metallib),
                        s_gx_ubershader_metallib, sizeof(s_gx_ubershader_metallib),
                        "MSL à mão (metallib), framebuffer fetch");
        return true;
    }
    if (!rov) {
        (void)snprintf(s_gpu.error, sizeof(s_gpu.error),
                       "driver %s sem storage gravável na ordem da rasterização (ROV/interlock)%s",
                       SDL_GetGPUDeviceDriver(s_gpu.device), AM_GPU_HAVE_RW ? "" : "; SDL sem patches/sdl");
        return false;
    }
    s_gpu.hlsl = true;
    if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0U) {
        *blob = blob_of(SDL_GPU_SHADERFORMAT_DXIL, s_gx_hlsl_vs_dxil, sizeof(s_gx_hlsl_vs_dxil), s_gx_hlsl_fs_dxil,
                        sizeof(s_gx_hlsl_fs_dxil), "HLSL (DXIL), ROV");
    } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0U) {
        *blob = blob_of(SDL_GPU_SHADERFORMAT_SPIRV, s_gx_hlsl_vs_spirv, sizeof(s_gx_hlsl_vs_spirv), s_gx_hlsl_fs_spirv,
                        sizeof(s_gx_hlsl_fs_spirv), "HLSL (SPIR-V), fragment shader interlock");
    } else {
        *blob =
            blob_of(SDL_GPU_SHADERFORMAT_METALLIB, s_gx_hlsl_metallib, sizeof(s_gx_hlsl_metallib), s_gx_hlsl_metallib,
                    sizeof(s_gx_hlsl_metallib), "HLSL (MSL 2.3, metallib), raster order groups");
    }
    return true;
}

static bool init_resources(void) {
#if AM_GPU_HAVE_RW
    const SDL_GPUTextureUsageFlags efb_usage = s_gpu.hlsl
                                                   ? SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE
                                                   : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
#else
    const SDL_GPUTextureUsageFlags efb_usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
#endif
    s_gpu.efb_color = texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, efb_usage);
    s_gpu.efb_depth = texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, efb_usage);
    if (s_gpu.hlsl) {
        s_gpu.dummy = texture(SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    }
    const SDL_GPUBufferCreateInfo arena = {.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                                           .size = GPU_ARENA_TEXELS * 4U};
    s_gpu.arena = SDL_CreateGPUBuffer(s_gpu.device, &arena);
    const SDL_GPUTransferBufferCreateInfo down = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                                                  .size = 2U * EFB_BYTES};
    s_gpu.download = SDL_CreateGPUTransferBuffer(s_gpu.device, &down);
    if (s_gpu.efb_color == NULL || s_gpu.efb_depth == NULL || s_gpu.arena == NULL || s_gpu.download == NULL ||
        (s_gpu.hlsl && s_gpu.dummy == NULL)) {
        return fail("recursos da GPU");
    }
    return true;
}

bool platform_gpu_init(PlatformGpuShader want) {
    if (s_gpu.device != NULL) {
        return true;
    }
    s_gpu.scale = platform_gpu_scale();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { /* a SDL só cria o dispositivo com o vídeo iniciado */
        return fail("SDL_InitSubSystem(VIDEO)");
    }
    s_gpu.device = create_device();
    if (s_gpu.device == NULL) {
        (void)fail("SDL_CreateGPUDevice");
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    ShaderBlob blob;
    const bool picked = pick_shader(want, &blob);
    if (picked && !s_gpu.hlsl && s_gpu.scale > 1U) {
        SDL_Log("GPU: --internal-res %u só existe com o shader HLSL; seguindo em 1x", s_gpu.scale);
        s_gpu.scale = 1U;
    }
    if (!picked || !init_resources() || !create_pipeline(&blob)) {
        char error[sizeof(s_gpu.error)];
        (void)snprintf(error, sizeof(error), "%s", s_gpu.error);
        platform_gpu_shutdown();
        (void)snprintf(s_gpu.error, sizeof(s_gpu.error), "%s", error);
        return false;
    }
    (void)snprintf(s_gpu.name, sizeof(s_gpu.name), "%s, %s", SDL_GetGPUDeviceDriver(s_gpu.device), blob.label);
    return true;
}

SDL_GPUDevice *platform_gpu_device(void) {
    return s_gpu.device;
}

const char *platform_gpu_driver(void) {
    return s_gpu.device != NULL ? SDL_GetGPUDeviceDriver(s_gpu.device) : "";
}

const char *platform_gpu_adapter(void) {
    if (s_gpu.device == NULL) {
        return "";
    }
    const char *name =
        SDL_GetStringProperty(SDL_GetGPUDeviceProperties(s_gpu.device), SDL_PROP_GPU_DEVICE_NAME_STRING, NULL);
    return name != NULL ? name : "";
}

const char *platform_gpu_error(void) {
    return s_gpu.error;
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
        memcpy(map + at, b->efb_color_in, EFB_BYTES_Z);
        memcpy(map + (size_t)at + EFB_BYTES_Z, b->efb_depth_in, EFB_BYTES_Z);
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
                                                .pixels_per_row = EFB_W,
                                                .rows_per_layer = EFB_H};
        const SDL_GPUTextureRegion dst = {
            .texture = k == 0U ? s_gpu.efb_color : s_gpu.efb_depth, .w = EFB_W, .h = EFB_H, .d = 1};
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
    }
    SDL_EndGPUCopyPass(copy);
    return true;
}

static void render(SDL_GPUCommandBuffer *cmd, const GpuBatch *b) {
    if (b->draw_count == 0U) {
        return;
    }
    SDL_GPURenderPass *pass;
    if (s_gpu.hlsl) {
        const SDL_GPUColorTargetInfo dummy = {
            .texture = s_gpu.dummy, .load_op = SDL_GPU_LOADOP_DONT_CARE, .store_op = SDL_GPU_STOREOP_DONT_CARE};
        pass = SDL_BeginGPURenderPass(cmd, &dummy, 1, NULL);
    } else {
        const SDL_GPUColorTargetInfo targets[2] = {
            {.texture = s_gpu.efb_color, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE},
            {.texture = s_gpu.efb_depth, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE}};
        pass = SDL_BeginGPURenderPass(cmd, targets, 2, NULL);
    }
    SDL_BindGPUGraphicsPipeline(pass, s_gpu.pipeline);
#if AM_GPU_HAVE_RW
    if (s_gpu.hlsl) {
        const SDL_GPUStorageTextureReadWriteBinding efb[2] = {{.texture = s_gpu.efb_color},
                                                              {.texture = s_gpu.efb_depth}};
        SDL_BindGPUFragmentReadWriteStorageTextures(pass, 0, efb, 2);
    }
#endif
    const SDL_GPUBufferBinding vb = {.buffer = s_gpu.vertices, .offset = 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_BindGPUFragmentStorageBuffers(pass, 0, &s_gpu.arena, 1);
    for (u32 i = 0; i < b->draw_count; i++) {
        const GpuDraw *d = &b->draws[i];
        if (d->vertex_count == 0U || d->scissor[2] <= 0 || d->scissor[3] <= 0) {
            continue;
        }
        s32 scaled[4] = {d->scissor[0], d->scissor[1], d->scissor[2], d->scissor[3]};
        if (s_gpu.scale > 1U) {
            gpu_scale_rect(d->scissor, s_gpu.scale, scaled);
        }
        const SDL_Rect sc = {scaled[0], scaled[1], scaled[2], scaled[3]};
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
    const u32 top = b->readback_top * platform_gpu_scale(); /* linhas do EFB da GPU (o lote conta em linhas nativas) */
    const u32 rows =
        b->readback_bottom > b->readback_top ? (b->readback_bottom - b->readback_top) * platform_gpu_scale() : 0U;
    const u32 bytes = rows * EFB_W * 4U;
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    for (u32 k = 0; k < (b->efb_depth_out != NULL ? 2U : 1U) && rows != 0U; k++) {
        const SDL_GPUTextureRegion src = {
            .texture = k == 0U ? s_gpu.efb_color : s_gpu.efb_depth, .y = top, .w = EFB_W, .h = rows, .d = 1};
        const SDL_GPUTextureTransferInfo dst = {.transfer_buffer = s_gpu.download,
                                                .offset = k * EFB_BYTES,
                                                .pixels_per_row = EFB_W,
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
    const size_t at = (size_t)top * (size_t)EFB_W;
    memcpy(b->efb_color_out + at, map, bytes);
    if (b->efb_depth_out != NULL) {
        memcpy(b->efb_depth_out + at, map + EFB_BYTES_Z, bytes);
    }
    SDL_UnmapGPUTransferBuffer(s_gpu.device, s_gpu.download);
    return true;
}

void platform_gpu_shutdown(void) {
    if (s_gpu.device == NULL) {
        return;
    }
    platform_window_release_gpu(); /* M9 parte 5: a janela pode estar no swapchain deste device */
    SDL_WaitForGPUIdle(s_gpu.device);
    if (s_gpu.download != NULL) {
        SDL_ReleaseGPUTransferBuffer(s_gpu.device, s_gpu.download);
    }
    if (s_gpu.upload != NULL) {
        SDL_ReleaseGPUTransferBuffer(s_gpu.device, s_gpu.upload);
    }
    if (s_gpu.vertices != NULL) {
        SDL_ReleaseGPUBuffer(s_gpu.device, s_gpu.vertices);
    }
    if (s_gpu.arena != NULL) {
        SDL_ReleaseGPUBuffer(s_gpu.device, s_gpu.arena);
    }
    SDL_GPUTexture *textures[3] = {s_gpu.efb_color, s_gpu.efb_depth, s_gpu.dummy};
    for (u32 i = 0; i < 3U; i++) {
        if (textures[i] != NULL) {
            SDL_ReleaseGPUTexture(s_gpu.device, textures[i]);
        }
    }
    if (s_gpu.pipeline != NULL) {
        SDL_ReleaseGPUGraphicsPipeline(s_gpu.device, s_gpu.pipeline);
    }
    SDL_DestroyGPUDevice(s_gpu.device);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    memset(&s_gpu, 0, sizeof(s_gpu));
}
