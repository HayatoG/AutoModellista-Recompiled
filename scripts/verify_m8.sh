#!/bin/zsh
# Verificação local do M8, parte 1 (docs/M8_CORRIDA.md §5): clang-format, builds Release arm64 e x86_64 (Rosetta), ctest
# nos dois e o roteiro da corrida até a largada em Suzuka (6400 quadros):
#   - tempo emulado contra o Dolphin (--timebase-out dos dois lados, scripts/compare_timebase.py);
#   - capturas do filme da abertura (q1600), "PRESS START" (q1800), menu principal (q2300) e largada (q6400) com o GX
#     em software e na GPU, comparadas entre si e com o Dolphin (scripts/compare_shots.py);
#   - a janela no modo de tempo real com a GPU: quadros/s do jogo e desenhados no menu e na corrida.
#   scripts/verify_m8.sh [--oracle] [--update-golden] [--no-window] [--cel]
#     --oracle         roda o Dolphin de novo (capturas e timebase) em vez de usar os de $AM_LOG_DIR
#     --update-golden  regrava tests/golden/m8_corrida.json com o resumo desta execução
#     --no-window      pula as execuções com janela (sessão sem tela)
#     --cel            M8 parte 2b: acelera e anda na pista até o quadro 8400 (soft e GPU contra o Dolphin, contagem
#                      do GX no trecho; docs/M8_PARTE2B_CEL.md). Roda no lugar do roteiro padrão, só em arm64.
# Builds em $BUILD_ROOT (os mesmos dos verify anteriores); logs, capturas e traces em $AM_LOG_DIR, fora do repositório.
set -euo pipefail
ROOT=${0:A:h:h}
GEN=${AM_GENERATED_DIR:-/Volumes/AMRecomp/recomp/m5}
BUILD_ROOT=${BUILD_ROOT:-/Volumes/AMRecomp}
export AM_LOG_DIR=${AM_LOG_DIR:-/Volumes/AMRecomp/logs/m8/verify}
SYS=${AM_SYS_DIR:-/Volumes/AMRecomp/work/extracted/sys}
ISO=${AM_ISO:-/Volumes/AMRecomp/work/iso/Auto Modellista (USA).rvz}
LLVM=${LLVM_BIN:-/opt/homebrew/opt/llvm@21/bin}
T=$ROOT/scripts/with_timeout.sh
# Roteiro da corrida (o de scripts/assistir.sh corrida): menus, carro, pista, música e a largada.
PAD="320:D_LEFT:8,360:A:8,1700:START:8,2000:START:8,2600:A:8,2900:A:8,3300:A:8,3600:A:8,3900:A:8,4200:A:8"
PAD="$PAD,4500:A:8,4800:A:8,5100:A:8,5400:A:8,5700:A:8"
FRAMES=6400
SHOTS=(1600 1800 2300 6400)
ORACLE=0; UPDATE=0; WINDOW=1; CEL=0
for a in "$@"; do
  case $a in
    --oracle) ORACLE=1 ;; --update-golden) UPDATE=1 ;; --no-window) WINDOW=0 ;; --cel) CEL=1 ;;
    *) echo "opção desconhecida: $a"; exit 2 ;;
  esac
done
[[ -d $GEN/rels && -d $GEN/dol ]] || { echo "falta o C gerado em $GEN (scripts/regen_rels_m5.sh)"; exit 1; }
ls /Volumes/AMRecomp > /dev/null || { echo "volume /Volumes/AMRecomp ausente"; exit 1; }
mkdir -p $AM_LOG_DIR
cd $ROOT

echo "== clang-format"
find runtime tests/unit -type f \( -name '*.c' -o -name '*.h' \) ! -name '*_metallib.h' -print0 \
  | xargs -0 $LLVM/clang-format --dry-run -Werror

