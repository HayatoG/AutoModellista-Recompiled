#!/bin/zsh
# Atalho de desenvolvimento do M5: compila o build Release arm64 com os RELs regenerados e roda o jogo.
#   scripts/m5_run.sh [opções da runtime...]
set -euo pipefail
ROOT=${0:A:h:h}
B=${M5_BUILD:-/Volumes/AMRecomp/build-m5-arm64-release}
export AM_LOG_DIR=${AM_LOG_DIR:-/Volumes/AMRecomp/logs/m5}
mkdir -p $AM_LOG_DIR
cmake --build $B > $AM_LOG_DIR/build_dev.log 2>&1 || { grep -E "error|Error" $AM_LOG_DIR/build_dev.log | head -20; exit 1; }
cd $AM_LOG_DIR
$ROOT/scripts/with_timeout.sh ${M5_TIMEOUT:-300} $B/runtime/automodellista --sys /Volumes/AMRecomp/work/extracted/sys "$@"
