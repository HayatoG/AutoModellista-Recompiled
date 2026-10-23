#!/bin/zsh
# Verificação local do M4 (substitui verify_m3.sh): clang-format, clang-tidy, builds Debug/Release arm64 e x86_64
# (Rosetta) com o C gerado + um build com AM_WITH_GENERATED=OFF (o do CI), ctest em todos, e o boot até main com
# comparação de trace e de estado inicial contra o Dolphin (docs/M4_BOOT.md §6).
#   AM_GENERATED_DIR=/Volumes/AMRecomp/recomp scripts/verify_m4.sh [--oracle] [--c128] [--update-golden]
#     --oracle         roda o Dolphin de novo (trace + estado no entry point) em vez de usar os de $AM_LOG_DIR
#     --c128           também compara com o C regenerado em trechos de 128 instruções (scripts/regen_c128.sh)
#     --update-golden  regrava tests/golden/m4_trace.json com os hashes desta execução
# Tudo que é pesado ou derivado do jogo vai para fora do repositório: builds em $BUILD_ROOT, traces em $AM_LOG_DIR.
set -euo pipefail
ROOT=${0:A:h:h}
GEN=${AM_GENERATED_DIR:?defina AM_GENERATED_DIR (diretório que contém dol/ e rels/)}
BUILD_ROOT=${BUILD_ROOT:-/Volumes/AMRecomp}
export AM_LOG_DIR=${AM_LOG_DIR:-/Volumes/AMRecomp/logs/m4}
SYS=${AM_SYS_DIR:-/Volumes/AMRecomp/work/extracted/sys}
ISO=${AM_ISO:-/Volumes/AMRecomp/work/iso/Auto Modellista (USA).rvz}
LLVM=${LLVM_BIN:-/opt/homebrew/opt/llvm@21/bin}
T=$ROOT/scripts/with_timeout.sh
ORACLE=0; C128=0; UPDATE=0
for a in "$@"; do
  case $a in
    --oracle) ORACLE=1 ;; --c128) C128=1 ;; --update-golden) UPDATE=1 ;;
    *) echo "opção desconhecida: $a"; exit 2 ;;
  esac
done
mkdir -p $AM_LOG_DIR
cd $ROOT

echo "== clang-format"
find runtime tests/unit -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 $LLVM/clang-format --dry-run -Werror

build() {  # build <dir> <cmake args...>
  local B=$1; shift
  cmake -S . -B $B -G Ninja "$@" > $AM_LOG_DIR/cmake_${B:t}.log
  cmake --build $B > $AM_LOG_DIR/build_${B:t}.log
  ctest --test-dir $B --output-on-failure | grep -E "tests passed|tests failed"
}

echo "== sem o C gerado (configuração do CI)"
build $BUILD_ROOT/build-ci-off -DCMAKE_BUILD_TYPE=Debug -DAM_WITH_GENERATED=OFF -DAM_SANITIZE=ON
$T 30 $BUILD_ROOT/build-ci-off/runtime/automodellista 2>&1 | tail -1

for combo in "arm64 Debug" "arm64 Release" "x86_64 Debug" "x86_64 Release"; do
  arch=${combo% *}; cfg=${combo#* }
  B=$BUILD_ROOT/build-$arch-${cfg:l}
  echo "== $arch $cfg ($B)"
  build $B -DCMAKE_BUILD_TYPE=$cfg -DCMAKE_OSX_ARCHITECTURES=$arch -DAM_GENERATED_DIR=$GEN
  $T 30 $B/runtime/automodellista 2>&1 | tail -1
  $T 180 $B/runtime/automodellista --sys $SYS --trace runtime_trace_$arch-${cfg:l}.txt 2>&1 | tail -1
  if [[ $arch == arm64 && $cfg == Debug ]]; then
    echo "== clang-tidy (compile_commands de $B)"
    find runtime tests/unit -type f -name '*.c' -print0 \
      | xargs -0 $LLVM/clang-tidy -p $B --quiet --extra-arg=-isysroot$(xcrun --show-sdk-path)
    $T 60 $B/runtime/automodellista --sys $SYS --dump-boot-state runtime_boot 2>&1 | tail -1
  fi
done

if [[ $ORACLE == 1 ]]; then
  echo "== oráculo: Dolphin (trace + estado no entry point)"
  $T 900 python3 -I scripts/dolphin_trace.py --iso "$ISO" --dolphin-log $AM_LOG_DIR/dolphin_trace.dolphin.log \
    trace --dol $SYS/main.dol --out $AM_LOG_DIR/dolphin_trace_raw.txt
  $T 300 python3 -I scripts/dolphin_trace.py --iso "$ISO" boot-state --out $AM_LOG_DIR/dolphin_boot
fi

echo "== estado inicial: runtime x Dolphin"
python3 -I scripts/compare_boot_state.py $AM_LOG_DIR/dolphin_boot $AM_LOG_DIR/runtime_boot | tail -1

for v in arm64-debug arm64-release x86_64-debug x86_64-release; do
  echo "== trace $v x Dolphin"
  python3 -I scripts/compare_trace.py --dol $SYS/main.dol --dolphin $AM_LOG_DIR/dolphin_trace_raw.txt \
    --runtime $AM_LOG_DIR/runtime_trace_$v.txt --summary-json $AM_LOG_DIR/compare_$v.json | tail -2
done

if [[ $C128 == 1 ]]; then
  echo "== trace com trechos de 128 instruções"
  [[ -d /Volumes/AMRecomp/recomp/c128/dol ]] || scripts/regen_c128.sh
  build $BUILD_ROOT/build-m4-c128 -DCMAKE_BUILD_TYPE=Release -DAM_GENERATED_DIR=/Volumes/AMRecomp/recomp/c128 \
    -DAM_BUILD_TESTS=OFF
  $T 180 $BUILD_ROOT/build-m4-c128/runtime/automodellista --sys $SYS --trace runtime_trace_c128.txt 2>&1 | tail -1
  python3 -I scripts/compare_trace.py --dol $SYS/main.dol --dolphin $AM_LOG_DIR/dolphin_trace_raw.txt \
    --runtime $AM_LOG_DIR/runtime_trace_c128.txt --summary-json $AM_LOG_DIR/compare_c128.json | tail -2
fi

echo "== golden (tests/golden/m4_trace.json)"
python3 -I scripts/check_m4_golden.py $AM_LOG_DIR $( [[ $UPDATE == 1 ]] && echo --update )
echo "== OK"
