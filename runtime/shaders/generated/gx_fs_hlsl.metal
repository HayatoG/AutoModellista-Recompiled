#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wmissing-braces"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

template<typename T, size_t Num>
struct spvUnsafeArray
{
    T elements[Num ? Num : 1];
    
    thread T& operator [] (size_t pos) thread
    {
        return elements[pos];
    }
    constexpr const thread T& operator [] (size_t pos) const thread
    {
        return elements[pos];
    }
    
    device T& operator [] (size_t pos) device
    {
        return elements[pos];
    }
    constexpr const device T& operator [] (size_t pos) const device
    {
        return elements[pos];
    }
    
    constexpr const constant T& operator [] (size_t pos) const constant
    {
        return elements[pos];
    }
    
    threadgroup T& operator [] (size_t pos) threadgroup
    {
        return elements[pos];
    }
    constexpr const threadgroup T& operator [] (size_t pos) const threadgroup
    {
        return elements[pos];
    }
};

template <typename ImageT>
void spvImageFence(ImageT img) { img.fence(); }

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

constant spvUnsafeArray<int, 8> _181 = spvUnsafeArray<int, 8>({ 255, 223, 191, 159, 128, 96, 64, 32 });
constant spvUnsafeArray<int, 4> _182 = spvUnsafeArray<int, 4>({ 0, 128, -128, 0 });
constant spvUnsafeArray<int, 4> _183 = spvUnsafeArray<int, 4>({ 0, 1, 2, 0 });
constant spvUnsafeArray<int, 4> _184 = spvUnsafeArray<int, 4>({ 0, 0, 0, 1 });
constant spvUnsafeArray<uint, 4> _185 = spvUnsafeArray<uint, 4>({ 0u, 2u, 3u, 1u });
constant spvUnsafeArray<int4, 2> _190 = spvUnsafeArray<int4, 2>({ int4(0), int4(0) });
constant spvUnsafeArray<uint, 4> _191 = spvUnsafeArray<uint, 4>({ 7u, 8u, 6u, 9u });

struct gx_fs_in
{
    float in_var_TEXCOORD0 [[user(locn0), center_no_perspective]];
    float4 in_var_TEXCOORD1 [[user(locn1), center_no_perspective]];
    float4 in_var_TEXCOORD2 [[user(locn2), center_no_perspective]];
    float3 in_var_TEXCOORD3 [[user(locn3)]];
    float3 in_var_TEXCOORD4 [[user(locn4)]];
    float3 in_var_TEXCOORD5 [[user(locn5)]];
    float3 in_var_TEXCOORD6 [[user(locn6)]];
    float3 in_var_TEXCOORD7 [[user(locn7)]];
    float3 in_var_TEXCOORD8 [[user(locn8)]];
    float3 in_var_TEXCOORD9 [[user(locn9)]];
    float3 in_var_TEXCOORD10 [[user(locn10)]];
};

