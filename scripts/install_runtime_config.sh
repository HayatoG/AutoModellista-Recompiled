#!/bin/zsh
# Instala a configuração de entrada da ModernGekko sem sobrescrever uma existente (faz backup).
set -euo pipefail
ROOT=${0:A:h:h}
DEST=${MODERNGEKKO_USER_DIR:-$HOME/.local/share/moderngekko}/Config
mkdir -p $DEST
if [[ -f $DEST/GCPadNew.ini ]] && ! cmp -s $ROOT/runtime/config/GCPadNew.ini $DEST/GCPadNew.ini; then
  cp $DEST/GCPadNew.ini $DEST/GCPadNew.ini.bak.$(date +%Y%m%d%H%M%S)
fi
cp $ROOT/runtime/config/GCPadNew.ini $DEST/GCPadNew.ini
echo "GCPadNew.ini -> $DEST"
