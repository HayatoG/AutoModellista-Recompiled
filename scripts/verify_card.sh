#!/bin/zsh
# Verificação do memory card (docs/M8_PARTE2B_CARD.md): o jogo salva num cartão novo e, numa segunda execução, lê.
#   scripts/verify_card.sh [--oracle] [--update-golden]
#     --oracle         roda o mesmo roteiro no Dolphin (com lockf) e compara o .raw que ele grava com o da runtime
#     --update-golden  regrava tests/golden/m8_card.json
# Execução 1 (cartão novo e formatado pela runtime): cria o System File no boot (400:A), vai ao menu principal,
# save/load > save > Memory Card Slot A > sobrescrever "yes" e espera "The file has been saved". Execução 2 (com o
# cartão da 1): save/load > load > Slot A > "yes" e espera "The file ... has been loaded". Nos dois builds (arm64 e
# x86_64, se existirem em $BUILD_ROOT) o cartão e as capturas têm de ser idênticos.
set -euo pipefail
ROOT=${0:A:h:h}
GEN=${AM_GENERATED_DIR:-/Volumes/AMRecomp/recomp/m5}
BUILD_ROOT=${BUILD_ROOT:-/Volumes/AMRecomp}
export AM_LOG_DIR=${AM_LOG_DIR:-/Volumes/AMRecomp/logs/m8/p2b-card/card_verify}
SYS=${AM_SYS_DIR:-/Volumes/AMRecomp/work/extracted/sys}
ISO=${AM_ISO:-/Volumes/AMRecomp/work/iso/Auto Modellista (USA).rvz}
T=$ROOT/scripts/with_timeout.sh
SAVE="400:A:8,1700:START:8,2000:START:8,2600:D_DOWN:8,2660:D_DOWN:8,2720:D_DOWN:8,2800:A:8,3000:A:8,3200:A:8"
SAVE="$SAVE,3400:D_LEFT:8,3450:A:8"
LOAD="1700:START:8,2000:START:8,2600:D_DOWN:8,2660:D_DOWN:8,2720:D_DOWN:8,2800:A:8,3000:D_DOWN:8,3060:A:8"
LOAD="$LOAD,3260:A:8,3500:A:8"
FRAMES=4300
ORACLE=0; UPDATE=0
for a in "$@"; do
  case $a in
    --oracle) ORACLE=1 ;; --update-golden) UPDATE=1 ;;
    *) echo "opção desconhecida: $a"; exit 2 ;;
  esac
done
ls /Volumes/AMRecomp > /dev/null || { echo "volume /Volumes/AMRecomp ausente"; exit 1; }
mkdir -p $AM_LOG_DIR
cd $ROOT
rm -f $AM_LOG_DIR/card_*.raw(N) $AM_LOG_DIR/*_card*.png(N) $AM_LOG_DIR/card_*.log(N)
for arch in arm64 x86_64; do
  B=$BUILD_ROOT/build-$arch-release
  echo "== $arch Release ($B)"
  cmake -S . -B $B -G Ninja -UAM_SDL3_TAG -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=$arch \
    -DAM_GENERATED_DIR=$GEN > $AM_LOG_DIR/cmake_card_$arch.log
  cmake --build $B > $AM_LOG_DIR/build_card_$arch.log
  run() { # $1 = nome, $2 = roteiro, $3 = captura (quadro:arquivo), $4 = cartão
    (cd $AM_LOG_DIR && $T 900 $B/runtime/automodellista --sys $SYS --frames $FRAMES --pad-script "$2" --gfx soft \
      --memcard $4 --shot $3 --max-seconds 850 > card_$1_$arch.log 2>&1) || { tail -5 $AM_LOG_DIR/card_$1_$arch.log; exit 1; }
  }
  t0=$(date +%s)
  run save "$SAVE" 3800:save_card_$arch.png card_save_$arch.raw
  cp $AM_LOG_DIR/card_save_$arch.raw $AM_LOG_DIR/card_load_$arch.raw
  run load "$LOAD" 3700:load_card_$arch.png card_load_$arch.raw
  echo "  salvar e ler: $(( $(date +%s) - t0 )) s"
  grep "memory card:" $AM_LOG_DIR/card_save_$arch.log | sed 's/^\[INFO\] /  salvar: /'
  grep "memory card:" $AM_LOG_DIR/card_load_$arch.log | sed 's/^\[INFO\] /  ler:    /'
done

if [[ $ORACLE == 1 ]]; then
  echo "== oráculo: Dolphin 2609a com o mesmo roteiro e o mesmo cartão inicial"
  # Cartão inicial = o que a runtime cria (4 Mbit, formatado com RTC 0): um .raw já existente é lido como está.
  rm -rf $AM_LOG_DIR/dolphin_card; mkdir -p $AM_LOG_DIR/dolphin_card
  (cd $AM_LOG_DIR && $T 60 $BUILD_ROOT/build-arm64-release/runtime/automodellista --sys $SYS --frames 5 --gfx off \
    --memcard dolphin_card/card.USA.raw > card_blank.log 2>&1)
  cp $AM_LOG_DIR/dolphin_card/card.USA.raw $AM_LOG_DIR/card_blank.raw
  lockf /Volumes/AMRecomp/wt-builds/dolphin.lock $T 1700 python3 -I scripts/dolphin_trace.py --iso "$ISO" --video Null \
    --memcard $AM_LOG_DIR/dolphin_card/card.USA.raw --dolphin-log $AM_LOG_DIR/dolphin_card.log events \
    --pad-script "$SAVE" --frames $FRAMES --timeout 1600 --out $AM_LOG_DIR/dolphin_card_events.txt | tail -1
fi

echo "== golden (tests/golden/m8_card.json)"
python3 -I scripts/check_card.py $AM_LOG_DIR $( [[ $ORACLE == 1 ]] && echo --dolphin $AM_LOG_DIR/dolphin_card/card.USA.raw ) \
  $( [[ $UPDATE == 1 ]] && echo --update )
echo "== OK"