# Trecho do cel-shading (docs/M8_PARTE2B_CEL.md): acelera (A) a partir do quadro 6900 e vira à direita em 7700.
if [[ $CEL == 1 ]]; then
  CPAD="$PAD,6900:A:1500,7700:D_RIGHT:120"; CSHOTS=(7400 7600 7750 7850 7950 8100 8400); CF=8400
  mkdir -p $AM_LOG_DIR/cel
  cshots() { local s=() f; for f in $CSHOTS; do s+=("$f:cel/$1_f$f.png"); done; echo ${(j:,:)s}; }
  if [[ $ORACLE == 1 || ! -f $AM_LOG_DIR/cel/dolphin_f$CF.png ]]; then
    echo "== cel: Dolphin com o roteiro da corrida andando"
    lockf /Volumes/AMRecomp/wt-builds/dolphin.lock $T 1900 python3 -I scripts/dolphin_trace.py --iso "$ISO" --video Metal \
      --dolphin-log $AM_LOG_DIR/cel/dolphin.log events --pad-script "$CPAD" --frames $CF \
      --shots ${(j:,:)CSHOTS} --timebase-out $AM_LOG_DIR/cel/dolphin_tb.txt --timeout 1800 \
      --out $AM_LOG_DIR/cel/dolphin.txt | tail -1
  fi
  B=$BUILD_ROOT/build-arm64-release
  for gfx in soft gpu; do
    (cd $AM_LOG_DIR && $T 3000 $B/runtime/automodellista --sys $SYS --frames $CF --pad-script "$CPAD" --gfx $gfx \
      --audio off --shot $(cshots $gfx) --max-seconds 2900 > cel/$gfx.log 2>&1) || { tail -5 $AM_LOG_DIR/cel/$gfx.log; exit 1; }
    echo "  cel --gfx $gfx: $(grep -E '^\[INFO\] (GX render|TEV:)' $AM_LOG_DIR/cel/$gfx.log | sed 's/^\[INFO\] //' | tr '\n' ' ')"
  done
  for f in $CSHOTS; do
    L=$AM_LOG_DIR/cel
    python3 -I scripts/compare_shots.py $L/soft_f$f.png $L/dolphin_f$f.png --min-psnr 30 --max-bad 2 \
      --side-by-side $L/lado_soft_dolphin_f$f.png | sed 's/^/  cel soft x Dolphin: /'
    python3 -I scripts/compare_shots.py $L/gpu_f$f.png $L/dolphin_f$f.png --min-psnr 34 --max-bad 1 \
      --side-by-side $L/lado_gpu_dolphin_f$f.png | sed 's/^/  cel GPU x Dolphin: /'
  done
  echo "== OK (cel)"
  exit 0
fi

if [[ $ORACLE == 1 || ! -f $AM_LOG_DIR/dolphin_f${SHOTS[-1]}.png || ! -f $AM_LOG_DIR/dolphin_tb.txt ]]; then
  echo "== oráculo: Dolphin (Metal, XFB na RAM) com o roteiro da corrida, capturas e timebase"
  rm -f $AM_LOG_DIR/dolphin_f*.png(N) $AM_LOG_DIR/dolphin_tb.txt(N)
  $T 1500 python3 -I scripts/dolphin_trace.py --iso "$ISO" --video Metal --dolphin-log $AM_LOG_DIR/dolphin.log \
    events --pad-script "$PAD" --frames $FRAMES --shots ${(j:,:)SHOTS} --timebase-out $AM_LOG_DIR/dolphin_tb.txt \
    --timeout 1400 --out $AM_LOG_DIR/dolphin.txt | tail -1
fi

shot_args() { # $1 = prefixo
  local s=() f
  for f in $SHOTS; do s+=("$f:$1_f$f.png"); done
  echo ${(j:,:)s}
}

# Média de "quadros/s" e de "desenhado(s)" nas linhas de progresso entre os quadros $2 e $3 do log $1.
fps_between() {
  grep -o "quadro [0-9]*: .*quadros/s, [0-9]* desenhado" $1 \
    | awk -v a=$2 -v b=$3 '{f=$2+0} f>a && f<=b {s+=$(NF-3); d+=$(NF-1); n++} END {if (n) printf "%.1f/%.1f", s/n, d/n; else print "?"}'
}

# Limiares (docs/M8_CORRIDA.md §5): GPU contra o software e contra o Dolphin >= 30 dB; software contra o Dolphin como
# no M7, menos a largada (a câmera ainda está 2 campos atrás, §2).
min_soft() { [[ $1 == 6400 ]] && echo "--min-psnr 28 --max-bad 4" || echo "--min-psnr 30 --max-bad 1" }

