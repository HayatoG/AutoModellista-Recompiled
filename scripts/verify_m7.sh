#!/bin/zsh
# Verificação local do M7 (docs/M7_MENUS.md §6): clang-format, builds Release arm64 e x86_64 (Rosetta), ctest nos
# dois, e o jogo pelo roteiro do M5 até a escolha da música (3600 quadros) com o GX em software: capturas do filme da
# abertura e dos menus comparadas às do Dolphin (scripts/compare_shots.py) e o --audio-dump comparado ao dspdump.wav
# do Dolphin (scripts/compare_audio.py). Depois, no arm64: as mesmas capturas com 1 trabalhador e sem o cache de
# texturas (têm de ser idênticas), a janela SDL3 (a imagem lida de volta = a captura) e o modo de tempo real com pulo de
# quadros (quadros/s do jogo e da janela).
#   scripts/verify_m7.sh [--oracle] [--update-golden] [--no-window]
#     --oracle         roda o Dolphin de novo (capturas e áudio com o mesmo roteiro) em vez de usar os de $AM_LOG_DIR
#     --update-golden  regrava tests/golden/m7_menus.json com o resumo desta execução
#     --no-window      pula as execuções com janela (sessão sem tela)
# Builds em $BUILD_ROOT (os mesmos do verify_m4/m5/m6), logs, capturas e WAV em $AM_LOG_DIR: são do jogo e ficam fora do
# repositório; no golden só hashes e métricas.
set -euo pipefail
ROOT=${0:A:h:h}
GEN=${AM_GENERATED_DIR:-/Volumes/AMRecomp/recomp/m5}
BUILD_ROOT=${BUILD_ROOT:-/Volumes/AMRecomp}
export AM_LOG_DIR=${AM_LOG_DIR:-/Volumes/AMRecomp/logs/m7/verify}
SYS=${AM_SYS_DIR:-/Volumes/AMRecomp/work/extracted/sys}
ISO=${AM_ISO:-/Volumes/AMRecomp/work/iso/Auto Modellista (USA).rvz}
LLVM=${LLVM_BIN:-/opt/homebrew/opt/llvm@21/bin}
T=$ROOT/scripts/with_timeout.sh
# Roteiro do M5 (docs/M5_OS_DISCO.md §4) até a escolha da música. Capturas: o filme da abertura (q1600), "PRESS START"
# (q1800), menu principal (q2300), modo (q2880), pista (q3200) e música (q3500).
PAD="320:D_LEFT:8,360:A:8,1700:START:8,2000:START:8,2600:A:8,2900:A:8,3300:A:8"
FRAMES=3600
SHOTS=(1600 1800 2300 2880 3200 3500)
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

if [[ $ORACLE == 1 || ! -f $AM_LOG_DIR/dolphin_f${SHOTS[-1]}.png || ! -f $AM_LOG_DIR/dolphin.wav ]]; then
  echo "== oráculo: Dolphin (Metal, XFB na RAM, DumpAudio) com o roteiro, capturas nos quadros ${(j:,:)SHOTS}"
  rm -f $AM_LOG_DIR/dolphin_f*.png(N) $AM_LOG_DIR/dolphin.wav(N)
  $T 1200 python3 -I scripts/dolphin_trace.py --iso "$ISO" --video Metal \
    --dolphin-log $AM_LOG_DIR/dolphin.log events --pad-script "$PAD" --frames $FRAMES --shots ${(j:,:)SHOTS} \
    --audio-dump $AM_LOG_DIR/dolphin.wav --timeout 1100 --out $AM_LOG_DIR/dolphin.txt | tail -2
fi

shot_args() { # $1 = prefixo
  local s=() f
  for f in $SHOTS; do s+=("$f:$1_f$f.png"); done
  echo ${(j:,:)s}
}

# Média de quadros/s nas linhas de progresso entre os quadros $2 e $3 do log $1 (menu principal: 2160..2400).
fps_between() {
  grep -o "quadro [0-9]*: .*quadros/s" $1 | awk -v a=$2 -v b=$3 '{f=$2+0} f>a && f<=b {s+=$(NF-1); n++} END {if (n) printf "%.1f", s/n; else print "?"}'
}

