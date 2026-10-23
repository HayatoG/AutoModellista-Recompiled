#!/usr/bin/env bash
# Procura símbolos globais duplicados entre as saídas SEPARADAS do DolRecomp (DOL e RELs) e lista o que o C gerado
# espera da runtime (símbolos indefinidos). Base da decisão registrada em docs/M3_ESQUELETO.md §3.
#   scripts/check_generated_symbols.sh <dir de .o do DOL> <dir de .o dos RELs>
# Os .o vêm de scripts/compile_generated.sh. Sai com 1 se houver duplicata.
set -euo pipefail
DOL=${1:?dir de objetos do DOL}
REL=${2:?dir de objetos dos RELs}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

defined() { find "$1" -name '*.o' -print0 | xargs -0 nm -g --defined-only 2>/dev/null | awk 'NF==3 {print $3}' | sort; }
undefined() { find "$@" -name '*.o' -print0 | xargs -0 nm -u 2>/dev/null | awk 'NF>0 && $NF !~ /:$/ {print $NF}' | sort -u; }

defined "$DOL" > "$TMP/dol.txt"
defined "$REL" > "$TMP/rel.txt"
echo "símbolos globais definidos: DOL=$(wc -l < "$TMP/dol.txt" | tr -d ' ') RELs=$(wc -l < "$TMP/rel.txt" | tr -d ' ')"

dups=$(sort "$TMP/dol.txt" "$TMP/rel.txt" | uniq -d)
if [[ -n $dups ]]; then
  echo "DUPLICADOS:"; echo "$dups"; exit 1
fi
echo "duplicados entre DOL e RELs (e dentro de cada um): 0"

echo "indefinidos que a runtime precisa fornecer:"
undefined "$DOL" "$REL" | sed 's/^_//' | grep -E '^(mem_|ppc_|dolrecomp_|cpu_)' | sed 's/^/  /'
