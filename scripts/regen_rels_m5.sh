#!/bin/zsh
# M5: regenera o C dos 12 RELs com bases virtuais FORA da MEM1 (a partir de 0x81800000) num diretório NOVO.
#
# Por quê (docs/M5_OS_DISCO.md §3): o DolRecomp aplica as relocações dos RELs em tempo de recompilação, então todo
# lis/addi que aponta para dados do próprio módulo vira imediato com a base virtual. Com a base padrão (0x80500000) esses
# endereços caem no meio da arena da MEM1, onde o jogo põe outras coisas; não dá para redirecioná-los sem ambiguidade.
# Com bases a partir de 0x81800000 (logo depois dos 24 MiB) o endereço virtual é físico 0x018xxxxx, que não existe no GameCube, e a
# runtime o resolve para o endereço real em que o OSLink pôs o módulo (core/rel_map.c).
# A base não pode ser maior: o bl (REL24) de um REL para o DOL alcança só ±32 MiB, e o último módulo termina perto
# de 0x81B8xxxx. O DOL não muda: dol/ é link simbólico para o gerado padrão.
#   scripts/regen_rels_m5.sh [<diretório com os .rel>] [<saída>]
set -euo pipefail
ROOT=${0:A:h:h}
RELS_IN=${1:-/Volumes/AMRecomp/recomp/rels_in}
OUT=${2:-/Volumes/AMRecomp/recomp/m5}
REL_BASE=0x81800000
mkdir -p $OUT
cd $OUT
rm -rf rels
$ROOT/tools/DolRecomp/build/dolrecomp --gamecube -j8 --rel-base $REL_BASE $RELS_IN rels > rels.log
ln -sfn /Volumes/AMRecomp/recomp/dol dol
grep -E "module [0-9]+:" rels.log
