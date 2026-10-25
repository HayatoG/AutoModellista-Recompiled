#!/bin/zsh
# Compila os ubershaders do GX offline e gera os headers C que a runtime embute. Tudo o que sai é versionado: o build
# normal não precisa de compilador de shader e a runtime nunca compila shader em tempo de execução
# (docs/PLANO_PORT_C.md §3).
#   scripts/build_shaders.sh [hand|hlsl|all]   (padrão: all)
#
# hand (M8): runtime/shaders/gx_ubershader.metal (MSL à mão, framebuffer fetch) -> gx_ubershader.metallib e
#   gx_ubershader_metallib.h. Precisa do Metal Toolchain do Xcode (xcrun metal / metallib).
# hlsl (M9): runtime/shaders/gx_ubershader.hlsl -> com o SDL_shadercross, DXIL (D3D12), SPIR-V (Vulkan) e MSL 2.3
#   (Metal), este compilado para metallib; tudo junto em gx_ubershader_hlsl.h. O MSL gerado fica em
#   runtime/shaders/generated/ para leitura e diff. Precisa do shadercross ($SHADERCROSS, padrão o de
#   scripts/build_shadercross.sh), do Metal Toolchain e, para conferir o SPIR-V, do spirv-val (Homebrew).
#   docs/M9_PARTE1B_HLSL.md §2.
set -euo pipefail
ROOT=${0:A:h:h}
OUT=$ROOT/runtime/shaders
WHAT=${1:-all}
[[ $WHAT == hand || $WHAT == hlsl || $WHAT == all ]] || { echo "uso: $0 [hand|hlsl|all]" >&2; exit 2; }
TMP=${TMPDIR:-/tmp}/am_shaders.$$
mkdir -p $TMP
trap 'rm -rf $TMP' EXIT
# -mmacosx-version-min: o mesmo mínimo do SDL3 GPU com Metal.
METAL=(xcrun -sdk macosx metal -std=metal3.0 -mmacosx-version-min=13.0 -O2 -Werror)

