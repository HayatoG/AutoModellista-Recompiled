#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct type_DrawState
{
    uint d_stages;
    uint d_texgens;
    uint d_ind_stages;
    uint d_colchans;
    uint d_zmode;
    uint d_pe_control;
    uint d_blend;
    uint d_dst_alpha;
    uint d_alpha_test;
    uint d_fog3;
    uint d_fog_color;
    uint d_fog_bmag;
    uint d_fog_bshift;
    uint d_ztex2;
    uint d_ztex_bias;
    uint d_flags;
    float d_fog_a;
    float d_fog_c;
    float d_vp_wd;
    float d_pad0;
    uint4 d_fog_range[2];
    uint4 d_cc[4];
    uint4 d_ac[4];
    uint4 d_order[4];
    uint4 d_ksel[2];
    int4 d_regs[4];
    int4 d_konst[4];
    uint4 d_tex_size[2];
    uint4 d_tex_mode0[2];
    uint4 d_tex_mode1[2];
    uint4 d_tex_levels[2];
    uint4 d_tex_base[2];
};

struct type_ByteAddressBuffer
{
    uint _m0[1];
};

struct gx_vs_out
{
    float out_var_TEXCOORD0 [[user(locn0)]];
    float4 out_var_TEXCOORD1 [[user(locn1)]];
    float4 out_var_TEXCOORD2 [[user(locn2)]];
    float3 out_var_TEXCOORD3 [[user(locn3)]];
    float3 out_var_TEXCOORD4 [[user(locn4)]];
    float3 out_var_TEXCOORD5 [[user(locn5)]];
    float3 out_var_TEXCOORD6 [[user(locn6)]];
    float3 out_var_TEXCOORD7 [[user(locn7)]];
    float3 out_var_TEXCOORD8 [[user(locn8)]];
    float3 out_var_TEXCOORD9 [[user(locn9)]];
    float3 out_var_TEXCOORD10 [[user(locn10)]];
    float4 gl_Position [[position]];
};

struct gx_vs_in
{
    float4 in_var_TEXCOORD0 [[attribute(0)]];
    float4 in_var_TEXCOORD1 [[attribute(1)]];
    float4 in_var_TEXCOORD2 [[attribute(2)]];
    float4 in_var_TEXCOORD3 [[attribute(3)]];
    float4 in_var_TEXCOORD4 [[attribute(4)]];
    float4 in_var_TEXCOORD5 [[attribute(5)]];
    float4 in_var_TEXCOORD6 [[attribute(6)]];
    float4 in_var_TEXCOORD7 [[attribute(7)]];
    float4 in_var_TEXCOORD8 [[attribute(8)]];
    float4 in_var_TEXCOORD9 [[attribute(9)]];
    float4 in_var_TEXCOORD10 [[attribute(10)]];
};

vertex gx_vs_out gx_vs(gx_vs_in in [[stage_in]])
{
    gx_vs_out out = {};
    float _79 = (in.in_var_TEXCOORD0.w > 0.0) ? in.in_var_TEXCOORD0.w : 1.0;
    out.gl_Position = float4(((in.in_var_TEXCOORD0.x * 0.00312500004656612873077392578125) - 1.0) * _79, (1.0 - (in.in_var_TEXCOORD0.y * 0.0037878789007663726806640625)) * _79, 0.5 * _79, _79);
    out.out_var_TEXCOORD0 = in.in_var_TEXCOORD0.z;
    out.out_var_TEXCOORD1 = in.in_var_TEXCOORD1;
    out.out_var_TEXCOORD2 = in.in_var_TEXCOORD2;
    out.out_var_TEXCOORD3 = in.in_var_TEXCOORD3.xyz;
    out.out_var_TEXCOORD4 = in.in_var_TEXCOORD4.xyz;
    out.out_var_TEXCOORD5 = in.in_var_TEXCOORD5.xyz;
    out.out_var_TEXCOORD6 = in.in_var_TEXCOORD6.xyz;
    out.out_var_TEXCOORD7 = in.in_var_TEXCOORD7.xyz;
    out.out_var_TEXCOORD8 = in.in_var_TEXCOORD8.xyz;
    out.out_var_TEXCOORD9 = in.in_var_TEXCOORD9.xyz;
    out.out_var_TEXCOORD10 = in.in_var_TEXCOORD10.xyz;
    return out;
}

