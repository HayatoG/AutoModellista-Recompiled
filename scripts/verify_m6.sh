#!/bin/zsh
# Verificação local do M6 (docs/M6_PRIMEIRO_QUADRO.md §6): clang-format, builds Release arm64 e x86_64 (Rosetta), ctest
# nos dois, e o jogo até o logo da Capcom com o GX em software: o XFB dos quadros 1100 (fade) e 1250 (logo inteiro)
# comparado ao do Dolphin no mesmo quadro (PSNR e pixels ruins, scripts/compare_shots.py), mais uma execução com a
# janela SDL3 (arm64) cuja imagem lida de volta tem de ser a mesma da captura.
#   scripts/verify_m6.sh [--oracle] [--update-golden] [--no-window]
#     --oracle         roda o Dolphin de novo (capturas com o mesmo roteiro) em vez de usar as de $AM_LOG_DIR
#     --update-golden  regrava tests/golden/m6_frame.json com o resumo desta execução
#     --no-window      pula a execução com janela (sessão sem tela)
# Builds em $BUILD_ROOT (os mesmos do verify_m4.sh/verify_m5.sh), logs e capturas em $AM_LOG_DIR. As imagens são do jogo:
# ficam fora do repositório; no golden só hashes e métricas.
set -euo pipefail
ROOT=${0:A:h:h}
GEN=${AM_GENERATED_DIR:-/Volumes/AMRecomp/recomp/m5}
BUILD_ROOT=${BUILD_ROOT:-/Volumes/AMRecomp}
export AM_LOG_DIR=${AM_LOG_DIR:-/Volumes/AMRecomp/logs/m6/verify}
SYS=${AM_SYS_DIR:-/Volumes/AMRecomp/work/extracted/sys}
ISO=${AM_ISO:-/Volumes/AMRecomp/work/iso/Auto Modellista (USA).rvz}
LLVM=${LLVM_BIN:-/opt/homebrew/opt/llvm@21/bin}
T=$ROOT/scripts/with_timeout.sh
# Abertura sem pular nada: só o "No Memory Card" -> Yes (como no M5). O logo da Capcom entra em fade a partir do
# quadro ~1076 e fica inteiro de ~1110 a ~1410 nos dois lados.
PAD="320:D_LEFT:8,360:A:8"
FRAMES=1260
SHOTS=(1100 1250)
ORACLE=0; UPDATE=0; WINDOW=1
for a in "$@"; do
  case $a in
    --oracle) ORACLE=1 ;; --update-golden) UPDATE=1 ;; --no-window) WINDOW=0 ;;
    *) echo "opção desconhecida: $a"; exit 2 ;;
  esac
done
[[ -d $GEN/rels && -d $GEN/dol ]] || { echo "falta o C gerado em $GEN (scripts/regen_rels_m5.sh)"; exit 1; }
ls /Volumes/AMRecomp > /dev/null || { echo "volume /Volumes/AMRecomp ausente"; exit 1; }
mkdir -p $AM_LOG_DIR
cd $ROOT

echo "== clang-format"
find runtime tests/unit -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 $LLVM/clang-format --dry-run -Werror

if [[ $ORACLE == 1 || ! -f $AM_LOG_DIR/dolphin_f${SHOTS[-1]}.png ]]; then
  echo "== oráculo: Dolphin (Metal, XFB na RAM) com o roteiro, capturas nos quadros ${(j:,:)SHOTS}"
  rm -f $AM_LOG_DIR/dolphin_f*.png(N)
  $T 900 python3 -I scripts/dolphin_trace.py --iso "$ISO" --video Metal \
    --dolphin-log $AM_LOG_DIR/dolphin.log events --pad-script "$PAD" --frames $FRAMES --shots ${(j:,:)SHOTS} \
    --timeout 800 --out $AM_LOG_DIR/dolphin.txt | tail -1
fi

shot_args() { # $1 = prefixo
  local s=() f
  for f in $SHOTS; do s+=("$f:$1_f$f.png"); done
  echo ${(j:,:)s}
}

rm -f $AM_LOG_DIR/compare_*.json(N)
for arch in arm64 x86_64; do
  B=$BUILD_ROOT/build-$arch-release
  echo "== $arch Release ($B)"
  cmake -S . -B $B -G Ninja -UAM_SDL3_TAG -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=$arch \
    -DAM_GENERATED_DIR=$GEN > $AM_LOG_DIR/cmake_$arch.log
  cmake --build $B > $AM_LOG_DIR/build_$arch.log
  ctest --test-dir $B --output-on-failure | grep -E "tests passed|tests failed"
  t0=$(date +%s)
  (cd $AM_LOG_DIR && $T 900 $B/runtime/automodellista --sys $SYS --frames $FRAMES --pad-script "$PAD" \
    --shot $(shot_args runtime_$arch) --max-seconds 800 > runtime_$arch.log 2>&1) \
    || { tail -5 $AM_LOG_DIR/runtime_$arch.log; exit 1; }
  echo "$(tail -1 $AM_LOG_DIR/runtime_$arch.log | sed 's/^\[INFO\] //') em $(( $(date +%s) - t0 )) s"
  grep -E "^\[INFO\] (GX render|EFB:)" $AM_LOG_DIR/runtime_$arch.log | sed 's/^\[INFO\] /  /'
  for f in $SHOTS; do
    python3 -I scripts/compare_shots.py $AM_LOG_DIR/runtime_${arch}_f$f.png $AM_LOG_DIR/dolphin_f$f.png \
      --side-by-side $AM_LOG_DIR/lado_${arch}_f$f.png --diff $AM_LOG_DIR/diff_${arch}_f$f.png \
      --json $AM_LOG_DIR/compare_${arch}_f$f.json
  done
done

if [[ $WINDOW == 1 ]]; then
  echo "== janela SDL3 (arm64): a imagem desenhada na janela no quadro ${SHOTS[-1]}"
  B=$BUILD_ROOT/build-arm64-release
  (cd $AM_LOG_DIR && $T 900 $B/runtime/automodellista --sys $SYS --frames $FRAMES --pad-script "$PAD" \
    --shot ${SHOTS[-1]}:window_f${SHOTS[-1]}.png --window 1 --max-seconds 800 > runtime_window.log 2>&1) \
    || { tail -5 $AM_LOG_DIR/runtime_window.log; exit 1; }
  python3 -I scripts/compare_shots.py $AM_LOG_DIR/window_f${SHOTS[-1]}_janela.png \
    $AM_LOG_DIR/runtime_arm64_f${SHOTS[-1]}.png --min-psnr 99
fi

echo "== golden (tests/golden/m6_frame.json)"
python3 -I scripts/check_m6_golden.py $AM_LOG_DIR --pad-script "$PAD" --frames $FRAMES \
  $( [[ $UPDATE == 1 ]] && echo --update )
echo "== OK"
