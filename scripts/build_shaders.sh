#!/bin/zsh
# M8: compila o ubershader do GX (runtime/shaders/gx_ubershader.metal) offline para metallib e gera o header C que a
# runtime embute (runtime/shaders/gx_ubershader_metallib.h). Os dois saem versionados: o build não precisa do
# compilador Metal e a runtime nunca compila shader em tempo de execução (docs/PLANO_PORT_C.md §3).
#   scripts/build_shaders.sh
# Precisa do Metal Toolchain do Xcode (xcrun metal / metallib). SPIR-V e DXIL (Vulkan, D3D12) ainda não existem:
# docs/M8_CORRIDA.md §3.
set -euo pipefail
ROOT=${0:A:h:h}
SRC=$ROOT/runtime/shaders/gx_ubershader.metal
OUT=$ROOT/runtime/shaders
TMP=${TMPDIR:-/tmp}/am_shaders.$$
mkdir -p $TMP
trap 'rm -rf $TMP' EXIT
# -mmacosx-version-min: o mesmo mínimo do SDL3 GPU com Metal; framebuffer fetch exige GPU da Apple (M1 em diante).
xcrun -sdk macosx metal -std=metal3.0 -mmacosx-version-min=13.0 -O2 -Werror -c $SRC -o $TMP/gx.air
xcrun -sdk macosx metallib $TMP/gx.air -o $OUT/gx_ubershader.metallib
python3 -I - $OUT/gx_ubershader.metallib $OUT/gx_ubershader_metallib.h <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
rows = [", ".join(f"0x{b:02X}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
open(sys.argv[2], "w").write(
    "/* Gerado por scripts/build_shaders.sh a partir de gx_ubershader.metal: não editar. */\n"
    "/* clang-format off */\n"
    f"static const unsigned char GX_UBERSHADER_METALLIB[{len(data)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n")
PY
echo "$OUT/gx_ubershader.metallib ($(wc -c < $OUT/gx_ubershader.metallib) bytes) e gx_ubershader_metallib.h"
