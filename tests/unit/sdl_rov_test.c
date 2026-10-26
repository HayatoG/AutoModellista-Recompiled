/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M9 partes 1b e 3a: o patch da SDL (patches/sdl/) de ponta a ponta. Um fragment shader lê e grava uma textura
 * R32_UINT de storage (SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE) na ordem da rasterização: centenas de
 * triângulos sobrepostos cobrem a textura inteira e cada um faz v = v * 31 + (tri + 1). O resultado só bate com a
 * conta da CPU se os fragmentos de cada pixel rodarem na ordem da API (dentro de um desenho, entre desenhos e entre
 * passes). Também confere a numeração dos registros: textura de storage só de leitura primeiro (a semente) e a
 * gravável logo depois (docs/M9_PARTE1B_SDL.md §2).
 *
 * Shader por backend: no Metal, MSL à mão com [[raster_order_group(0)]] ([[texture(0)]] e [[texture(1)]]); no Vulkan
 * e no D3D12, o SPIR-V e o DXIL de tests/shaders/rov_min.hlsl (RasterizerOrderedTexture2D, interlock no SPIR-V; set
 * 2, bindings 0 e 1), embutidos em tests/shaders/rov_min_shaders.h (docs/M9_PARTE3A_LINUX.md §3).
 *
 *   sdl_rov_test             o teste (sai com 77, "pulado", sem SDL remendada ou sem GPU)
 *   sdl_rov_test --no-order  o mesmo shader sem a ordem (sem raster_order_group / sem interlock): só informa quantos
 *                            pixels saíram errados (--no-rog é o nome antigo) */
#include <stdio.h>
#include <string.h>

#include "am/types.h"

#include <SDL3/SDL.h>

#define ROV_SIZE   64U
#define ROV_PIXELS (ROV_SIZE * ROV_SIZE)
#define ROV_SKIP   77

/* Triângulos por desenho: o passe 1 tem dois desenhos, o passe 2 um. */
#define ROV_TRIS_DRAW1 192U
#define ROV_TRIS_DRAW2 64U
#define ROV_TRIS_PASS2 128U
#define ROV_TRIS_TOTAL (ROV_TRIS_DRAW1 + ROV_TRIS_DRAW2 + ROV_TRIS_PASS2)

#ifdef SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE

#include "../shaders/rov_min_shaders.h"
#include "am_test.h"

/* Formatos que o teste oferece à SDL; ela escolhe o backend e o teste, o shader do formato que veio. */
#define ROV_FORMATS (SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL)

/* MSL à mão para o Metal (o metallib do HLSL precisaria do Xcode no build). %s vira o atributo de ordem ou nada.
 * Cada triângulo cobre a textura toda; o índice do triângulo vem do vertex_id, que no Metal já inclui o
 * first_vertex do desenho. O color target é só para o passe ter um attachment (o Metal pede tamanho). */