# embed <header> <comentário> <nome1> <arq1> [<nome2> <arq2> ...]: um array C por arquivo.
embed() {
  python3 -I - "$@" <<'PY'
import sys
out, note, pairs = sys.argv[1], sys.argv[2], sys.argv[3:]
text = [f"/* Gerado por scripts/build_shaders.sh a partir de {note}: não editar. */\n", "/* clang-format off */\n"]
for name, path in zip(pairs[0::2], pairs[1::2]):
    data = open(path, "rb").read()
    rows = [", ".join(f"0x{b:02X}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    text.append(f"static const unsigned char {name}[{len(data)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n")
open(out, "w").write("".join(text))
PY
}

if [[ $WHAT == hand || $WHAT == all ]]; then
  # framebuffer fetch exige GPU da Apple (M1 em diante).
  $METAL -c $OUT/gx_ubershader.metal -o $TMP/gx.air
  xcrun -sdk macosx metallib $TMP/gx.air -o $OUT/gx_ubershader.metallib
  embed $OUT/gx_ubershader_metallib.h gx_ubershader.metal s_gx_ubershader_metallib $OUT/gx_ubershader.metallib
  echo "hand: gx_ubershader.metallib ($(wc -c < $OUT/gx_ubershader.metallib | tr -d ' ') bytes) e gx_ubershader_metallib.h"
fi

if [[ $WHAT == hlsl || $WHAT == all ]]; then
  SC=${SHADERCROSS:-/Volumes/AMRecomp/ext/shadercross/bin/shadercross}
  [[ -x $SC ]] || { echo "falta o shadercross em $SC (scripts/build_shadercross.sh)" >&2; exit 1; }
  export DYLD_LIBRARY_PATH=${SC:h:h}/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}
  SRC=$OUT/gx_ubershader.hlsl
  GEN=$OUT/generated
  mkdir -p $GEN
  for st in vertex:gx_vs fragment:gx_fs; do
    stage=${st%%:*}; entry=${st##*:}
    $SC $SRC -s HLSL -d DXIL -t $stage -e $entry -o $TMP/$entry.dxil
    $SC $SRC -s HLSL -d SPIRV -t $stage -e $entry -o $TMP/$entry.spv
    # --msl-version 2.3.0: com o padrão (1.2) o raster_order_group some sem aviso. -DAM_MSL_LAYOUT: bindings do
    # SPIR-V intermediário que fazem o shadercross numerar [[texture]]/[[buffer]] como a SDL espera (cabeçalho do .hlsl).
    $SC $SRC -s HLSL -d MSL -t $stage -e $entry -DAM_MSL_LAYOUT --msl-version 2.3.0 -o $GEN/${entry}_hlsl.metal
    [[ $(head -c 4 $TMP/$entry.dxil) == DXBC ]] || { echo "DXIL de $entry sem contêiner DXBC" >&2; exit 1; }
    if command -v spirv-val > /dev/null; then
      spirv-val --target-env vulkan1.2 $TMP/$entry.spv || { echo "spirv-val recusou $entry" >&2; exit 1; }
    else
      echo "aviso: sem spirv-val, SPIR-V de $entry não conferido"
    fi
    # O MSL gerado declara constantes que o código otimizado não usa: só esse aviso fica de fora.
    $METAL -Wno-unused-const-variable -c $GEN/${entry}_hlsl.metal -o $TMP/$entry.air
  done
  grep -q 'texture(0), raster_order_group(0)' $GEN/gx_fs_hlsl.metal \
    && grep -q 'texture(1), raster_order_group(0)' $GEN/gx_fs_hlsl.metal \
    && grep -q 'DrawState& DrawState \[\[buffer(0)\]\]' $GEN/gx_fs_hlsl.metal \
    && grep -q 'arena \[\[buffer(1)\]\]' $GEN/gx_fs_hlsl.metal \
    || { echo "MSL do gx_fs sem raster_order_group ou com registros fora do layout da SDL" >&2; exit 1; }
  if command -v spirv-dis > /dev/null; then
    spirv-dis $TMP/gx_fs.spv -o $TMP/gx_fs.spvasm
    grep -q 'OpExecutionMode %gx_fs PixelInterlockOrderedEXT' $TMP/gx_fs.spvasm \
      || { echo "SPIR-V do gx_fs sem PixelInterlockOrderedEXT" >&2; exit 1; }
    [[ $(grep -c 'OpBeginInvocationInterlockEXT' $TMP/gx_fs.spvasm) == 1 ]] \
      || { echo "SPIR-V do gx_fs com mais de uma região ordenada (o MSL perderia a ordem)" >&2; exit 1; }
  fi
  xcrun -sdk macosx metallib $TMP/gx_vs.air $TMP/gx_fs.air -o $TMP/hlsl.metallib
  embed $OUT/gx_ubershader_hlsl.h gx_ubershader.hlsl \
    s_gx_hlsl_vs_dxil $TMP/gx_vs.dxil s_gx_hlsl_fs_dxil $TMP/gx_fs.dxil \
    s_gx_hlsl_vs_spirv $TMP/gx_vs.spv s_gx_hlsl_fs_spirv $TMP/gx_fs.spv \
    s_gx_hlsl_metallib $TMP/hlsl.metallib
  echo "hlsl: DXIL $(wc -c < $TMP/gx_vs.dxil | tr -d ' ')+$(wc -c < $TMP/gx_fs.dxil | tr -d ' ')," \
    "SPIR-V $(wc -c < $TMP/gx_vs.spv | tr -d ' ')+$(wc -c < $TMP/gx_fs.spv | tr -d ' ')," \
    "metallib $(wc -c < $TMP/hlsl.metallib | tr -d ' ') bytes em gx_ubershader_hlsl.h"
fi
