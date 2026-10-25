#!/bin/sh
# Auto Modellista Recompiled, pacote do Linux x86_64 (docs/M9_PARTE3A_LINUX.md). Abre o jogo numa janela.
#   ./rodar.sh <pasta extraída do jogo> [--gfx gpu|soft] [--escala N] [outras opções da runtime...]
# A pasta é a raiz do "dtk disc extract" (com sys/ e files/) ou a própria sys/. Teclado como o padrão do Dolphin:
# X = A, Z = B, C = X, S = Y, D = Z, Return = START, setas = stick, TFGH = direcional, IJKL = stick C, Q/W = L/R.
set -eu
AQUI=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
uso() { echo "uso: $0 <pasta extraída do jogo> [--gfx gpu|soft] [--escala N] [opções da runtime...]" >&2; exit 2; }
[ $# -ge 1 ] || uso
JOGO=$1; shift
GFX=gpu
ESCALA=2
while [ $# -gt 0 ]; do
  case $1 in
    --gfx) [ $# -ge 2 ] || uso; GFX=$2; shift 2 ;;
    --escala) [ $# -ge 2 ] || uso; ESCALA=$2; shift 2 ;;
    *) break ;;
  esac
done
case $GFX in gpu|soft) ;; *) uso ;; esac
if [ -f "$JOGO/sys/main.dol" ]; then SYS=$JOGO/sys
elif [ -f "$JOGO/main.dol" ]; then SYS=$JOGO
else echo "não achei sys/main.dol em $JOGO (veja o LEIAME.txt)" >&2; exit 1; fi
ESPERADO=@MAIN_DOL_SHA256@
if command -v sha256sum > /dev/null; then
  h=$(sha256sum "$SYS/main.dol" | cut -d' ' -f1)
  [ "$h" = "$ESPERADO" ] || echo "aviso: main.dol com SHA-256 $h; o executável foi recompilado do GAUE08 rev 0 ($ESPERADO)" >&2
fi
# Sem --frames: roda até a janela fechar. Memory card em ~/.local/share/AutoModellista/MemoryCardA.raw.
exec "$AQUI/automodellista" --sys "$SYS" --frames 100000000 --window "$ESCALA" --gfx "$GFX" --max-seconds 0 "$@"
