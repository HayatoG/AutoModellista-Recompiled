#!/bin/zsh
# Verificação local do M5 (docs/M5_OS_DISCO.md §6): clang-format, builds Release arm64 e x86_64 (Rosetta) com o C
# gerado dos RELs fora da MEM1 (scripts/regen_rels_m5.sh), ctest nos dois, e o jogo com o roteiro de controle até a
# corrida: OSLink do game.rel + pelo menos 600 quadros do laço fn_80051034, eventos comparados com os do Dolphin.
#   scripts/verify_m5.sh [--oracle] [--update-golden]
#     --oracle         roda o Dolphin de novo (eventos com o mesmo roteiro) em vez de usar o de $AM_LOG_DIR
#     --update-golden  regrava tests/golden/m5_events.json com o resumo desta execução
# Builds em $BUILD_ROOT (os mesmos do verify_m4.sh: rode os dois com AM_GENERATED_DIR=/Volumes/AMRecomp/recomp/m5),
# logs em $AM_LOG_DIR. Nada derivado do jogo entra no repositório.
set -euo pipefail
ROOT=${0:A:h:h}
GEN=${AM_GENERATED_DIR:-/Volumes/AMRecomp/recomp/m5}
BUILD_ROOT=${BUILD_ROOT:-/Volumes/AMRecomp}
export AM_LOG_DIR=${AM_LOG_DIR:-/Volumes/AMRecomp/logs/m5/verify}
SYS=${AM_SYS_DIR:-/Volumes/AMRecomp/work/extracted/sys}
ISO=${AM_ISO:-/Volumes/AMRecomp/work/iso/Auto Modellista (USA).rvz}
LLVM=${LLVM_BIN:-/opt/homebrew/opt/llvm@21/bin}
T=$ROOT/scripts/with_timeout.sh
# Menus até a corrida (§4): "No Memory Card" -> Yes; logos e filme -> START; PRESS START; arcade -> single race;
# depois A a cada 300 quadros (pista, música, carro, estilo, ajuste, regras, "go race"). O jogo liga o game.rel no
# quadro 5731 nos dois lados; 6400 quadros dão 669 depois do OSLink.
PAD="320:D_LEFT:8,360:A:8,1700:START:8,2000:START:8,2600:A:8,2900:A:8,3300:A:8,3600:A:8,3900:A:8,4200:A:8"
PAD="$PAD,4500:A:8,4800:A:8,5100:A:8,5400:A:8,5700:A:8"
FRAMES=6400
ORACLE=0; UPDATE=0
for a in "$@"; do
  case $a in
    --oracle) ORACLE=1 ;; --update-golden) UPDATE=1 ;;
    *) echo "opção desconhecida: $a"; exit 2 ;;
  esac
done
[[ -d $GEN/rels && -d $GEN/dol ]] || { echo "falta o C gerado em $GEN (scripts/regen_rels_m5.sh)"; exit 1; }
mkdir -p $AM_LOG_DIR
cd $ROOT

echo "== clang-format"
find runtime tests/unit -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 $LLVM/clang-format --dry-run -Werror

if [[ $ORACLE == 1 || ! -f $AM_LOG_DIR/dolphin_events.txt ]]; then
  echo "== oráculo: Dolphin com o roteiro ($FRAMES quadros)"
  $T 1800 python3 -I scripts/dolphin_trace.py --iso "$ISO" --dolphin-log $AM_LOG_DIR/dolphin_events.dolphin.log \
    events --pad-script "$PAD" --frames $FRAMES --timeout 1700 --out $AM_LOG_DIR/dolphin_events.txt | tail -1
fi

rm -f $AM_LOG_DIR/compare_*.json(N)
for arch in arm64 x86_64; do
  B=$BUILD_ROOT/build-$arch-release
  echo "== $arch Release ($B)"
  # -UAM_SDL3_TAG: caches antigos guardavam a SDL 3.4.14; vale a versão fixada no CMakeLists (3.4.18).
  cmake -S . -B $B -G Ninja -UAM_SDL3_TAG -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=$arch \
    -DAM_GENERATED_DIR=$GEN > $AM_LOG_DIR/cmake_$arch.log
  cmake --build $B > $AM_LOG_DIR/build_$arch.log
  ctest --test-dir $B --output-on-failure | grep -E "tests passed|tests failed"
  t0=$(date +%s)
  (cd $AM_LOG_DIR && $T 1200 $B/runtime/automodellista --sys $SYS --frames $FRAMES --pad-script "$PAD" \
    --events runtime_events_$arch.txt --max-seconds 1100 > runtime_$arch.log 2>&1) || { tail -5 $AM_LOG_DIR/runtime_$arch.log; exit 1; }
  echo "$(tail -1 $AM_LOG_DIR/runtime_$arch.log | sed 's/^\[INFO\] //') em $(( $(date +%s) - t0 )) s"
  python3 -I scripts/compare_events.py --dolphin $AM_LOG_DIR/dolphin_events.txt \
    --runtime $AM_LOG_DIR/runtime_events_$arch.txt --summary-json $AM_LOG_DIR/compare_$arch.json \
    | grep -vE "^  trecho"
done

echo "== golden (tests/golden/m5_events.json)"
python3 -I scripts/check_m5_golden.py $AM_LOG_DIR --pad-script "$PAD" --frames $FRAMES \
  $( [[ $UPDATE == 1 ]] && echo --update )
echo "== OK"
