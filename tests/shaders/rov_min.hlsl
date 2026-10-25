// Teste do patch da SDL (M9, partes 1b e 3a): fragment shader com RasterizerOrderedTexture2D (ROV). É o mesmo
// teste do MSL à mão de tests/unit/sdl_rov_test.c: cada triângulo cobre a textura toda e faz
// v = v * 31 + (tri + 1), partindo da semente no triângulo 0; o resultado só bate com a CPU se os fragmentos de cada
// pixel rodarem na ordem da API. Bindings explícitos no layout da SDL GPU (docs/M9_PARTE1B_SDL.md §2): no set 2, a
// textura de storage só de leitura (que a SDL Vulkan amarra como sampled image) no binding 0 e a gravável logo
// depois, no 1.
// -DAM_NO_INTERLOCK: a mesma conta numa RWTexture2D, sem ordem (o controle do teste, sdl_rov_test --no-order).
// Entradas: vs_main (vertex) e ps_main (fragment). Gerado com scripts/build_shaders.sh rov.

[[vk::binding(0, 2)]] Texture2D<uint> seed : register(t0, space2);
#ifdef AM_NO_INTERLOCK
[[vk::binding(1, 2)]] [[vk::image_format("r32ui")]] RWTexture2D<uint> counter : register(u0, space2);
#else
[[vk::binding(1, 2)]] [[vk::image_format("r32ui")]] RasterizerOrderedTexture2D<uint> counter : register(u0, space2);
#endif

struct VSOut {
    float4 pos : SV_Position;
    nointerpolation uint tri : TEXCOORD0;
};

// O índice do triângulo vem do índice do vértice, que no Vulkan (VertexIndex) já inclui o first_vertex do desenho.
VSOut vs_main(uint id : SV_VertexID)
{
    const float2 p[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VSOut o;
    o.pos = float4(p[id % 3u], 0.0, 1.0);
    o.tri = id / 3u;
    return o;
}

void ps_main(VSOut i)
{
    uint2 c = uint2(i.pos.xy);
    // Leitura incondicional: um só par Begin/End de interlock, fora de desvios (docs/M9_PARTE1B_HLSL.md §3.3).
    uint cur = counter[c];
    uint v = i.tri == 0u ? seed[c] : cur;
    counter[c] = v * 31u + i.tri + 1u;
}
