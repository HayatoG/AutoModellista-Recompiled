#!/bin/zsh
# Abre a runtime numa janela SDL3 para assistir ao jogo (GX em software, sem som e sem controle: os botões vêm de um
# roteiro fixo, o mesmo dos verify_m5/m6).
#   scripts/assistir.sh [logo|corrida] [escala]
#     logo     abertura até o logo da Capcom (1500 quadros, ~30 s)
#     corrida  menus até a largada em Suzuka (6400 quadros, ~10 min com desenho na CPU)
# Fechar a janela encerra a execução.
set -euo pipefail
modo=${1:-logo}
escala=${2:-2}
BIN=${BUILD_ROOT:-/Volumes/AMRecomp}/build-arm64-release/runtime/automodellista
SYS=${AM_SYS_DIR:-/Volumes/AMRecomp/work/extracted/sys}
case $modo in
  logo) FRAMES=1500; PAD="320:D_LEFT:8,360:A:8" ;;
  corrida)
    FRAMES=6400
    PAD="320:D_LEFT:8,360:A:8,1700:START:8,2000:START:8,2600:A:8,2900:A:8,3300:A:8,3600:A:8,3900:A:8,4200:A:8"
    PAD="$PAD,4500:A:8,4800:A:8,5100:A:8,5400:A:8,5700:A:8" ;;
  *) echo "uso: scripts/assistir.sh [logo|corrida] [escala]"; exit 2 ;;
esac
[[ -x $BIN ]] || { echo "falta o build arm64 Release ($BIN): rode scripts/verify_m6.sh"; exit 1; }
mkdir -p /Volumes/AMRecomp/logs/assistir
cd /Volumes/AMRecomp/logs/assistir
exec $BIN --sys $SYS --frames $FRAMES --pad-script "$PAD" --window $escala --max-seconds 1200
