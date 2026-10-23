#!/bin/zsh
# M4: regenera o C do DOL com trechos de 128 instruções (em vez de 4096) num diretório NOVO, para o teste mais forte
# de comparação de traces (docs/M4_BOOT.md §5.3): com trechos menores quase todo bl cruza trecho e volta ao chassi.
# Não toca no gerado padrão (/Volumes/AMRecomp/recomp/dol). Os RELs são reaproveitados por link simbólico.
#   scripts/regen_c128.sh [<main.dol>] [<saída>]
set -euo pipefail
ROOT=${0:A:h:h}
DOL=${1:-/Volumes/AMRecomp/work/extracted/sys/main.dol}
OUT=${2:-/Volumes/AMRecomp/recomp/c128}
mkdir -p $OUT
cd $OUT
rm -rf dol
DOLRECOMP_C_CHUNK_INSTRUCTIONS=128 $ROOT/tools/DolRecomp/build/dolrecomp --gamecube -j8 $DOL dol
ln -sfn /Volumes/AMRecomp/recomp/rels rels
ls dol/generated/chunks | wc -l
