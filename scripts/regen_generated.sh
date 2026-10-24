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
$DOLRECOMP --gamecube -j8 $DOL dol > dol.log
$DOLRECOMP --gamecube -j8 --rel-base 0x81800000 $RELS_IN rels > rels.log
echo "dol: $(ls dol/generated/chunks | wc -l) trechos; rels: $(grep -cE 'module [0-9]+:' rels.log) módulos"
