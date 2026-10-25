#!/bin/zsh
# Verificação local completa do M3: clang-format, clang-tidy e builds Debug/Release em arm64 e x86_64 (Rosetta) com o
# C gerado ligado, rodando ctest e o executável em cada um. Builds pesados vão para $BUILD_ROOT (fora do disco interno).
#   AM_GENERATED_DIR=/Volumes/AMRecomp/recomp scripts/verify_m3.sh
set -euo pipefail
ROOT=${0:A:h:h}
GEN=${AM_GENERATED_DIR:?defina AM_GENERATED_DIR (diretório que contém dol/ e rels/)}
BUILD_ROOT=${BUILD_ROOT:-/Volumes/AMRecomp}
LLVM=${LLVM_BIN:-/opt/homebrew/opt/llvm@21/bin}
cd $ROOT

echo "== clang-format"
find runtime tests/unit -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 $LLVM/clang-format --dry-run -Werror

for combo in "arm64 Debug" "arm64 Release" "x86_64 Debug" "x86_64 Release"; do
  arch=${combo% *}; cfg=${combo#* }
  B=$BUILD_ROOT/build-$arch-${cfg:l}
  echo "== $arch $cfg ($B)"
  cmake -S . -B $B -G Ninja -DCMAKE_BUILD_TYPE=$cfg -DCMAKE_OSX_ARCHITECTURES=$arch -DAM_GENERATED_DIR=$GEN > /dev/null
  cmake --build $B
  file $B/runtime/automodellista
  ctest --test-dir $B --output-on-failure | tail -3
  $B/runtime/automodellista --selftest 2>&1 | tail -2
  if [[ $arch == arm64 && $cfg == Debug ]]; then
    echo "== clang-tidy (compile_commands de $B)"
    find runtime tests/unit -type f -name '*.c' -print0 \
      | xargs -0 $LLVM/clang-tidy -p $B --quiet --extra-arg=-isysroot$(xcrun --show-sdk-path)
  fi
done
echo "== OK"
