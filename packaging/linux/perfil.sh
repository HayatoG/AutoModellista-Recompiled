#!/bin/sh
# Auto Modellista Recompiled, pacote de perfil do Linux x86_64 (executável com símbolos e frame pointers).
# Roda o roteiro fixo dos menus até a corrida em Suzuka (8400 quadros, sem janela, sem som, na velocidade que der)
# debaixo do perf e grava um relatório em texto para mandar de volta.
#   ./perfil.sh <pasta extraída do jogo> [gpu|soft]
# Precisa do perf (Fedora/Nobara: sudo dnf install perf). Se o perf recusar por permissão:
#   sudo sysctl kernel.perf_event_paranoid=1
set -eu
AQUI=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ $# -ge 1 ] || { echo "uso: $0 <pasta extraída do jogo> [gpu|soft]" >&2; exit 2; }
JOGO=$1; GFX=${2:-gpu}
if [ -f "$JOGO/sys/main.dol" ]; then SYS=$JOGO/sys
elif [ -f "$JOGO/main.dol" ]; then SYS=$JOGO
else echo "não achei sys/main.dol em $JOGO" >&2; exit 1; fi
command -v perf > /dev/null || { echo "falta o perf: sudo dnf install perf" >&2; exit 1; }
OUT=${OUT:-$HOME/automodellista_perfil}
mkdir -p "$OUT"
PAD="320:D_LEFT:8,360:A:8,1700:START:8,2000:START:8,2600:A:8,2900:A:8,3300:A:8,3600:A:8,3900:A:8,4200:A:8"
PAD="$PAD,4500:A:8,4800:A:8,5100:A:8,5400:A:8,5700:A:8,6900:A:1500,7700:D_RIGHT:120"
echo "== 1/3: medindo sem o perf (8400 quadros, sem janela)"
"$AQUI/automodellista" --sys "$SYS" --frames 8400 --pad-script "$PAD" --gfx "$GFX" --audio off --max-seconds 0 \
  > "$OUT/sem_perf.log" 2>&1
echo "== 2/3: perf record (mesmo roteiro)"
perf record -F 999 -g --call-graph fp -o "$OUT/perf.data" -- "$AQUI/automodellista" --sys "$SYS" --frames 8400 \
  --pad-script "$PAD" --gfx "$GFX" --audio off --max-seconds 0 > "$OUT/com_perf.log" 2>&1
echo "== 3/3: relatórios"
{
  echo "### sistema"; uname -a; lscpu | grep -E "Model name|^CPU\(s\)|Thread|MHz" || true
  echo; echo "### tempo por quadro (sem o perf)"; grep -E "quadro (60|1200|3000|5400|6600|7200|7800|8400):" "$OUT/sem_perf.log" || true
  echo; echo "### por thread"; perf report -i "$OUT/perf.data" --no-children --sort comm --stdio 2>/dev/null | grep -v "^#" | grep . | head -20
  echo; echo "### funções (todas as threads)"; perf report -i "$OUT/perf.data" --no-children --sort comm,symbol --stdio 2>/dev/null | grep -v "^#" | grep "%" | head -80
} > "$OUT/relatorio.txt"
tail -30 "$OUT/sem_perf.log" > "$OUT/fim_do_log.txt"
echo "pronto: mande $OUT/relatorio.txt e $OUT/fim_do_log.txt"
