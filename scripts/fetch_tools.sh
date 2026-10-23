#!/bin/zsh
# Busca as ferramentas de terceiros em commits fixos (reprodutível). Clones rasos para poupar disco.
# Atualizar um pin = mudar o commit aqui e registrar o motivo em docs/ENVIRONMENT.md.
set -euo pipefail
ROOT=${0:A:h:h}
TOOLS=$ROOT/tools
mkdir -p $TOOLS

pins=(
  "DolRecomp            https://github.com/ExpansionPak/DolRecomp.git            02e27aca1c408cfcb2a4079a3a5b5ab3ae39d340"
  "ModernGekko          https://github.com/ExpansionPak/ModernGekko.git          a2e4e2b026a68d4848f20ebf16f0e0ddeb573957"
  "ModernGekko-Template https://github.com/ExpansionPak/ModernGekko-Template.git c2553a99584b2045dbe46a70e1570994dae8beef"
  "ModernGekkate        https://github.com/snazzmaster/ModernGekkate.git         cbb2fc624784f27f5a9cbf6af10df1cffd1ea7d4"
  "Wiicompiled          https://github.com/patchzyy/Wiicompiled.git              4c166a7eb1ddfc79b4c832e422e6bb00ec06fb4f"
)

for line in $pins; do
  read -r name url sha <<< "$line"
  dir=$TOOLS/$name
  if [[ ! -d $dir/.git ]]; then
    git init -q $dir
    git -C $dir remote add origin $url
  fi
  git -C $dir fetch -q --depth 1 origin $sha
  git -C $dir checkout -q --detach $sha
  echo "$name @ $(git -C $dir rev-parse --short HEAD)"
done

# DolRecomp só precisa do tomlc17; os submódulos da ModernGekko (RecompCore/Dolphin, centenas de MB)
# são buscados por scripts/build_moderngekko.sh, que checa espaço em disco antes.
git -C $TOOLS/DolRecomp submodule update -q --init --recursive --depth 1