rm -f $AM_LOG_DIR/*_f*.json(N) $AM_LOG_DIR/timebase_*.json(N)
for arch in arm64 x86_64; do
  B=$BUILD_ROOT/build-$arch-release
  echo "== $arch Release ($B)"
  cmake -S . -B $B -G Ninja -UAM_SDL3_TAG -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=$arch \
    -DAM_GENERATED_DIR=$GEN > $AM_LOG_DIR/cmake_$arch.log
  cmake --build $B > $AM_LOG_DIR/build_$arch.log
  ctest --test-dir $B --output-on-failure | grep -E "tests passed|tests failed"
  for gfx in soft gpu; do
    t0=$(date +%s)
    extra=()
    [[ $gfx == soft ]] && extra=(--timebase-out runtime_tb_$arch.txt)
    (cd $AM_LOG_DIR && $T 1500 $B/runtime/automodellista --sys $SYS --frames $FRAMES --pad-script "$PAD" --gfx $gfx \
      --shot $(shot_args ${gfx}_$arch) $extra --max-seconds 1400 > runtime_${gfx}_$arch.log 2>&1) \
      || { tail -5 $AM_LOG_DIR/runtime_${gfx}_$arch.log; exit 1; }
    echo "  --gfx $gfx: $FRAMES quadros em $(( $(date +%s) - t0 )) s"
  done
  (cd $AM_LOG_DIR && $T 1500 $B/runtime/automodellista --sys $SYS --frames $FRAMES --pad-script "$PAD" --gfx gpu \
    --gx-thread on --shot $(shot_args gputh_$arch) --timebase-out runtime_tb_gputh_$arch.txt --max-seconds 1400 \
    > runtime_gputh_$arch.log 2>&1) || { tail -5 $AM_LOG_DIR/runtime_gputh_$arch.log; exit 1; }
  for f in $SHOTS; do
    cmp -s $AM_LOG_DIR/gputh_${arch}_f$f.png $AM_LOG_DIR/gpu_${arch}_f$f.png \
      || { echo "  --gx-thread on: captura q$f DIFERENTE"; exit 1; }
  done
  cmp -s $AM_LOG_DIR/runtime_tb_gputh_$arch.txt $AM_LOG_DIR/runtime_tb_$arch.txt \
    || { echo "  --gx-thread on: tempo emulado DIFERENTE"; exit 1; }
  echo "  --gx-thread on: capturas da GPU e tempo emulado idênticos; $(grep -o 'GX thread: .*' $AM_LOG_DIR/runtime_gputh_$arch.log)"
  grep -E "^\[INFO\] GX GPU" $AM_LOG_DIR/runtime_gpu_$arch.log | sed 's/^\[INFO\] /  /'
  python3 -I scripts/compare_timebase.py $AM_LOG_DIR/runtime_tb_$arch.txt $AM_LOG_DIR/dolphin_tb.txt --frame $FRAMES \
    --max-lag 3 --json $AM_LOG_DIR/timebase_$arch.json | sed 's/^/  /'
  for f in $SHOTS; do
    L=$AM_LOG_DIR
    python3 -I scripts/compare_shots.py $L/soft_${arch}_f$f.png $L/dolphin_f$f.png $(min_soft $f) \
      --json $L/soft_${arch}_f$f.json --side-by-side $L/lado_soft_${arch}_f$f.png | sed 's/^/  soft x Dolphin: /'
    python3 -I scripts/compare_shots.py $L/gpu_${arch}_f$f.png $L/soft_${arch}_f$f.png --min-psnr 30 --max-bad 3 \
      --json $L/gpu_soft_${arch}_f$f.json --side-by-side $L/lado_gpu_soft_${arch}_f$f.png | sed 's/^/  GPU x soft: /'
    python3 -I scripts/compare_shots.py $L/gpu_${arch}_f$f.png $L/dolphin_f$f.png --min-psnr 30 --max-bad 1 \
      --json $L/gpu_dolphin_${arch}_f$f.json --side-by-side $L/lado_gpu_dolphin_${arch}_f$f.png \
      | sed 's/^/  GPU x Dolphin: /'
  done
done
cmp -s $AM_LOG_DIR/runtime_tb_arm64.txt $AM_LOG_DIR/runtime_tb_x86_64.txt \
  && echo "tempo emulado idêntico nos dois builds" || { echo "tempo emulado DIFERENTE entre arm64 e x86_64"; exit 1; }

if [[ $WINDOW == 1 ]]; then
  for arch in arm64 x86_64; do
    B=$BUILD_ROOT/build-$arch-release
    echo "== janela com a GPU ($arch): tempo real, pulo de quadros, sem som, até $(( FRAMES + 200 )) quadros"
    (cd $AM_LOG_DIR && $T 1200 $B/runtime/automodellista --sys $SYS --frames $(( FRAMES + 200 )) --pad-script "$PAD" \
      --gfx gpu --window 1 --realtime on --frameskip on --audio off --max-seconds 1100 > window_gpu_$arch.log 2>&1) \
      || { tail -5 $AM_LOG_DIR/window_gpu_$arch.log; exit 1; }
    echo "  quadros/s do jogo/desenhados: menu principal (2160-2400) $(fps_between $AM_LOG_DIR/window_gpu_$arch.log 2160 2400);" \
      "corrida ($(( FRAMES - 160 ))-$(( FRAMES + 200 ))) $(fps_between $AM_LOG_DIR/window_gpu_$arch.log $(( FRAMES - 160 )) $(( FRAMES + 200 )))"
  done
fi

echo "== golden (tests/golden/m8_corrida.json)"
python3 -I scripts/check_m8_golden.py $AM_LOG_DIR --pad-script "$PAD" --frames $FRAMES \
  $( [[ $UPDATE == 1 ]] && echo --update )
echo "== OK"
