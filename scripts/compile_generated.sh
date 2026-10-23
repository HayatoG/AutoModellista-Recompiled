#!/bin/zsh
# Compila todo o C gerado pelo DolRecomp (só verifica que compila; o link é feito pela runtime).
#   scripts/compile_generated.sh <dir generated/> <dir de objetos> [flags extras do clang]
set -uo pipefail
ROOT=${0:A:h:h}
GEN=${1:?}; OBJ=${2:?}; shift 2
mkdir -p $OBJ
export ROOT OBJ EXTRA="$*"
find $GEN -name '*.c' -print0 | xargs -0 -n 1 -P ${JOBS:-$(sysctl -n hw.ncpu)} zsh -c '
  o=$OBJ/${1:t:r}.o
  clang -std=c11 -O1 -c -w -I$ROOT/tools/DolRecomp/src ${=EXTRA} $1 -o $o 2>$o.err && rm -f $o.err || echo "FALHA ${1:t}"' _
echo "objetos: $(find $OBJ -name '*.o' | wc -l | tr -d ' ')  falhas: $(find $OBJ -name '*.err' | wc -l | tr -d ' ')"
