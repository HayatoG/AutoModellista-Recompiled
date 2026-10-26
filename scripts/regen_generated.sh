#!/bin/zsh
# M8 parte 2b: regenera o C do DOL e dos 12 RELs (bases a partir de 0x81800000, como scripts/regen_rels_m5.sh) num
# diretório NOVO, com o DolRecomp de tools/ (ou $DOLRECOMP) já com os patches/dolrecomp aplicados. A saída tem dol/ e
# rels/ e serve de AM_GENERATED_DIR.
#   scripts/regen_generated.sh <saída> [<main.dol>] [<diretório com os .rel>]
set -euo pipefail
ROOT=${0:A:h:h}
OUT=${1:?uso: regen_generated.sh <saída> [<main.dol>] [<rels>]}
DOL=${2:-/Volumes/AMRecomp/work/extracted/sys/main.dol}
RELS_IN=${3:-/Volumes/AMRecomp/recomp/rels_in}
DOLRECOMP=${DOLRECOMP:-$ROOT/tools/DolRecomp/build/dolrecomp}
mkdir -p $OUT
cd $OUT
rm -rf dol rels
# M9 parte 6 (patches/dolrecomp/0002): bl entre trechos chama a função do trecho direto, sem voltar ao chassi. Ficam de
# fora os alvos que a runtime intercepta por pc (hook_register): OSLink, OSLinkFixed, OSUnlink (am/game.h), o quadro
# (fn_80051034), main e __DVDInterruptHandler. Só no DOL: os RELs continuam como antes.
DOLRECOMP_C_DIRECT_CALLS=1 DOLRECOMP_C_NO_DIRECT_CALL=800C5C70,800C5C94,800C5F08,80051034,800719AC,800CD89C   $DOLRECOMP --gamecube -j8 $DOL dol > dol.log
$DOLRECOMP --gamecube -j8 --rel-base 0x81800000 $RELS_IN rels > rels.log
echo "dol: $(ls dol/generated/chunks | wc -l) trechos; rels: $(grep -cE 'module [0-9]+:' rels.log) módulos"