static const char s_msl_fmt[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VOut { float4 pos [[position]]; uint tri [[flat]]; };\n"
    "vertex VOut rov_vs(uint vid [[vertex_id]]) {\n"
    "    const float2 p[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };\n"
    "    VOut o; o.pos = float4(p[vid %% 3u], 0.0, 1.0); o.tri = vid / 3u; return o;\n"
    "}\n"
    "fragment float4 rov_fs(VOut in [[stage_in]],\n"
    "                       texture2d<uint, access::read> seed [[texture(0)]],\n"
    "                       texture2d<uint, access::read_write> counter [[texture(1)%s]]) {\n"
    "    uint2 c = uint2(in.pos.xy);\n"
    "    uint v = in.tri == 0u ? seed.read(c).r : counter.read(c).r;\n"
    "    counter.write(uint4(v * 31u + in.tri + 1u, 0u, 0u, 0u), c);\n"
    "    return float4(1.0);\n"
    "}\n";

static struct {
    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUTexture *seed;
    SDL_GPUTexture *counter;
    SDL_GPUTexture *target;
    u32 seed_texels[ROV_PIXELS];
    u32 result[ROV_PIXELS];
} s_rov;

static u32 seed_of(u32 i) {
    return i * 2654435761U ^ 0x9E3779B9U;
}

typedef struct RovCode {
    const u8 *code;
    size_t size;
    const char *entry;
} RovCode;

static SDL_GPUShader *make_shader(SDL_GPUShaderFormat format, RovCode c, SDL_GPUShaderStage stage, u32 storage,
                                  u32 rw) {
    const SDL_PropertiesID props = SDL_CreateProperties();
    (void)SDL_SetNumberProperty(props, SDL_PROP_GPU_SHADER_CREATE_NUM_READWRITE_STORAGE_TEXTURES_NUMBER, rw);
    const SDL_GPUShaderCreateInfo info = {.code_size = c.size,
                                          .code = c.code,
                                          .entrypoint = c.entry,
                                          .format = format,
                                          .stage = stage,
                                          .num_storage_textures = storage,
                                          .props = props};
    SDL_GPUShader *sh = SDL_CreateGPUShader(s_rov.device, &info);
    SDL_DestroyProperties(props);
    return sh;
}

/* O shader do formato do device; ordered = com raster_order_group / interlock / ROV. */
static bool make_pipeline(bool ordered) {
    static char src[sizeof(s_msl_fmt) + 32];
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(s_rov.device);
    SDL_GPUShaderFormat format;
    RovCode vc;
    RovCode fc;
    if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0U) {
        (void)snprintf(src, sizeof(src), s_msl_fmt, ordered ? ", raster_order_group(0)" : "");
        format = SDL_GPU_SHADERFORMAT_MSL;
        vc = (RovCode){(const u8 *)src, strlen(src), "rov_vs"};
        fc = (RovCode){(const u8 *)src, strlen(src), "rov_fs"};
    } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0U) {
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        vc = (RovCode){s_rov_vs_spirv, sizeof(s_rov_vs_spirv), "vs_main"};
        fc = ordered ? (RovCode){s_rov_fs_spirv, sizeof(s_rov_fs_spirv), "ps_main"}
                     : (RovCode){s_rov_fs_nolock_spirv, sizeof(s_rov_fs_nolock_spirv), "ps_main"};
    } else {
        format = SDL_GPU_SHADERFORMAT_DXIL;
        vc = (RovCode){s_rov_vs_dxil, sizeof(s_rov_vs_dxil), "vs_main"};
        fc = ordered ? (RovCode){s_rov_fs_dxil, sizeof(s_rov_fs_dxil), "ps_main"}
                     : (RovCode){s_rov_fs_nolock_dxil, sizeof(s_rov_fs_nolock_dxil), "ps_main"};
    }
    SDL_GPUShader *vs = make_shader(format, vc, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader *fs = make_shader(format, fc, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (vs == NULL || fs == NULL) {
        (void)printf("  shader: %s\n", SDL_GetError());
        return false;
    }
    (void)printf("  shader: %s\n", format == SDL_GPU_SHADERFORMAT_MSL     ? "MSL à mão"
                                   : format == SDL_GPU_SHADERFORMAT_SPIRV ? "SPIR-V de rov_min.hlsl"
                                                                          : "DXIL de rov_min.hlsl");
    const SDL_GPUColorTargetDescription target = {.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
    const SDL_GPUGraphicsPipelineCreateInfo info = {
        .vertex_shader = vs,
        .fragment_shader = fs,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE},
        .target_info = {.color_target_descriptions = &target, .num_color_targets = 1}};
    s_rov.pipeline = SDL_CreateGPUGraphicsPipeline(s_rov.device, &info);
    SDL_ReleaseGPUShader(s_rov.device, vs);
    SDL_ReleaseGPUShader(s_rov.device, fs);
    return s_rov.pipeline != NULL;
}

static SDL_GPUTexture *make_texture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage) {
    const SDL_GPUTextureCreateInfo info = {.type = SDL_GPU_TEXTURETYPE_2D,
                                           .format = format,
                                           .usage = usage,
                                           .width = ROV_SIZE,
                                           .height = ROV_SIZE,
                                           .layer_count_or_depth = 1,
                                           .num_levels = 1};
    return SDL_CreateGPUTexture(s_rov.device, &info);
}

/* Sobe a semente e enche o contador de lixo (o triângulo 0 o ignora): exercita a cópia para a textura nova. */
static void upload(SDL_GPUCommandBuffer *cmd) {
    const SDL_GPUTransferBufferCreateInfo tinfo = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                   .size = 2U * ROV_PIXELS * 4U};
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(s_rov.device, &tinfo);
    u32 *map = SDL_MapGPUTransferBuffer(s_rov.device, tb, false);
    for (u32 i = 0; i < ROV_PIXELS; i++) {
        map[i] = s_rov.seed_texels[i];
        map[ROV_PIXELS + i] = 0xDEADBEEFU;
    }
    SDL_UnmapGPUTransferBuffer(s_rov.device, tb);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTextureTransferInfo src_seed = {.transfer_buffer = tb, .offset = 0};
    const SDL_GPUTextureTransferInfo src_counter = {.transfer_buffer = tb, .offset = ROV_PIXELS * 4U};
    const SDL_GPUTextureRegion seed = {.texture = s_rov.seed, .w = ROV_SIZE, .h = ROV_SIZE, .d = 1};
    const SDL_GPUTextureRegion counter = {.texture = s_rov.counter, .w = ROV_SIZE, .h = ROV_SIZE, .d = 1};
    SDL_UploadToGPUTexture(copy, &src_seed, &seed, false);
    SDL_UploadToGPUTexture(copy, &src_counter, &counter, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(s_rov.device, tb);
}

