#!/bin/zsh
# Abre a runtime numa janela SDL3 (Release arm64). O GX desenha na GPU (M8, docs/M8_CORRIDA.md §3); AM_GFX=soft volta
# ao rasterizador em software.
#   scripts/assistir.sh [jogar|logo|corrida] [escala]
#     jogar    (padrão) modo interativo: controle pelo teclado ou gamepad, som, tempo real e pulo de quadros
#              quando não dá para desenhar todos (mapeamento em docs/M7_MENUS.md §3; teclado como o padrão
#              do Dolphin: X = A, Z = B, C = X, S = Y, D = Z, Return = START, setas = stick, TFGH = direcional,
#              IJKL = stick C, Q/W = L/R)
#     logo     roteiro fixo: abertura até o logo da Capcom (1500 quadros), sem som, na velocidade que der
#     corrida  roteiro fixo: menus até a largada em Suzuka (6400 quadros), sem som, na velocidade que der
# Fechar a janela encerra a execução. Log em /Volumes/AMRecomp/logs/assistir/.
set -euo pipefail
modo=${1:-jogar}
escala=${2:-2}
GFX=${AM_GFX:-gpu}
BIN=${BUILD_ROOT:-/Volumes/AMRecomp}/build-arm64-release/runtime/automodellista
SYS=${AM_SYS_DIR:-/Volumes/AMRecomp/work/extracted/sys}
[[ -x $BIN ]] || { echo "falta o build arm64 Release ($BIN): rode scripts/verify_m7.sh"; exit 1; }
mkdir -p /Volumes/AMRecomp/logs/assistir
cd /Volumes/AMRecomp/logs/assistir
case $modo in
  jogar)
    # Sem --frames: roda até a janela fechar (o watchdog de tempo real fica desligado).
    exec $BIN --sys $SYS --frames 100000000 --window $escala --gfx $GFX --max-seconds 0 ;;
  logo) FRAMES=1500; PAD="320:D_LEFT:8,360:A:8" ;;
  corrida)
    FRAMES=6400
    PAD="320:D_LEFT:8,360:A:8,1700:START:8,2000:START:8,2600:A:8,2900:A:8,3300:A:8,3600:A:8,3900:A:8,4200:A:8"
    PAD="$PAD,4500:A:8,4800:A:8,5100:A:8,5400:A:8,5700:A:8" ;;
  *) echo "uso: scripts/assistir.sh [jogar|logo|corrida] [escala]"; exit 2 ;;
esac
exec $BIN --sys $SYS --frames $FRAMES --pad-script "$PAD" --window $escala --gfx $GFX --max-seconds 1200
