#!/bin/zsh
# Verifica o shadercross: compila tests/shaders/rov_min.hlsl para DXIL, SPIR-V e MSL 2.0+ e confere o resultado.
# Sai com 0 se tudo ok. Saídas em /Volumes/AMRecomp/ext/shadercross-check/.
set -euo pipefail
ROOT=${0:A:h:h}
SC=${SHADERCROSS:-/Volumes/AMRecomp/ext/shadercross/bin/shadercross}
MSLVER=${MSLVER:-2.3.0}
SRC=$ROOT/tests/shaders/rov_min.hlsl
OUT=/Volumes/AMRecomp/ext/shadercross-check
mkdir -p $OUT; rm -rf $OUT; mkdir -p $OUT
[[ -x $SC ]] || { echo "FALHA: $SC não existe (rode scripts/build_shadercross.sh)" >&2; exit 1; }
export DYLD_LIBRARY_PATH=${SC:h:h}/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}
ok() { echo "ok:    $1"; }
fail() { echo "FALHA: $1" >&2; exit 1; }

for stage_entry in vertex:vs_main fragment:ps_main; do
  st=${stage_entry%%:*}; en=${stage_entry##*:}
  $SC $SRC -s HLSL -d DXIL  -t $st -e $en -o $OUT/$st.dxil
  $SC $SRC -s HLSL -d SPIRV -t $st -e $en -o $OUT/$st.spv
  $SC $SRC -s HLSL -d MSL   -t $st -e $en --msl-version $MSLVER -o $OUT/$st.msl
done
[[ $(head -c 4 $OUT/fragment.dxil) == DXBC ]] || fail "DXIL do fragment sem contêiner DXBC"
ok "DXIL gerado ($(wc -c < $OUT/fragment.dxil | tr -d ' ') bytes no fragment)"

spirv-dis $OUT/fragment.spv -o $OUT/fragment.spvasm
grep -q 'OpCapability FragmentShaderPixelInterlockEXT' $OUT/fragment.spvasm || fail "SPIR-V sem FragmentShaderPixelInterlockEXT"
grep -q 'PixelInterlockOrderedEXT' $OUT/fragment.spvasm || fail "SPIR-V sem PixelInterlockOrderedEXT"
grep -q 'OpBeginInvocationInterlockEXT' $OUT/fragment.spvasm || fail "SPIR-V sem OpBeginInvocationInterlockEXT"
ok "SPIR-V com FragmentShaderPixelInterlockEXT, PixelInterlockOrderedEXT e OpBegin/EndInvocationInterlockEXT"

grep -q 'raster_order_group' $OUT/fragment.msl || fail "MSL sem raster_order_group"
ok "MSL $MSLVER com raster_order_group"
for st in vertex fragment; do
  cp $OUT/$st.msl $OUT/$st.metal
  xcrun -sdk macosx metal -std=macos-metal2.3 -c $OUT/$st.metal -o $OUT/$st.air || fail "metal não compilou o MSL de $st"
done
ok "xcrun metal compilou vertex e fragment"
echo "shadercross OK"