fragment void gx_fs(gx_fs_in in [[stage_in]], constant type_DrawState& DrawState [[buffer(0)]], const device type_ByteAddressBuffer& arena [[buffer(1)]], texture2d<uint, access::read_write> efb_color [[texture(0), raster_order_group(0)]], texture2d<uint, access::read_write> efb_depth [[texture(1), raster_order_group(0)]], float4 gl_FragCoord [[position]])
{
    uint2 _228 = uint2(gl_FragCoord.xy);
    bool _232 = (DrawState.d_flags & 1u) != 0u;
    uint4 _2114;
    uint _2115;
    uint _2116;
    bool _2117;
    if (!_232)
    {
        spvUnsafeArray<float3, 8> _236 = spvUnsafeArray<float3, 8>({ in.in_var_TEXCOORD3, in.in_var_TEXCOORD4, in.in_var_TEXCOORD5, in.in_var_TEXCOORD6, in.in_var_TEXCOORD7, in.in_var_TEXCOORD8, in.in_var_TEXCOORD9, in.in_var_TEXCOORD10 });
        spvUnsafeArray<float3, 8> _209 = _236;
        spvUnsafeArray<float2, 8> _208;
        spvUnsafeArray<float2, 8> _210;
        spvUnsafeArray<float2, 8> _211;
        for (uint _238 = 0u; _238 < 8u; _238++)
        {
            float2 _259;
            if (_209[_238].z != 0.0)
            {
                _259 = _209[_238].xy / float2(_209[_238].z);
            }
            else
            {
                _259 = _209[_238].xy;
            }
            _208[_238] = _259;
            _210[_238] = dfdx(_208[_238]);
            _211[_238] = dfdy(_208[_238]);
        }
        int _268 = int(fast::clamp(in.in_var_TEXCOORD0, 0.0, 16777215.0));
        spvUnsafeArray<float2, 8> _212 = _208;
        spvUnsafeArray<float2, 8> _213 = _210;
        spvUnsafeArray<float2, 8> _214 = _211;
        spvUnsafeArray<int4, 2> _200 = spvUnsafeArray<int4, 2>({ int4(0), int4(0) });
        if (DrawState.d_colchans > 0u)
        {
            _200[0] = int4(fast::clamp(in.in_var_TEXCOORD1, float4(0.0), float4(255.0)));
        }
        if (DrawState.d_colchans > 1u)
        {
            _200[1] = int4(fast::clamp(in.in_var_TEXCOORD2, float4(0.0), float4(255.0)));
        }
        spvUnsafeArray<int4, 10> _201;
        for (uint _287 = 0u; _287 < 4u; )
        {
            _201[_287] = DrawState.d_regs[_287];
            _287++;
            continue;
        }
        _201[4] = int4(0);
        _201[5] = int4(0);
        _201[6] = int4(0);
        _201[7] = int4(255);
        _201[8] = int4(128);
        _201[9] = int4(0);
        int4 _302;
        _302 = int4(0);
        uint _312;
        spvUnsafeArray<uint, 4> CS;
        spvUnsafeArray<int4, 4> _204;
        int4 _303;
        bool _308;
        uint _305 = 0u;
        bool _307 = false;
        for (;;)
        {
            _312 = DrawState.d_stages;
            bool _317;
            if (_305 < _312)
            {
                _317 = _305 < 16u;
            }
            else
            {
                _317 = false;
            }
            if (_317)
            {
                uint _319 = _305 >> 2u;
                uint _320 = _305 & 3u;
                uint _329 = (DrawState.d_order[_319][_320] >> 3u) & 7u;
                uint _333 = (_329 >= DrawState.d_texgens) ? 0u : _329;
                if (((DrawState.d_order[_319][_320] >> 6u) & 1u) != 0u)
                {
                    uint4 _1218;
                    if (DrawState.d_texgens > 0u)
                    {
                        uint _342 = (DrawState.d_order[_319][_320] & 7u) >> 2u;
                        uint _343 = DrawState.d_order[_319][_320] & 3u;
                        float _353 = abs(_213[_333].x);
                        float _355 = abs(_213[_333].y);
                        float _357 = abs(_214[_333].x);
                        float _359 = abs(_214[_333].y);
                        float _370;
                        float _371;
                        if (((DrawState.d_tex_mode0[_342][_343] >> 8u) & 1u) != 0u)
                        {
                            _370 = _355 + _359;
                            _371 = _353 + _357;
                        }
                        else
                        {
                            _370 = precise::max(_355, _359);
                            _371 = precise::max(_353, _357);
                        }
                        uint _373 = as_type<uint>(precise::max(_371, _370));
                        int _386 = ((int((_373 & 2139095040u) >> 19u) - 2032) + int((_373 & 8388607u) >> 19u)) + ((int(DrawState.d_tex_mode0[_342][_343] << 15u) >> 24) >> 1);
                        bool _393;
                        if (_386 > 0)
                        {
                            _393 = ((DrawState.d_tex_mode0[_342][_343] >> 7u) & 1u) == 1u;
                        }
                        else
                        {
                            _393 = false;
                        }
                        bool _404;
                        if (!_393)
                        {
                            bool _403;
                            if (_386 <= 0)
                            {
                                _403 = ((DrawState.d_tex_mode0[_342][_343] >> 4u) & 1u) == 1u;
                            }
                            else
                            {
                                _403 = false;
                            }
                            _404 = _403;
                        }
                        else
                        {
                            _404 = true;
                        }
                        int _407 = int((DrawState.d_tex_mode1[_342][_343] >> 8u) & 255u);
                        int _409 = int(DrawState.d_tex_mode1[_342][_343] & 255u);
                        int _416;
                        if (_386 > _407)
                        {
                            _416 = _407;
                        }
                        else
                        {
                            _416 = (_386 < _409) ? _409 : _386;
                        }
                        int _424 = int(uint(int(_212[_333].x * 128.0)) << 8u) >> 8;
                        int _432 = int(uint(int(_212[_333].y * 128.0)) << 8u) >> 8;
                        uint4 _1217;
                        do
                        {
                            uint _436 = (DrawState.d_tex_mode0[_342][_343] >> 5u) & 3u;
                            int _437 = _416 & 15;
                            bool _442;
                            if (_416 > 0)
                            {
                                _442 = _436 != 0u;
                            }
                            else
                            {
                                _442 = false;
                            }
                            uint _461;
                            bool _462;
                            if (_442)
                            {
                                uint _446 = uint(_416 >> 4);
                                bool _451;
                                if (_437 != 0)
                                {
                                    _451 = _436 == 2u;
                                }
                                else
                                {
                                    _451 = false;
                                }
                                bool _456;
                                if (_436 == 1u)
                                {
                                    _456 = _437 >= 8;
                                }
                                else
                                {
                                    _456 = false;
                                }
                                uint _460;
                                if (_456)
                                {
                                    _460 = _446 + 1u;
                                }
                                else
                                {
                                    _460 = _446;
                                }
                                _461 = _460;
                                _462 = _451;
                            }
                            else
                            {
                                _461 = 0u;
                                _462 = false;
                            }
                            if (!_462)
                            {
                                uint4 _714;
                                do
                                {
                                    uint _470 = DrawState.d_tex_size[_342][_343] & 65535u;
                                    uint _471 = DrawState.d_tex_size[_342][_343] >> 16u;
                                    if (_461 >= DrawState.d_tex_levels[_342][_343])
                                    {
                                        _714 = uint4(0u);
                                        break;
                                    }
                                    uint _480;
                                    _480 = DrawState.d_tex_base[_342][_343];
                                    for (uint _483 = 0u; _483 < _461; )
                                    {
                                        uint _487 = _483 & 31u;
                                        _480 += (((_470 >> _487) + 1u) * ((_471 >> _487) + 1u));
                                        _483++;
                                        continue;
                                    }
                                    uint _493 = _461 & 31u;
                                    int _495 = int(_470 >> _493);
                                    int _496 = _495 + 1;
                                    int _498 = int(_471 >> _493);
                                    int _499 = _498 + 1;
                                    uint _500 = DrawState.d_tex_mode0[_342][_343] & 3u;
                                    uint _502 = (DrawState.d_tex_mode0[_342][_343] >> 2u) & 3u;
                                    if (!_404)
                                    {
                                        int _506 = _432 >> 7;
                                        int _524;
                                        do
                                        {
                                            if (_502 == 1u)
                                            {
                                                _524 = _506 & _498;
                                                break;
                                            }
                                            if (_502 == 2u)
                                            {
                                                int _521;
                                                if ((_506 & _499) != 0)
                                                {
                                                    _521 = ~_506;
                                                }
                                                else
                                                {
                                                    _521 = _506;
                                                }
                                                _524 = _521 & _498;
                                                break;
                                            }
                                            _524 = clamp(_506, 0, _498);
                                            break;
                                        } while(false);
                                        int _526 = _424 >> 7;
                                        int _544;
                                        do
                                        {
                                            if (_500 == 1u)
                                            {
                                                _544 = _526 & _495;
                                                break;
                                            }
                                            if (_500 == 2u)
                                            {
                                                int _541;
                                                if ((_526 & _496) != 0)
                                                {
                                                    _541 = ~_526;
                                                }
                                                else
                                                {
                                                    _541 = _526;
                                                }
                                                _544 = _541 & _495;
                                                break;
                                            }
                                            _544 = clamp(_526, 0, _495);
                                            break;
                                        } while(false);
                                        uint _549 = ((_480 + uint((_524 * _496) + _544)) * 4u) >> 2u;
                                        _714 = uint4(arena._m0[_549] & 255u, (arena._m0[_549] >> 8u) & 255u, (arena._m0[_549] >> 16u) & 255u, arena._m0[_549] >> 24u);
                                        break;
                                    }
                                    bool _564;
                                    int _559 = _424 - 64;
                                    int _560 = _432 - 64;
                                    int _561 = _559 >> 7;
                                    int _579;
                                    do
                                    {
                                        _564 = _500 == 1u;
                                        if (_564)
                                        {
                                            _579 = _561 & _495;
                                            break;
                                        }
                                        if (_500 == 2u)
                                        {
                                            int _576;
                                            if ((_561 & _496) != 0)
                                            {
                                                _576 = ~_561;
                                            }
                                            else
                                            {
                                                _576 = _561;
                                            }
                                            _579 = _576 & _495;
                                            break;
                                        }
                                        _579 = clamp(_561, 0, _495);
                                        break;
                                    } while(false);
                                    bool _583;
                                    int _580 = _560 >> 7;
                                    int _598;
                                    do
                                    {
                                        _583 = _502 == 1u;
                                        if (_583)
                                        {
                                            _598 = _580 & _498;
                                            break;
                                        }
                                        if (_502 == 2u)
                                        {
                                            int _595;
                                            if ((_580 & _499) != 0)
                                            {
                                                _595 = ~_580;
                                            }
                                            else
                                            {
                                                _595 = _580;
                                            }
                                            _598 = _595 & _498;
                                            break;
                                        }
                                        _598 = clamp(_580, 0, _498);
                                        break;
                                    } while(false);
                                    int _599 = _561 + 1;
                                    int _616;
                                    do
                                    {
                                        if (_564)
                                        {
                                            _616 = _599 & _495;
                                            break;
                                        }
                                        if (_500 == 2u)
                                        {
                                            int _613;
                                            if ((_599 & _496) != 0)
                                            {
                                                _613 = ~_599;
                                            }
                                            else
                                            {
                                                _613 = _599;
                                            }
                                            _616 = _613 & _495;
                                            break;
                                        }
                                        _616 = clamp(_599, 0, _495);
                                        break;
                                    } while(false);
                                    int _617 = _580 + 1;
                                    int _634;
                                    do
                                    {
                                        if (_583)
                                        {
                                            _634 = _617 & _498;
                                            break;
                                        }
                                        if (_502 == 2u)
                                        {
                                            int _631;
                                            if ((_617 & _499) != 0)
                                            {
                                                _631 = ~_617;
                                            }
                                            else
                                            {
                                                _631 = _617;
                                            }
                                            _634 = _631 & _498;
                                            break;
                                        }
                                        _634 = clamp(_617, 0, _498);
                                        break;
                                    } while(false);
                                    uint _636 = uint(_559) & 127u;
                                    uint _638 = uint(_560) & 127u;
                                    int _639 = _598 * _496;
                                    uint _644 = ((_480 + uint(_639 + _579)) * 4u) >> 2u;
                                    uint _658 = ((_480 + uint(_639 + _616)) * 4u) >> 2u;
                                    int _668 = _634 * _496;
                                    uint _673 = ((_480 + uint(_668 + _579)) * 4u) >> 2u;
                                    uint _687 = ((_480 + uint(_668 + _616)) * 4u) >> 2u;
                                    uint4 _698 = uint4(128u - _636);
                                    uint4 _702 = uint4(_636);
                                    uint4 _707 = uint4(_638);
                                    _714 = (((uint4(128u - _638) * ((uint4(arena._m0[_644] & 255u, (arena._m0[_644] >> 8u) & 255u, (arena._m0[_644] >> 16u) & 255u, arena._m0[_644] >> 24u) * _698) + (uint4(arena._m0[_658] & 255u, (arena._m0[_658] >> 8u) & 255u, (arena._m0[_658] >> 16u) & 255u, arena._m0[_658] >> 24u) * _702))) + ((uint4(arena._m0[_673] & 255u, (arena._m0[_673] >> 8u) & 255u, (arena._m0[_673] >> 16u) & 255u, arena._m0[_673] >> 24u) * _698) * _707)) + ((uint4(arena._m0[_687] & 255u, (arena._m0[_687] >> 8u) & 255u, (arena._m0[_687] >> 16u) & 255u, arena._m0[_687] >> 24u) * _702) * _707)) >> uint4(14u);
                                    break;
                                } while(false);
                                _1217 = _714;
                                break;
                            }
                            uint _719;
                            uint _720;
                            uint _722;
                            uint4 _963;
                            do
                            {
                                _719 = DrawState.d_tex_size[_342][_343] & 65535u;
                                _720 = DrawState.d_tex_size[_342][_343] >> 16u;
                                _722 = DrawState.d_tex_levels[_342][_343];
                                if (_461 >= _722)
                                {
                                    _963 = uint4(0u);
                                    break;
                                }
                                uint _729;
                                _729 = DrawState.d_tex_base[_342][_343];
                                for (uint _732 = 0u; _732 < _461; )
                                {
                                    uint _736 = _732 & 31u;
                                    _729 += (((_719 >> _736) + 1u) * ((_720 >> _736) + 1u));
                                    _732++;
                                    continue;
                                }
                                uint _742 = _461 & 31u;
                                int _744 = int(_719 >> _742);
                                int _745 = _744 + 1;
                                int _747 = int(_720 >> _742);
                                int _748 = _747 + 1;
                                uint _749 = DrawState.d_tex_mode0[_342][_343] & 3u;
                                uint _751 = (DrawState.d_tex_mode0[_342][_343] >> 2u) & 3u;
                                if (!_404)
                                {
                                    int _755 = _432 >> 7;
                                    int _773;
                                    do
                                    {
                                        if (_751 == 1u)
                                        {
                                            _773 = _755 & _747;
                                            break;
                                        }
                                        if (_751 == 2u)
                                        {
                                            int _770;
                                            if ((_755 & _748) != 0)
                                            {
                                                _770 = ~_755;
                                            }
                                            else
                                            {
                                                _770 = _755;
                                            }
                                            _773 = _770 & _747;
                                            break;
                                        }
                                        _773 = clamp(_755, 0, _747);
                                        break;
                                    } while(false);
                                    int _775 = _424 >> 7;
                                    int _793;
                                    do
                                    {
                                        if (_749 == 1u)
                                        {
                                            _793 = _775 & _744;
                                            break;
                                        }
                                        if (_749 == 2u)
                                        {
                                            int _790;
                                            if ((_775 & _745) != 0)
                                            {
                                                _790 = ~_775;
                                            }
                                            else
                                            {
                                                _790 = _775;
                                            }
                                            _793 = _790 & _744;
                                            break;
                                        }
                                        _793 = clamp(_775, 0, _744);
                                        break;
                                    } while(false);
                                    uint _798 = ((_729 + uint((_773 * _745) + _793)) * 4u) >> 2u;
                                    _963 = uint4(arena._m0[_798] & 255u, (arena._m0[_798] >> 8u) & 255u, (arena._m0[_798] >> 16u) & 255u, arena._m0[_798] >> 24u);
                                    break;
                                }
                                bool _813;
                                int _808 = _424 - 64;
                                int _809 = _432 - 64;
                                int _810 = _808 >> 7;
                                int _828;
                                do
                                {
                                    _813 = _749 == 1u;
                                    if (_813)
                                    {
                                        _828 = _810 & _744;
                                        break;
                                    }
                                    if (_749 == 2u)
                                    {
                                        int _825;
                                        if ((_810 & _745) != 0)
                                        {
                                            _825 = ~_810;
                                        }
                                        else
                                        {
                                            _825 = _810;
                                        }
                                        _828 = _825 & _744;
                                        break;
                                    }
                                    _828 = clamp(_810, 0, _744);
                                    break;
                                } while(false);
                                bool _832;
                                int _829 = _809 >> 7;
                                int _847;
                                do
                                {
                                    _832 = _751 == 1u;
                                    if (_832)
                                    {
                                        _847 = _829 & _747;
                                        break;
                                    }
                                    if (_751 == 2u)
                                    {
                                        int _844;
                                        if ((_829 & _748) != 0)
                                        {
                                            _844 = ~_829;
                                        }
                                        else
                                        {
                                            _844 = _829;
                                        }
                                        _847 = _844 & _747;
                                        break;
                                    }
                                    _847 = clamp(_829, 0, _747);
                                    break;
                                } while(false);
                                int _848 = _810 + 1;
                                int _865;
                                do
                                {
                                    if (_813)
                                    {
                                        _865 = _848 & _744;
                                        break;
                                    }
                                    if (_749 == 2u)
                                    {
                                        int _862;
                                        if ((_848 & _745) != 0)
                                        {
                                            _862 = ~_848;
                                        }
                                        else
                                        {
                                            _862 = _848;
                                        }
                                        _865 = _862 & _744;
                                        break;
                                    }
                                    _865 = clamp(_848, 0, _744);
                                    break;
                                } while(false);
                                int _866 = _829 + 1;
                                int _883;
                                do
                                {
                                    if (_832)
                                    {
                                        _883 = _866 & _747;
                                        break;
                                    }
                                    if (_751 == 2u)
                                    {
                                        int _880;
                                        if ((_866 & _748) != 0)
                                        {
                                            _880 = ~_866;
                                        }
                                        else
                                        {
                                            _880 = _866;
                                        }
                                        _883 = _880 & _747;
                                        break;
                                    }
                                    _883 = clamp(_866, 0, _747);
                                    break;
                                } while(false);
                                uint _885 = uint(_808) & 127u;
                                uint _887 = uint(_809) & 127u;
                                int _888 = _847 * _745;
                                uint _893 = ((_729 + uint(_888 + _828)) * 4u) >> 2u;
                                uint _907 = ((_729 + uint(_888 + _865)) * 4u) >> 2u;
                                int _917 = _883 * _745;
                                uint _922 = ((_729 + uint(_917 + _828)) * 4u) >> 2u;
                                uint _936 = ((_729 + uint(_917 + _865)) * 4u) >> 2u;
                                uint4 _947 = uint4(128u - _885);
                                uint4 _951 = uint4(_885);
                                uint4 _956 = uint4(_887);
                                _963 = (((uint4(128u - _887) * ((uint4(arena._m0[_893] & 255u, (arena._m0[_893] >> 8u) & 255u, (arena._m0[_893] >> 16u) & 255u, arena._m0[_893] >> 24u) * _947) + (uint4(arena._m0[_907] & 255u, (arena._m0[_907] >> 8u) & 255u, (arena._m0[_907] >> 16u) & 255u, arena._m0[_907] >> 24u) * _951))) + ((uint4(arena._m0[_922] & 255u, (arena._m0[_922] >> 8u) & 255u, (arena._m0[_922] >> 16u) & 255u, arena._m0[_922] >> 24u) * _947) * _956)) + ((uint4(arena._m0[_936] & 255u, (arena._m0[_936] >> 8u) & 255u, (arena._m0[_936] >> 16u) & 255u, arena._m0[_936] >> 24u) * _951) * _956)) >> uint4(14u);
                                break;
                            } while(false);
                            uint _964 = _461 + 1u;
                            uint4 _1207;
                            do
                            {
                                if (_964 >= _722)
                                {
                                    _1207 = uint4(0u);
                                    break;
                                }
                                uint _973;
                                _973 = DrawState.d_tex_base[_342][_343];
                                for (uint _976 = 0u; _976 < _964; )
                                {
                                    uint _980 = _976 & 31u;
                                    _973 += (((_719 >> _980) + 1u) * ((_720 >> _980) + 1u));
                                    _976++;
                                    continue;
                                }
                                uint _986 = _964 & 31u;
                                int _988 = int(_719 >> _986);
                                int _989 = _988 + 1;
                                int _991 = int(_720 >> _986);
                                int _992 = _991 + 1;
                                uint _993 = DrawState.d_tex_mode0[_342][_343] & 3u;
                                uint _995 = (DrawState.d_tex_mode0[_342][_343] >> 2u) & 3u;
                                if (!_404)
                                {
                                    int _999 = _432 >> 7;
                                    int _1017;
                                    do
                                    {
                                        if (_995 == 1u)
                                        {
                                            _1017 = _999 & _991;
                                            break;
                                        }
                                        if (_995 == 2u)
                                        {
                                            int _1014;
                                            if ((_999 & _992) != 0)
                                            {
                                                _1014 = ~_999;
                                            }
                                            else
                                            {
                                                _1014 = _999;
                                            }
                                            _1017 = _1014 & _991;
                                            break;
                                        }
                                        _1017 = clamp(_999, 0, _991);
                                        break;
                                    } while(false);
                                    int _1019 = _424 >> 7;
                                    int _1037;
                                    do
                                    {
                                        if (_993 == 1u)
                                        {
                                            _1037 = _1019 & _988;
                                            break;
                                        }
                                        if (_993 == 2u)
                                        {
                                            int _1034;
                                            if ((_1019 & _989) != 0)
                                            {
                                                _1034 = ~_1019;
                                            }
                                            else
                                            {
                                                _1034 = _1019;
                                            }
                                            _1037 = _1034 & _988;
                                            break;
                                        }
                                        _1037 = clamp(_1019, 0, _988);
                                        break;
                                    } while(false);
                                    uint _1042 = ((_973 + uint((_1017 * _989) + _1037)) * 4u) >> 2u;
                                    _1207 = uint4(arena._m0[_1042] & 255u, (arena._m0[_1042] >> 8u) & 255u, (arena._m0[_1042] >> 16u) & 255u, arena._m0[_1042] >> 24u);
                                    break;
                                }
                                bool _1057;
                                int _1052 = _424 - 64;
                                int _1053 = _432 - 64;
                                int _1054 = _1052 >> 7;
                                int _1072;
                                do
                                {
                                    _1057 = _993 == 1u;
                                    if (_1057)
                                    {
                                        _1072 = _1054 & _988;
                                        break;
                                    }
                                    if (_993 == 2u)
                                    {
                                        int _1069;
                                        if ((_1054 & _989) != 0)
                                        {
                                            _1069 = ~_1054;
                                        }
                                        else
                                        {
                                            _1069 = _1054;
                                        }
                                        _1072 = _1069 & _988;
                                        break;
                                    }
                                    _1072 = clamp(_1054, 0, _988);
                                    break;
                                } while(false);
                                bool _1076;
                                int _1073 = _1053 >> 7;
                                int _1091;
                                do
                                {
                                    _1076 = _995 == 1u;
                                    if (_1076)
                                    {
                                        _1091 = _1073 & _991;
                                        break;
                                    }
                                    if (_995 == 2u)
                                    {
                                        int _1088;
                                        if ((_1073 & _992) != 0)
                                        {
                                            _1088 = ~_1073;
                                        }
                                        else
                                        {
                                            _1088 = _1073;
                                        }
                                        _1091 = _1088 & _991;
                                        break;
                                    }
                                    _1091 = clamp(_1073, 0, _991);
                                    break;
                                } while(false);
                                int _1092 = _1054 + 1;
                                int _1109;
                                do
                                {
                                    if (_1057)
                                    {
                                        _1109 = _1092 & _988;
                                        break;
                                    }
                                    if (_993 == 2u)
                                    {
                                        int _1106;
                                        if ((_1092 & _989) != 0)
                                        {
                                            _1106 = ~_1092;
                                        }
                                        else
                                        {
                                            _1106 = _1092;
                                        }
                                        _1109 = _1106 & _988;
                                        break;
                                    }
                                    _1109 = clamp(_1092, 0, _988);
                                    break;
                                } while(false);
                                int _1110 = _1073 + 1;
                                int _1127;
                                do
                                {
                                    if (_1076)
                                    {
                                        _1127 = _1110 & _991;
                                        break;
                                    }
                                    if (_995 == 2u)
                                    {
                                        int _1124;
                                        if ((_1110 & _992) != 0)
                                        {
                                            _1124 = ~_1110;
                                        }
                                        else
                                        {
                                            _1124 = _1110;
                                        }
                                        _1127 = _1124 & _991;
                                        break;
                                    }
                                    _1127 = clamp(_1110, 0, _991);
                                    break;
                                } while(false);
                                uint _1129 = uint(_1052) & 127u;
                                uint _1131 = uint(_1053) & 127u;
                                int _1132 = _1091 * _989;
                                uint _1137 = ((_973 + uint(_1132 + _1072)) * 4u) >> 2u;
                                uint _1151 = ((_973 + uint(_1132 + _1109)) * 4u) >> 2u;
                                int _1161 = _1127 * _989;
                                uint _1166 = ((_973 + uint(_1161 + _1072)) * 4u) >> 2u;
                                uint _1180 = ((_973 + uint(_1161 + _1109)) * 4u) >> 2u;
                                uint4 _1191 = uint4(128u - _1129);
                                uint4 _1195 = uint4(_1129);
                                uint4 _1200 = uint4(_1131);
                                _1207 = (((uint4(128u - _1131) * ((uint4(arena._m0[_1137] & 255u, (arena._m0[_1137] >> 8u) & 255u, (arena._m0[_1137] >> 16u) & 255u, arena._m0[_1137] >> 24u) * _1191) + (uint4(arena._m0[_1151] & 255u, (arena._m0[_1151] >> 8u) & 255u, (arena._m0[_1151] >> 16u) & 255u, arena._m0[_1151] >> 24u) * _1195))) + ((uint4(arena._m0[_1166] & 255u, (arena._m0[_1166] >> 8u) & 255u, (arena._m0[_1166] >> 16u) & 255u, arena._m0[_1166] >> 24u) * _1191) * _1200)) + ((uint4(arena._m0[_1180] & 255u, (arena._m0[_1180] >> 8u) & 255u, (arena._m0[_1180] >> 16u) & 255u, arena._m0[_1180] >> 24u) * _1195) * _1200)) >> uint4(14u);
                                break;
                            } while(false);
                            _1217 = ((_963 * uint4(uint(16 - _437))) + (_1207 * uint4(uint(_437)))) >> uint4(4u);
                            break;
                        } while(false);
                        _1218 = _1217;
                    }
                    else
                    {
                        _1218 = uint4(0u);
                    }
                    int4 _1219 = int4(_1218);
                    int4 _202 = _1219;
                    uint _1222 = ((DrawState.d_ac[_319][_320] >> 2u) & 3u) * 2u;
                    uint _1223 = _1222 >> 2u;
                    uint _1224 = _1222 & 3u;
                    uint _1234 = _1222 + 1u;
                    uint _1235 = _1234 >> 2u;
                    uint _1236 = _1234 & 3u;
                    _201[4] = int4(_202[DrawState.d_ksel[_1223][_1224] & 3u], _202[(DrawState.d_ksel[_1223][_1224] >> 2u) & 3u], _202[DrawState.d_ksel[_1235][_1236] & 3u], _202[(DrawState.d_ksel[_1235][_1236] >> 2u) & 3u]);
                    _303 = _1219;
                }
                else
                {
                    _303 = _302;
                }
                uint _1247 = _305 >> 1u;
                uint _1248 = _1247 >> 2u;
                uint _1249 = _1247 & 3u;
                bool _1253 = (_305 & 1u) != 0u;
                uint _1261;
                if (_1253)
                {
                    _1261 = (DrawState.d_ksel[_1248][_1249] >> 14u) & 31u;
                }
                else
                {
                    _1261 = (DrawState.d_ksel[_1248][_1249] >> 4u) & 31u;
                }
                uint _1269;
                if (_1253)
                {
                    _1269 = (DrawState.d_ksel[_1248][_1249] >> 19u) & 31u;
                }
                else
                {
                    _1269 = (DrawState.d_ksel[_1248][_1249] >> 9u) & 31u;
                }
                int4 _1308;
                if (_1261 < 8u)
                {
                    int3 _1306 = int3(_181[_1261], _181[_1261], _181[_1261]);
                    _1308 = int4(_1306.x, _1306.y, _1306.z, int4(0).w);
                }
                else
                {
                    bool _1278;
                    if (_1261 >= 12u)
                    {
                        _1278 = _1261 < 16u;
                    }
                    else
                    {
                        _1278 = false;
                    }
                    int4 _1301;
                    if (_1278)
                    {
                        uint _1297 = _1261 - 12u;
                        _1301 = int4(DrawState.d_konst[_1297].x, DrawState.d_konst[_1297].y, DrawState.d_konst[_1297].z, int4(0).w);
                    }
                    else
                    {
                        int4 _1296;
                        if (_1261 >= 16u)
                        {
                            uint _1289 = ((_1261 & 3u) * 4u) + ((_1261 - 16u) >> 2u);
                            int3 _1294 = int3(DrawState.d_konst[_1289 >> 2u][_1289 & 3u]);
                            _1296 = int4(_1294.x, _1294.y, _1294.z, int4(0).w);
                        }
                        else
                        {
                            _1296 = int4(0);
                        }
                        _1301 = _1296;
                    }
                    _1308 = _1301;
                }
                int4 _1330;
                if (_1269 < 8u)
                {
                    int4 _1329 = _1308;
                    _1329.w = _181[_1269];
                    _1330 = _1329;
                }
                else
                {
                    int4 _1326;
                    if (_1269 >= 16u)
                    {
                        uint _1320 = ((_1269 & 3u) * 4u) + ((_1269 - 16u) >> 2u);
                        int4 _1325 = _1308;
                        _1325.w = DrawState.d_konst[_1320 >> 2u][_1320 & 3u];
                        _1326 = _1325;
                    }
                    else
                    {
                        _1326 = _1308;
                    }
                    _1330 = _1326;
                }
                _201[6] = _1330;
                uint _1332 = (DrawState.d_order[_319][_320] >> 7u) & 7u;
                if (_1332 <= 1u)
                {
                    int4 _203 = _200[_1332];
                    uint _1340 = (DrawState.d_ac[_319][_320] & 3u) * 2u;
                    uint _1341 = _1340 >> 2u;
                    uint _1342 = _1340 & 3u;
                    uint _1352 = _1340 + 1u;
                    uint _1353 = _1352 >> 2u;
                    uint _1354 = _1352 & 3u;
                    _201[5] = int4(_203[DrawState.d_ksel[_1341][_1342] & 3u], _203[(DrawState.d_ksel[_1341][_1342] >> 2u) & 3u], _203[DrawState.d_ksel[_1353][_1354] & 3u], _203[(DrawState.d_ksel[_1353][_1354] >> 2u) & 3u]);
                }
                else
                {
                    _201[5] = int4(0);
                }
                _308 = _307;
                bool _1366;
                for (uint _1368 = 0u; _1368 < 4u; _308 = _1366, _1368++)
                {
                    uint _1376 = DrawState.d_cc[_319][_320] >> ((12u - (4u * _1368)) & 31u);
                    uint _1377 = _1376 & 15u;
                    uint _1378 = _1377 >> 1u;
                    int3 _1403;
                    if (_1378 < 6u)
                    {
                        int3 _1402;
                        if ((_1376 & 1u) != 0u)
                        {
                            _1402 = int3(_201[_1378].w);
                        }
                        else
                        {
                            _1402 = _201[_1378].xyz;
                        }
                        _1366 = _308;
                        _1403 = _1402;
                    }
                    else
                    {
                        if (!_308)
                        {
                            CS = _191;
                        }
                        _1366 = _308 ? _308 : true;
                        _1403 = _201[CS[_1377 - 12u]].xyz;
                    }
                    uint _1408 = (DrawState.d_ac[_319][_320] >> ((13u - (3u * _1368)) & 31u)) & 7u;
                    _204[_1368] = int4(_1403, _201[(_1408 == 7u) ? 9u : _1408].w);
                }
                int4 _205 = int4(0);
                uint _1419 = (DrawState.d_cc[_319][_320] >> 16u) & 3u;
                bool _1420 = _1419 == 3u;
                for (uint _1422 = 0u; _1422 < 3u; _1422++)
                {
                    int _1599;
                    if (_1420)
                    {
                        int4 _206 = _204[0];
                        int4 _207 = _204[1];
                        uint _1495 = (DrawState.d_cc[_319][_320] >> 20u) & 3u;
                        uint _1580;
                        uint _1581;
                        if (_1495 == 0u)
                        {
                            _1580 = uint(_207.x) & 255u;
                            _1581 = uint(_206.x) & 255u;
                        }
                        else
                        {
                            uint _1570;
                            uint _1571;
                            if (_1495 == 1u)
                            {
                                _1570 = ((uint(_207.y) & 255u) << 8u) | (uint(_207.x) & 255u);
                                _1571 = ((uint(_206.y) & 255u) << 8u) | (uint(_206.x) & 255u);
                            }
                            else
                            {
                                uint _1548;
                                uint _1549;
                                if (_1495 == 2u)
                                {
                                    _1548 = (((uint(_207.z) & 255u) << 16u) | ((uint(_207.y) & 255u) << 8u)) | (uint(_207.x) & 255u);
                                    _1549 = (((uint(_206.z) & 255u) << 16u) | ((uint(_206.y) & 255u) << 8u)) | (uint(_206.x) & 255u);
                                }
                                else
                                {
                                    _1548 = uint(_207[_1422]) & 255u;
                                    _1549 = uint(_206[_1422]) & 255u;
                                }
                                _1570 = _1548;
                                _1571 = _1549;
                            }
                            _1580 = _1570;
                            _1581 = _1571;
                        }
                        bool _1590;
                        if (((DrawState.d_cc[_319][_320] >> 18u) & 1u) != 0u)
                        {
                            _1590 = _1581 == _1580;
                        }
                        else
                        {
                            _1590 = _1581 > _1580;
                        }
                        int _1597;
                        if (_1590)
                        {
                            _1597 = _204[2][_1422] & 255;
                        }
                        else
                        {
                            _1597 = 0;
                        }
                        _1599 = (int(uint(_204[3][_1422]) << 21u) >> 21) + _1597;
                    }
                    else
                    {
                        bool _1441 = ((DrawState.d_cc[_319][_320] >> 18u) & 1u) != 0u;
                        uint _1443 = (DrawState.d_cc[_319][_320] >> 20u) & 3u;
                        int _1446 = _204[2][_1422] & 255;
                        int _1452 = _1446 + (_1446 >> 7);
                        int _1460 = (((_204[0][_1422] & 255) * (256 - _1452)) + ((_204[1][_1422] & 255) * _1452)) << (_183[_1443] & 31);
                        int _1466;
                        if (_1443 != 3u)
                        {
                            _1466 = _1460 + (_1441 ? 127 : 128);
                        }
                        else
                        {
                            _1466 = _1460;
                        }
                        int _1467 = _1466 >> 8;
                        int _1472;
                        if (_1441)
                        {
                            _1472 = -_1467;
                        }
                        else
                        {
                            _1472 = _1467;
                        }
                        _1599 = ((((int(uint(_204[3][_1422]) << 21u) >> 21) + _182[_1419]) << (_183[_1443] & 31)) + _1472) >> (_184[_1443] & 31);
                    }
                    _205[_1422] = _1599;
                }
                uint _1602 = (DrawState.d_ac[_319][_320] >> 16u) & 3u;
                int _1762;
                if (_1602 == 3u)
                {
                    uint _1672 = (DrawState.d_ac[_319][_320] >> 20u) & 3u;
                    uint _1743;
                    uint _1744;
                    if (_1672 == 0u)
                    {
                        _1743 = uint(_204[1].x) & 255u;
                        _1744 = uint(_204[0].x) & 255u;
                    }
                    else
                    {
                        uint _1735;
                        uint _1736;
                        if (_1672 == 1u)
                        {
                            _1735 = ((uint(_204[1].y) & 255u) << 8u) | (uint(_204[1].x) & 255u);
                            _1736 = ((uint(_204[0].y) & 255u) << 8u) | (uint(_204[0].x) & 255u);
                        }
                        else
                        {
                            uint _1717;
                            uint _1718;
                            if (_1672 == 2u)
                            {
                                _1717 = (((uint(_204[1].z) & 255u) << 16u) | ((uint(_204[1].y) & 255u) << 8u)) | (uint(_204[1].x) & 255u);
                                _1718 = (((uint(_204[0].z) & 255u) << 16u) | ((uint(_204[0].y) & 255u) << 8u)) | (uint(_204[0].x) & 255u);
                            }
                            else
                            {
                                _1717 = uint(_204[1].w) & 255u;
                                _1718 = uint(_204[0].w) & 255u;
                            }
                            _1735 = _1717;
                            _1736 = _1718;
                        }
                        _1743 = _1735;
                        _1744 = _1736;
                    }
                    bool _1753;
                    if (((DrawState.d_ac[_319][_320] >> 18u) & 1u) != 0u)
                    {
                        _1753 = _1744 == _1743;
                    }
                    else
                    {
                        _1753 = _1744 > _1743;
                    }
                    int _1760;
                    if (_1753)
                    {
                        _1760 = _204[2].w & 255;
                    }
                    else
                    {
                        _1760 = 0;
                    }
                    _1762 = (int(uint(_204[3].w) << 21u) >> 21) + _1760;
                }
                else
                {
                    bool _1617 = ((DrawState.d_ac[_319][_320] >> 18u) & 1u) != 0u;
                    uint _1619 = (DrawState.d_ac[_319][_320] >> 20u) & 3u;
                    int _1622 = _204[2].w & 255;
                    int _1628 = _1622 + (_1622 >> 7);
                    int _1636 = (((_204[0].w & 255) * (256 - _1628)) + ((_204[1].w & 255) * _1628)) << (_183[_1619] & 31);
                    int _1642;
                    if (_1619 != 3u)
                    {
                        _1642 = _1636 + (_1617 ? 127 : 128);
                    }
                    else
                    {
                        _1642 = _1636;
                    }
                    int _1649;
                    if (_1617)
                    {
                        _1649 = (-_1642) >> 8;
                    }
                    else
                    {
                        _1649 = _1642 >> 8;
                    }
                    _1762 = ((((int(uint(_204[3].w) << 21u) >> 21) + _182[_1602]) << (_183[_1619] & 31)) + _1649) >> (_184[_1619] & 31);
                }
                _205.w = _1762;
                uint _1765 = (DrawState.d_cc[_319][_320] >> 22u) & 3u;
                bool _1770 = ((DrawState.d_cc[_319][_320] >> 19u) & 1u) != 0u;
                int _1781;
                if (_1770)
                {
                    _1781 = clamp(_205.x, 0, 255);
                }
                else
                {
                    _1781 = clamp(_205.x, -1024, 1023);
                }
                int _1789;
                if (_1770)
                {
                    _1789 = clamp(_205.y, 0, 255);
                }
                else
                {
                    _1789 = clamp(_205.y, -1024, 1023);
                }
                int _1797;
                if (_1770)
                {
                    _1797 = clamp(_205.z, 0, 255);
                }
                else
                {
                    _1797 = clamp(_205.z, -1024, 1023);
                }
                int3 _1798 = int3(_1781, _1789, _1797);
                _201[_1765] = int4(_1798.x, _1798.y, _1798.z, _201[_1765].w);
                int _1808;
                if (((DrawState.d_ac[_319][_320] >> 19u) & 1u) != 0u)
                {
                    _1808 = clamp(_205.w, 0, 255);
                }
                else
                {
                    _1808 = clamp(_205.w, -1024, 1023);
                }
                _201[(DrawState.d_ac[_319][_320] >> 22u) & 3u].w = _1808;
                _302 = _303;
                _305++;
                _307 = _308;
                continue;
            }
            else
            {
                break;
            }
        }
        uint _1811 = clamp(_312, 1u, 16u) - 1u;
        uint _1812 = _1811 >> 2u;
        uint _1813 = _1811 & 3u;
        uint _1817 = (DrawState.d_cc[_1812][_1813] >> 22u) & 3u;
        uint _1837 = uint(_201[(DrawState.d_ac[_1812][_1813] >> 22u) & 3u].w) & 255u;
        uint4 _1838 = uint4(uint(_201[_1817].x) & 255u, uint(_201[_1817].y) & 255u, uint(_201[_1817].z) & 255u, _1837);
        int _1841 = int(_1837);
        int _1843 = int(DrawState.d_alpha_test & 255u);
        bool _1861;
        switch ((DrawState.d_alpha_test >> 16u) & 7u)
        {
            case 0u:
            {
                _1861 = false;
                break;
            }
            case 1u:
            {
                _1861 = _1841 < _1843;
                break;
            }
            case 2u:
            {
                _1861 = _1841 == _1843;
                break;
            }
            case 3u:
            {
                _1861 = _1841 <= _1843;
                break;
            }
            case 4u:
            {
                _1861 = _1841 > _1843;
                break;
            }
            case 5u:
            {
                _1861 = _1841 != _1843;
                break;
            }
            case 6u:
            {
                _1861 = _1841 >= _1843;
                break;
            }
            default:
            {
                _1861 = true;
                break;
            }
        }
        int _1864 = int((DrawState.d_alpha_test >> 8u) & 255u);
        bool _1882;
        switch ((DrawState.d_alpha_test >> 19u) & 7u)
        {
            case 0u:
            {
                _1882 = false;
                break;
            }
            case 1u:
            {
                _1882 = _1841 < _1864;
                break;
            }
            case 2u:
            {
                _1882 = _1841 == _1864;
                break;
            }
            case 3u:
            {
                _1882 = _1841 <= _1864;
                break;
            }
            case 4u:
            {
                _1882 = _1841 > _1864;
                break;
            }
            case 5u:
            {
                _1882 = _1841 != _1864;
                break;
            }
            case 6u:
            {
                _1882 = _1841 >= _1864;
                break;
            }
            default:
            {
                _1882 = true;
                break;
            }
        }
        bool _1895;
        switch ((DrawState.d_alpha_test >> 22u) & 3u)
        {
            case 0u:
            {
                _1895 = _1861 ? _1882 : false;
                break;
            }
            case 1u:
            {
                _1895 = (!_1861) ? _1882 : true;
                break;
            }
            case 2u:
            {
                _1895 = _1861 != _1882;
                break;
            }
            default:
            {
                _1895 = _1861 == _1882;
                break;
            }
        }
        int _1947;
        do
        {
            uint _1901 = (DrawState.d_ztex2 >> 2u) & 3u;
            if (_1901 == 0u)
            {
                _1947 = _268;
                break;
            }
            uint _1907 = DrawState.d_ztex2 & 3u;
            uint _1938;
            if (_1907 == 0u)
            {
                _1938 = DrawState.d_ztex_bias + uint(_302.w);
            }
            else
            {
                uint _1934;
                if (_1907 == 1u)
                {
                    _1934 = DrawState.d_ztex_bias + ((uint(_302.w) << 8u) | uint(_302.x));
                }
                else
                {
                    _1934 = DrawState.d_ztex_bias + (((uint(_302.x) << 16u) | (uint(_302.y) << 8u)) | uint(_302.z));
                }
                _1938 = _1934;
            }
            uint _1944;
            if (_1901 == 1u)
            {
                _1944 = _1938 + uint(_268);
            }
            else
            {
                _1944 = _1938;
            }
            _1947 = int(_1944 & 16777215u);
            break;
        } while(false);
        uint _1951 = (DrawState.d_fog3 >> 21u) & 7u;
        uint4 _2111;
        if (_1951 != 0u)
        {
            float _1983;
            if (((DrawState.d_fog3 >> 20u) & 1u) == 0u)
            {
                _1983 = (DrawState.d_fog_a * 16777215.0) / float(int(DrawState.d_fog_bmag) - (_1947 >> (int(DrawState.d_fog_bshift) & 31)));
            }
            else
            {
                _1983 = DrawState.d_fog_a * (float(_1947) * 5.9604651880817982601001858711243e-08);
            }
            float _2055;
            if (((DrawState.d_fog_range[0u].x >> 10u) & 1u) != 0u)
            {
                float _1998 = (float(int(_228.x)) - (float(DrawState.d_fog_range[0u].x & 1023u) - 342.0)) / DrawState.d_vp_wd;
                float _2002 = fast::clamp(9.0 - (abs(_1998) * 9.0), 0.0, 9.0);
                int _2003 = int(_2002);
                int _2004 = _2003 + 1;
                uint _2007 = uint(1 + (_2003 / 2));
                uint _2008 = _2007 >> 2u;
                uint _2009 = _2007 & 3u;
                uint _2016 = uint(1 + (((_2004 > 9) ? 9 : _2004) / 2));
                uint _2017 = _2016 >> 2u;
                uint _2018 = _2016 & 3u;
                uint _2029;
                if ((_2003 - 2 * (_2003 / 2)) != 0)
                {
                    _2029 = DrawState.d_fog_range[_2008][_2009] & 4095u;
                }
                else
                {
                    _2029 = (DrawState.d_fog_range[_2008][_2009] >> 12u) & 4095u;
                }
                uint _2040;
                if ((_2004 - 2 * (_2004 / 2)) != 0)
                {
                    _2040 = DrawState.d_fog_range[_2017][_2018] & 4095u;
                }
                else
                {
                    _2040 = (DrawState.d_fog_range[_2017][_2018] >> 12u) & 4095u;
                }
                float _2044 = float(_2004) - _2002;
                float _2048 = ((float(_2029) * 0.015625) * _2044) + ((float(_2040) * 0.015625) * (1.0 - _2044));
                _2055 = _1983 * (sqrt((_1998 * _1998) + (_2048 * _2048)) / _2048);
            }
            else
            {
                _2055 = _1983;
            }
            float _2059 = fast::clamp(_2055 - DrawState.d_fog_c, 0.0, 1.0);
            float _2092;
            if (_1951 == 4u)
            {
                _2092 = 1.0 - exp2((-8.0) * _2059);
            }
            else
            {
                float _2088;
                if (_1951 == 5u)
                {
                    _2088 = 1.0 - exp2(((-8.0) * _2059) * _2059);
                }
                else
                {
                    float _2083;
                    if (_1951 == 6u)
                    {
                        _2083 = exp2((-8.0) * (1.0 - _2059));
                    }
                    else
                    {
                        float _2079;
                        if (_1951 == 7u)
                        {
                            float _2075 = 1.0 - _2059;
                            _2079 = exp2(((-8.0) * _2075) * _2075);
                        }
                        else
                        {
                            _2079 = _2059;
                        }
                        _2083 = _2079;
                    }
                    _2088 = _2083;
                }
                _2092 = _2088;
            }
            uint _2094 = uint(_2092 * 256.0);
            uint3 _2109 = ((_1838.xyz * uint3(256u - _2094)) + (uint3(_2094) * uint3((DrawState.d_fog_color >> 16u) & 255u, (DrawState.d_fog_color >> 8u) & 255u, DrawState.d_fog_color & 255u))) >> uint3(8u);
            _2111 = uint4(_2109.x, _2109.y, _2109.z, _1838.w);
        }
        else
        {
            _2111 = _1838;
        }
        _2114 = _2111;
        _2115 = uint(_268);
        _2116 = uint(_1947);
        _2117 = _1895;
    }
    else
    {
        _2114 = uint4(0u);
        _2115 = 0u;
        _2116 = 0u;
        _2117 = false;
    }
    spvImageFence(efb_color);
    uint4 _2119 = efb_color.read(uint2(_228));
    uint _2120 = _2119.x;
    spvImageFence(efb_depth);
    uint4 _2122 = efb_depth.read(uint2(_228));
    uint _2123 = _2122.x;
    uint _2565;
    uint _2566;
    if (_232)
    {
        bool _2499 = ((DrawState.d_blend >> 3u) & 1u) != 0u;
        bool _2502 = ((DrawState.d_blend >> 4u) & 1u) != 0u;
        uint _2553;
        if ((!_2499) ? _2502 : true)
        {
            uint4 _2513 = uint4(DrawState.d_regs[0]);
            uint _2551;
            if ((DrawState.d_pe_control & 7u) == 1u)
            {
                uint _2543;
                if (_2499)
                {
                    _2543 = (((_2120 & 63u) | ((_2513.x >> 2u) << 18u)) | ((_2513.y >> 2u) << 12u)) | ((_2513.z >> 2u) << 6u);
                }
                else
                {
                    _2543 = _2120;
                }
                uint _2550;
                if (_2502)
                {
                    _2550 = (_2543 & 4294967232u) | (_2513.w >> 2u);
                }
                else
                {
                    _2550 = _2543;
                }
                _2551 = _2550;
            }
            else
            {
                uint _2527;
                if (_2499)
                {
                    _2527 = ((_2513.x << 16u) | (_2513.y << 8u)) | _2513.z;
                }
                else
                {
                    _2527 = _2120;
                }
                _2551 = _2527;
            }
            _2553 = _2551 & 16777215u;
        }
        else
        {
            _2553 = _2120;
        }
        uint _2564;
        if (((DrawState.d_zmode >> 4u) & 1u) != 0u)
        {
            _2564 = DrawState.d_ztex_bias;
        }
        else
        {
            _2564 = _2123;
        }
        _2565 = _2564;
        _2566 = _2553;
    }
    else
    {
        uint _2493;
        uint _2494;
        do
        {
            uint _2132 = (DrawState.d_zmode >> 1u) & 7u;
            bool _2135 = ((DrawState.d_zmode >> 4u) & 1u) != 0u;
            bool _2137 = (DrawState.d_zmode & 1u) != 0u;
            bool _2145;
            if (_2137)
            {
                _2145 = ((DrawState.d_pe_control >> 6u) & 1u) != 0u;
            }
            else
            {
                _2145 = false;
            }
            uint _2171;
            if (_2145)
            {
                bool _2163;
                switch (_2132)
                {
                    case 0u:
                    {
                        _2163 = false;
                        break;
                    }
                    case 1u:
                    {
                        _2163 = _2115 < _2123;
                        break;
                    }
                    case 2u:
                    {
                        _2163 = _2115 == _2123;
                        break;
                    }
                    case 3u:
                    {
                        _2163 = _2115 <= _2123;
                        break;
                    }
                    case 4u:
                    {
                        _2163 = _2115 > _2123;
                        break;
                    }
                    case 5u:
                    {
                        _2163 = _2115 != _2123;
                        break;
                    }
                    case 6u:
                    {
                        _2163 = _2115 >= _2123;
                        break;
                    }
                    default:
                    {
                        _2163 = true;
                        break;
                    }
                }
                if (!_2163)
                {
                    _2493 = _2123;
                    _2494 = _2120;
                    break;
                }
                uint _2170;
                if (_2135)
                {
                    _2170 = _2115 & 16777215u;
                }
                else
                {
                    _2170 = _2123;
                }
                _2171 = _2170;
            }
            else
            {
                _2171 = _2123;
            }
            if (!_2117)
            {
                _2493 = _2171;
                _2494 = _2120;
                break;
            }
            bool _2178;
            if (_2137)
            {
                _2178 = !_2145;
            }
            else
            {
                _2178 = false;
            }
            uint _2204;
            if (_2178)
            {
                bool _2196;
                switch (_2132)
                {
                    case 0u:
                    {
                        _2196 = false;
                        break;
                    }
                    case 1u:
                    {
                        _2196 = _2116 < _2123;
                        break;
                    }
                    case 2u:
                    {
                        _2196 = _2116 == _2123;
                        break;
                    }
                    case 3u:
                    {
                        _2196 = _2116 <= _2123;
                        break;
                    }
                    case 4u:
                    {
                        _2196 = _2116 > _2123;
                        break;
                    }
                    case 5u:
                    {
                        _2196 = _2116 != _2123;
                        break;
                    }
                    case 6u:
                    {
                        _2196 = _2116 >= _2123;
                        break;
                    }
                    default:
                    {
                        _2196 = true;
                        break;
                    }
                }
                if (!_2196)
                {
                    _2493 = _2171;
                    _2494 = _2120;
                    break;
                }
                uint _2203;
                if (_2135)
                {
                    _2203 = _2116 & 16777215u;
                }
                else
                {
                    _2203 = _2171;
                }
                _2204 = _2203;
            }
            else
            {
                _2204 = _2171;
            }
            bool _2212;
            uint4 _199 = _2114;
            uint4 _2241;
            do
            {
                _2212 = (DrawState.d_pe_control & 7u) == 1u;
                if (_2212)
                {
                    uint _2216 = (_2120 >> 18u) & 63u;
                    uint _2221 = (_2120 >> 12u) & 63u;
                    uint _2226 = (_2120 >> 6u) & 63u;
                    uint _2230 = _2120 & 63u;
                    _2241 = uint4((_2216 << 2u) | (_2216 >> 4u), (_2221 << 2u) | (_2221 >> 4u), (_2226 << 2u) | (_2226 >> 4u), (_2230 << 2u) | (_2230 >> 4u));
                    break;
                }
                _2241 = uint4((_2120 >> 16u) & 255u, (_2120 >> 8u) & 255u, _2120 & 255u, 255u);
                break;
            } while(false);
            uint4 _195 = _2241;
            if ((DrawState.d_blend & 1u) != 0u)
            {
                if (((DrawState.d_blend >> 11u) & 1u) != 0u)
                {
                    for (uint _2385 = 0u; _2385 < 4u; _2385++)
                    {
                        uint _2402;
                        if (_195[_2385] > _199[_2385])
                        {
                            _2402 = _195[_2385] - _199[_2385];
                        }
                        else
                        {
                            _2402 = 0u;
                        }
                        _195[_2385] = _2402;
                    }
                }
                else
                {
                    uint _2311 = (DrawState.d_blend >> 5u) & 7u;
                    uint _2313 = (DrawState.d_blend >> 8u) & 7u;
                    uint4 _196 = uint4(0u);
                    for (uint _2315 = 0u; _2315 < 4u; _2315++)
                    {
                        uint4 _197 = _195;
                        uint _2344;
                        switch (_2313)
                        {
                            case 0u:
                            {
                                _2344 = 0u;
                                break;
                            }
                            case 1u:
                            {
                                _2344 = 255u;
                                break;
                            }
                            case 2u:
                            {
                                _2344 = _197[_2315];
                                break;
                            }
                            case 3u:
                            {
                                _2344 = 255u - _197[_2315];
                                break;
                            }
                            case 4u:
                            {
                                _2344 = _2114.w;
                                break;
                            }
                            case 5u:
                            {
                                _2344 = 255u - _2114.w;
                                break;
                            }
                            case 6u:
                            {
                                _2344 = _197.w;
                                break;
                            }
                            default:
                            {
                                _2344 = 255u - _197.w;
                                break;
                            }
                        }
                        uint4 _198 = _2114;
                        uint _2368;
                        switch (_2311)
                        {
                            case 0u:
                            {
                                _2368 = 0u;
                                break;
                            }
                            case 1u:
                            {
                                _2368 = 255u;
                                break;
                            }
                            case 2u:
                            {
                                _2368 = _198[_2315];
                                break;
                            }
                            case 3u:
                            {
                                _2368 = 255u - _198[_2315];
                                break;
                            }
                            case 4u:
                            {
                                _2368 = _198.w;
                                break;
                            }
                            case 5u:
                            {
                                _2368 = 255u - _198.w;
                                break;
                            }
                            case 6u:
                            {
                                _2368 = _195.w;
                                break;
                            }
                            default:
                            {
                                _2368 = 255u - _195.w;
                                break;
                            }
                        }
                        _196[_2315] = min((((_199[_2315] * (_2344 + (_2344 >> 7u))) + (_195[_2315] * (_2368 + (_2368 >> 7u)))) >> 8u), 255u);
                    }
                    _195 = _196;
                }
            }
            else
            {
                if (((DrawState.d_blend >> 1u) & 1u) != 0u)
                {
                    for (uint _2254 = 0u; _2254 < 4u; _2254++)
                    {
                        uint _2302;
                        switch ((DrawState.d_blend >> 12u) & 15u)
                        {
                            case 0u:
                            {
                                _2302 = 0u;
                                break;
                            }
                            case 1u:
                            {
                                _2302 = _199[_2254] & _195[_2254];
                                break;
                            }
                            case 2u:
                            {
                                _2302 = _199[_2254] & (~_195[_2254]);
                                break;
                            }
                            case 3u:
                            {
                                _2302 = _199[_2254];
                                break;
                            }
                            case 4u:
                            {
                                _2302 = (~_199[_2254]) & _195[_2254];
                                break;
                            }
                            case 5u:
                            {
                                _2302 = _195[_2254];
                                break;
                            }
                            case 6u:
                            {
                                _2302 = _199[_2254] ^ _195[_2254];
                                break;
                            }
                            case 7u:
                            {
                                _2302 = _199[_2254] | _195[_2254];
                                break;
                            }
                            case 8u:
                            {
                                _2302 = ~(_199[_2254] | _195[_2254]);
                                break;
                            }
                            case 9u:
                            {
                                _2302 = ~(_199[_2254] ^ _195[_2254]);
                                break;
                            }
                            case 10u:
                            {
                                _2302 = ~_195[_2254];
                                break;
                            }
                            case 11u:
                            {
                                _2302 = _199[_2254] | (~_195[_2254]);
                                break;
                            }
                            case 12u:
                            {
                                _2302 = ~_199[_2254];
                                break;
                            }
                            case 13u:
                            {
                                _2302 = (~_199[_2254]) | _195[_2254];
                                break;
                            }
                            case 14u:
                            {
                                _2302 = ~(_199[_2254] & _195[_2254]);
                                break;
                            }
                            default:
                            {
                                _2302 = 4294967295u;
                                break;
                            }
                        }
                        _195[_2254] = _2302 & 255u;
                    }
                }
                else
                {
                    _195 = _2114;
                }
            }
            if (((DrawState.d_dst_alpha >> 8u) & 1u) != 0u)
            {
                _195.w = DrawState.d_dst_alpha & 255u;
            }
            bool _2414 = ((DrawState.d_blend >> 3u) & 1u) != 0u;
            bool _2417 = ((DrawState.d_blend >> 4u) & 1u) != 0u;
            bool _2423;
            if (_2414)
            {
                _2423 = ((DrawState.d_blend >> 2u) & 1u) != 0u;
            }
            else
            {
                _2423 = false;
            }
            if (_2423 ? _2212 : false)
            {
                uint _2432 = ((_228.y & 1u) << 1u) | (_228.x & 1u);
                for (uint _2436 = 0u; _2436 < 3u; )
                {
                    _195[_2436] = ((_195[_2436] - (_195[_2436] >> 6u)) + _185[_2432]) & 252u;
                    _2436++;
                    continue;
                }
            }
            uint _2492;
            if ((!_2414) ? _2417 : true)
            {
                uint _2490;
                if (_2212)
                {
                    uint _2482;
                    if (_2414)
                    {
                        _2482 = (((_2120 & 63u) | ((_195.x >> 2u) << 18u)) | ((_195.y >> 2u) << 12u)) | ((_195.z >> 2u) << 6u);
                    }
                    else
                    {
                        _2482 = _2120;
                    }
                    uint _2489;
                    if (_2417)
                    {
                        _2489 = (_2482 & 4294967232u) | (_195.w >> 2u);
                    }
                    else
                    {
                        _2489 = _2482;
                    }
                    _2490 = _2489;
                }
                else
                {
                    uint _2466;
                    if (_2414)
                    {
                        _2466 = ((_195.x << 16u) | (_195.y << 8u)) | _195.z;
                    }
                    else
                    {
                        _2466 = _2120;
                    }
                    _2490 = _2466;
                }
                _2492 = _2490 & 16777215u;
            }
            else
            {
                _2492 = _2120;
            }
            _2493 = _2204;
            _2494 = _2492;
            break;
        } while(false);
        _2565 = _2493;
        _2566 = _2494;
    }
    efb_color.write(uint4(_2566), uint2(_228));
    efb_depth.write(uint4(_2565), uint2(_228));
}