static void draw_pass(SDL_GPUCommandBuffer *cmd, u32 first_tri, const u32 *tris, u32 draws) {
    const SDL_GPUColorTargetInfo color = {
        .texture = s_rov.target, .load_op = SDL_GPU_LOADOP_DONT_CARE, .store_op = SDL_GPU_STOREOP_DONT_CARE};
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color, 1, NULL);
    SDL_BindGPUGraphicsPipeline(pass, s_rov.pipeline);
    SDL_BindGPUFragmentStorageTextures(pass, 0, &s_rov.seed, 1);
    const SDL_GPUStorageTextureReadWriteBinding rw = {.texture = s_rov.counter, .mip_level = 0, .layer = 0};
    SDL_BindGPUFragmentReadWriteStorageTextures(pass, 0, &rw, 1);
    for (u32 d = 0; d < draws; d++) {
        SDL_DrawGPUPrimitives(pass, tris[d] * 3U, 1, first_tri * 3U, 0);
        first_tri += tris[d];
    }
    SDL_EndGPURenderPass(pass);
}

/* Dois passes (o primeiro com dois desenhos) e a leitura do contador. Devolve false se a GPU falhar. */
static bool run(void) {
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(s_rov.device);
    upload(cmd);
    const u32 pass1[2] = {ROV_TRIS_DRAW1, ROV_TRIS_DRAW2};
    const u32 pass2[1] = {ROV_TRIS_PASS2};
    draw_pass(cmd, 0, pass1, 2);
    draw_pass(cmd, ROV_TRIS_DRAW1 + ROV_TRIS_DRAW2, pass2, 1);

    const SDL_GPUTransferBufferCreateInfo dinfo = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                                                   .size = ROV_PIXELS * 4U};
    SDL_GPUTransferBuffer *down = SDL_CreateGPUTransferBuffer(s_rov.device, &dinfo);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTextureRegion src = {.texture = s_rov.counter, .w = ROV_SIZE, .h = ROV_SIZE, .d = 1};
    const SDL_GPUTextureTransferInfo dst = {.transfer_buffer = down, .offset = 0};
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == NULL || !SDL_WaitForGPUFences(s_rov.device, true, &fence, 1)) {
        (void)printf("  GPU: %s\n", SDL_GetError());
        return false;
    }
    SDL_ReleaseGPUFence(s_rov.device, fence);
    const u32 *map = SDL_MapGPUTransferBuffer(s_rov.device, down, false);
    memcpy(s_rov.result, map, sizeof(s_rov.result));
    SDL_UnmapGPUTransferBuffer(s_rov.device, down);
    SDL_ReleaseGPUTransferBuffer(s_rov.device, down);
    return true;
}

/* A mesma conta na CPU, na ordem da API; devolve quantos pixels diferem. */
static u32 count_wrong(void) {
    u32 wrong = 0;
    for (u32 i = 0; i < ROV_PIXELS; i++) {
        u32 v = s_rov.seed_texels[i];
        for (u32 t = 0; t < ROV_TRIS_TOTAL; t++) {
            v = v * 31U + t + 1U;
        }
        wrong += s_rov.result[i] != v ? 1U : 0U;
    }
    return wrong;
}

static int open_device(void) {
    /* Sem tela (CI, Linux sem X11/Wayland), o driver offscreen da SDL ainda carrega o Vulkan. */
    if (!SDL_Init(SDL_INIT_VIDEO) && (!SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen") || !SDL_Init(SDL_INIT_VIDEO))) {
        (void)printf("pulado: SDL_Init(VIDEO): %s\n", SDL_GetError());
        return ROV_SKIP;
    }
    const SDL_PropertiesID props = SDL_CreateProperties();
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true);
    (void)SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_GRAPHICS_STORAGE_READ_WRITE_BOOLEAN, true);
    s_rov.device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (s_rov.device == NULL) {
        (void)printf("pulado: sem GPU com MSL, SPIR-V ou DXIL: %s\n", SDL_GetError());
        return ROV_SKIP;
    }
    const SDL_PropertiesID dev = SDL_GetGPUDeviceProperties(s_rov.device);
    (void)printf("GPU: %s (%s), read-write no fragmento: %d, ordem de rasterização: %d\n",
                 SDL_GetStringProperty(dev, SDL_PROP_GPU_DEVICE_NAME_STRING, "?"), SDL_GetGPUDeviceDriver(s_rov.device),
                 SDL_GetBooleanProperty(dev, SDL_PROP_GPU_DEVICE_GRAPHICS_STORAGE_READ_WRITE_BOOLEAN, false),
                 SDL_GetBooleanProperty(dev, SDL_PROP_GPU_DEVICE_RASTER_ORDERED_STORAGE_BOOLEAN, false));
    return 0;
}

