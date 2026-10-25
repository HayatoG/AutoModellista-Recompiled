#!/bin/sh
# Auto Modellista Recompiled, pacote do Linux x86_64 (docs/M9_PARTE3A_LINUX.md, docs/M9_PARTE5_LAUNCHER.md). Abre a
# tela inicial; JOGAR começa o jogo na mesma janela.
#   ./rodar.sh [pasta extraída do jogo] [--gfx gpu|soft] [--escala N] [outras opções da runtime...]
# Sem a pasta, a tela inicial pede (e lembra) a pasta do jogo. A pasta é a raiz do "dtk disc extract" (com sys/ e
# files/) ou a própria sys/. --gfx e --escala valem só nesta execução (o padrão fica nas Configurações da tela
# inicial). --no-launcher pula a tela inicial.
set -eu
AQUI=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
uso() { echo "uso: $0 [pasta extraída do jogo] [--gfx gpu|soft] [--escala N] [opções da runtime...]" >&2; exit 2; }
SYS=
case ${1:-} in
  ""|-*) ;;
  *)
    JOGO=$1; shift
    if [ -f "$JOGO/sys/main.dol" ]; then SYS=$JOGO/sys
    elif [ -f "$JOGO/main.dol" ]; then SYS=$JOGO
    else echo "não achei sys/main.dol em $JOGO (veja o LEIAME.txt)" >&2; exit 1; fi
    ESPERADO=@MAIN_DOL_SHA256@
    if command -v sha256sum > /dev/null; then
      h=$(sha256sum "$SYS/main.dol" | cut -d' ' -f1)
      [ "$h" = "$ESPERADO" ] || echo "aviso: main.dol com SHA-256 $h; o executável foi recompilado do GAUE08 rev 0 ($ESPERADO)" >&2
    fi ;;
esac
GFX=
ESCALA=
while [ $# -gt 0 ]; do
  case $1 in
    --gfx) [ $# -ge 2 ] || uso; GFX=$2; shift 2 ;;
    --escala) [ $# -ge 2 ] || uso; ESCALA=$2; shift 2 ;;
    *) break ;;
  esac
done
case $GFX in ""|gpu|soft) ;; *) uso ;; esac
# Com a pasta, --interactive liga o modo interativo (sem ela, a tela inicial já é o padrão). Roda até a janela
# fechar. Configurações em ~/.local/share/AutoModellista/settings.ini; memory card no mesmo diretório.
if [ -n "$SYS" ]; then set -- --sys "$SYS" --interactive "$@"; fi
if [ -n "$ESCALA" ]; then set -- --window "$ESCALA" "$@"; fi
if [ -n "$GFX" ]; then set -- --gfx "$GFX" "$@"; fi
exec "$AQUI/automodellista" "$@"