rm -f $AM_LOG_DIR/compare_*.json(N) $AM_LOG_DIR/audio_*.json(N)
for arch in arm64 x86_64; do
  B=$BUILD_ROOT/build-$arch-release
  echo "== $arch Release ($B)"
  cmake -S . -B $B -G Ninja -UAM_SDL3_TAG -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=$arch \
    -DAM_GENERATED_DIR=$GEN > $AM_LOG_DIR/cmake_$arch.log
  cmake --build $B > $AM_LOG_DIR/build_$arch.log
  ctest --test-dir $B --output-on-failure | grep -E "tests passed|tests failed"
  t0=$(date +%s)
  (cd $AM_LOG_DIR && $T 1500 $B/runtime/automodellista --sys $SYS --frames $FRAMES --pad-script "$PAD" \
    --shot $(shot_args runtime_$arch) --audio-dump runtime_$arch.wav --max-seconds 1400 > runtime_$arch.log 2>&1) \
    || { tail -5 $AM_LOG_DIR/runtime_$arch.log; exit 1; }
  echo "$(tail -1 $AM_LOG_DIR/runtime_$arch.log | sed 's/^\[INFO\] //') em $(( $(date +%s) - t0 )) s;" \
    "menu principal a $(fps_between $AM_LOG_DIR/runtime_$arch.log 2160 2400) quadros/s"
  grep -E "^\[INFO\] (GX render|GX cache|GX recursos|EFB:|AX:|áudio:)" $AM_LOG_DIR/runtime_$arch.log | sed 's/^\[INFO\] /  /'
  for f in $SHOTS; do
    python3 -I scripts/compare_shots.py $AM_LOG_DIR/runtime_${arch}_f$f.png $AM_LOG_DIR/dolphin_f$f.png \
      --side-by-side $AM_LOG_DIR/lado_${arch}_f$f.png --diff $AM_LOG_DIR/diff_${arch}_f$f.png \
      --json $AM_LOG_DIR/compare_${arch}_f$f.json
  done
  python3 -I scripts/compare_audio.py $AM_LOG_DIR/runtime_$arch.wav $AM_LOG_DIR/dolphin.wav \
    --json $AM_LOG_DIR/audio_$arch.json | tail -1
done

echo "== arm64: 1 trabalhador e sem cache de texturas (as capturas têm de ser as mesmas)"
B=$BUILD_ROOT/build-arm64-release
(cd $AM_LOG_DIR && $T 1500 $B/runtime/automodellista --sys $SYS --frames 2300 --pad-script "$PAD" \
  --gx-threads 1 --gx-tex-cache off --shot 1600:serial_f1600.png,2300:serial_f2300.png --max-seconds 1400 \
  > runtime_serial.log 2>&1) || { tail -5 $AM_LOG_DIR/runtime_serial.log; exit 1; }
for f in 1600 2300; do
  cmp -s $AM_LOG_DIR/serial_f$f.png $AM_LOG_DIR/runtime_arm64_f$f.png \
    && echo "q$f: idêntica à captura com $(sysctl -n hw.ncpu) trabalhadores e cache" \
    || { echo "q$f: DIFERENTE com 1 trabalhador e sem cache"; exit 1; }
done

if [[ $WINDOW == 1 ]]; then
  echo "== janela SDL3 (arm64): a imagem desenhada na janela no quadro 2300 (menu principal)"
  (cd $AM_LOG_DIR && $T 900 $B/runtime/automodellista --sys $SYS --frames 2300 --pad-script "$PAD" \
    --shot 2300:window_f2300.png --window 1 --max-seconds 800 > runtime_window.log 2>&1) \
    || { tail -5 $AM_LOG_DIR/runtime_window.log; exit 1; }
  python3 -I scripts/compare_shots.py $AM_LOG_DIR/window_f2300_janela.png $AM_LOG_DIR/runtime_arm64_f2300.png \
    --min-psnr 99
  echo "== tempo real (arm64): janela com pulo de quadros, sem som, até o menu principal"
  t0=$(date +%s)
  (cd $AM_LOG_DIR && $T 900 $B/runtime/automodellista --sys $SYS --frames 2400 --pad-script "$PAD" --window 1 \
    --realtime on --frameskip on --audio off --max-seconds 800 > runtime_realtime.log 2>&1) \
    || { tail -5 $AM_LOG_DIR/runtime_realtime.log; exit 1; }
  echo "2400 quadros em $(( $(date +%s) - t0 )) s (tempo do jogo: $(( 2400 * 1001 / 60000 )) s);" \
    "menu principal a $(fps_between $AM_LOG_DIR/runtime_realtime.log 2160 2400) quadros/s do jogo;" \
    "$(grep -o '[0-9]* quadro(s) pulado(s)' $AM_LOG_DIR/runtime_realtime.log)"
fi

echo "== golden (tests/golden/m7_menus.json)"
python3 -I scripts/check_m7_golden.py $AM_LOG_DIR --pad-script "$PAD" --frames $FRAMES \
  $( [[ $UPDATE == 1 ]] && echo --update )
echo "== OK"