/* Sem pedir o recurso na criação, a flag nova é recusada (quem não usa o patch não vê diferença). */
TEST(recurso_desligado_por_padrao) {
    SDL_GPUDevice *plain = SDL_CreateGPUDevice(ROV_FORMATS, false, NULL);
    EXPECT_NOT_NULL(plain);
    if (plain == NULL) {
        return;
    }
    const SDL_PropertiesID dev = SDL_GetGPUDeviceProperties(plain);
    EXPECT_TRUE(!SDL_GetBooleanProperty(dev, SDL_PROP_GPU_DEVICE_GRAPHICS_STORAGE_READ_WRITE_BOOLEAN, false));
    EXPECT_TRUE(!SDL_GPUTextureSupportsFormat(plain, SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTURETYPE_2D,
                                              SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE));
    const SDL_GPUTextureCreateInfo info = {.type = SDL_GPU_TEXTURETYPE_2D,
                                           .format = SDL_GPU_TEXTUREFORMAT_R32_UINT,
                                           .usage = SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE,
                                           .width = 4,
                                           .height = 4,
                                           .layer_count_or_depth = 1,
                                           .num_levels = 1};
    EXPECT_NULL(SDL_CreateGPUTexture(plain, &info));
    SDL_DestroyGPUDevice(plain);
}

TEST(contador_na_ordem_da_api) {
    const SDL_PropertiesID dev = SDL_GetGPUDeviceProperties(s_rov.device);
    EXPECT_TRUE(SDL_GetBooleanProperty(dev, SDL_PROP_GPU_DEVICE_GRAPHICS_STORAGE_READ_WRITE_BOOLEAN, false));
    EXPECT_TRUE(SDL_GetBooleanProperty(dev, SDL_PROP_GPU_DEVICE_RASTER_ORDERED_STORAGE_BOOLEAN, false));
    EXPECT_TRUE(SDL_GPUTextureSupportsFormat(s_rov.device, SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTURETYPE_2D,
                                             SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE));
    const bool pipeline = make_pipeline(true);
    EXPECT_TRUE(pipeline);
    if (!pipeline) { /* desenhar sem pipeline dispara o assert da SDL (modo debug) e o teste trava */
        (void)printf("  pipeline: %s\n", SDL_GetError());
        return;
    }
    EXPECT_TRUE(run());
    const u32 wrong = count_wrong();
    (void)printf("  %u triângulos por pixel em 3 desenhos e 2 passes: %u de %u pixels diferentes da CPU\n",
                 ROV_TRIS_TOTAL, wrong, ROV_PIXELS);
    EXPECT_EQ(wrong, 0);
    EXPECT_EQ(s_rov.result[0] != 0xDEADBEEFU, 1); /* a semente veio da textura só de leitura, não do lixo */
}

int main(int argc, char **argv) {
    const bool no_order = argc > 1 && (strcmp(argv[1], "--no-order") == 0 || strcmp(argv[1], "--no-rog") == 0);
    for (u32 i = 0; i < ROV_PIXELS; i++) {
        s_rov.seed_texels[i] = seed_of(i);
    }
    const int skip = open_device();
    if (skip != 0) {
        return skip;
    }
    s_rov.seed = make_texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ);
    s_rov.counter = make_texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE);
    s_rov.target = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    if (s_rov.seed == NULL || s_rov.counter == NULL || s_rov.target == NULL) {
        (void)printf("texturas: %s\n", SDL_GetError());
        return 1;
    }
    if (no_order) {
        const bool ok = make_pipeline(false) && run();
        (void)printf("sem ordem (sem raster_order_group / interlock): %u de %u pixels diferentes da CPU\n",
                     ok ? count_wrong() : ROV_PIXELS, ROV_PIXELS);
        return ok ? 0 : 1;
    }
    RUN_TEST(recurso_desligado_por_padrao);
    RUN_TEST(contador_na_ordem_da_api);
    SDL_ReleaseGPUGraphicsPipeline(s_rov.device, s_rov.pipeline);
    SDL_ReleaseGPUTexture(s_rov.device, s_rov.seed);
    SDL_ReleaseGPUTexture(s_rov.device, s_rov.counter);
    SDL_ReleaseGPUTexture(s_rov.device, s_rov.target);
    SDL_DestroyGPUDevice(s_rov.device);
    SDL_Quit();
    return am_test_finish();
}

#else /* SDL sem o patch (AM_SDL3_SYSTEM=ON): o recurso não existe */

int main(void) {
    (void)printf("pulado: a SDL em uso não tem SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE (patches/sdl)\n");
    return ROV_SKIP;
}

#endif
