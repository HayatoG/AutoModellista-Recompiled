#!/usr/bin/env bash
# Busca só o DolRecomp (cpu/cpu.h e cpu/cpu.c, que a runtime usa), no commit fixo de scripts/fetch_tools.sh.
# Sem submódulos e sem build: serve ao CI, onde o C gerado NÃO existe (AM_WITH_GENERATED=OFF).
#   scripts/ci_fetch_dolrecomp.sh [destino]     (padrão: tools/DolRecomp)
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST=${1:-$ROOT/tools/DolRecomp}

# A linha do pin é:   "DolRecomp   <url>   <sha>"
read -r _ url sha < <(grep -E '^[[:space:]]+"DolRecomp[[:space:]]' "$ROOT/scripts/fetch_tools.sh" | tr -d '"')
[[ -n ${url:-} && -n ${sha:-} ]] || { echo "pin do DolRecomp não encontrado em scripts/fetch_tools.sh" >&2; exit 1; }

mkdir -p "$DEST"
if [[ ! -d $DEST/.git ]]; then
  git init -q "$DEST"
  git -C "$DEST" remote add origin "$url"
fi
git -C "$DEST" fetch -q --depth 1 origin "$sha"
git -C "$DEST" checkout -q --detach "$sha"
echo "DolRecomp @ $(git -C "$DEST" rev-parse --short HEAD) em $DEST"
