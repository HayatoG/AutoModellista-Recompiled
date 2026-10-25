// Teste mínimo do M9 (parte 1b): fragment shader com RasterizerOrderedTexture2D (ROV).
// Lê o texel, soma 1 e grava, na ordem dos primitivos. Binding explícito no layout da SDL GPU:
// storage textures graváveis de fragment shader ficam no set 2.
// Entradas: vs_main (vertex) e ps_main (fragment).

[[vk::binding(0, 2)]] RasterizerOrderedTexture2D<uint> efb : register(u0, space2);

struct VSOut {
    float4 pos : SV_Position;
};

VSOut vs_main(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

void ps_main(VSOut i)
{
    uint2 p = uint2(i.pos.xy);
    uint v = efb[p];
    efb[p] = v + 1;
}
