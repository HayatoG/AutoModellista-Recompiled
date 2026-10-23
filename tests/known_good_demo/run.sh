#!/bin/zsh
# Teste de referência do DolRecomp com um DOL que podemos compartilhar: o "Demo Quest GC" do decomp.wtf
# (CC0 + zlib 1.1.4), compilado com CodeWarrior GC 1.3.2 e linkado no layout DOL padrão, como jogos reais.
# Passa se: (1) o DOL é recompilado sem instruções desconhecidas e (2) todo o C gerado compila com clang.
set -euo pipefail
HERE=${0:A:h}
ROOT=${HERE:h:h}
WTF=${DECOMP_WTF:-$HOME/Docs/Projetos-Pessoais/decomp.wtf}
REC=$ROOT/tools/DolRecomp/build/dolrecomp

if [[ ! -f $WTF/projects/demo-gc/build/main.dol ]]; then
  (cd $WTF && DECOMPNET_TOOLCHAINS=$HOME/.local/share/decompnet/toolchains uv run python projects/zlib-n64/prepare.py \
    && DECOMPNET_TOOLCHAINS=$HOME/.local/share/decompnet/toolchains uv run python projects/demo-gc/build.py)
fi
cp $WTF/projects/demo-gc/build/main.dol $HERE/demoq_gc.dol
rm -rf $HERE/out $HERE/obj && mkdir -p $HERE/obj

$REC --gamecube -j8 $HERE/demoq_gc.dol $HERE/out > $HERE/recomp.log 2>&1
grep -E "decoded|unknown" $HERE/recomp.log
if grep -E "[1-9][0-9]* unknown" $HERE/recomp.log; then echo "FALHA: instruções desconhecidas"; exit 1; fi

for f in $HERE/out/generated/generated.c $HERE/out/generated/chunks/*.c; do
  clang -std=c11 -O1 -c -w -I$ROOT/tools/DolRecomp/src $f -o $HERE/obj/${f:t:r}.o
done
echo "OK: $(ls $HERE/obj | wc -l | tr -d ' ') unidades compiladas para $(uname -m)"
