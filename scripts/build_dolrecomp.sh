#!/bin/zsh
# Compila o DolRecomp (backend C) e roda a suíte de testes. Logs em logs/.
set -euo pipefail
ROOT=${0:A:h:h}
D=$ROOT/tools/DolRecomp
mkdir -p $ROOT/logs
cmake -S $D -B $D/build -G Ninja -DCMAKE_BUILD_TYPE=Release > $ROOT/logs/dolrecomp_configure.log 2>&1
cmake --build $D/build > $ROOT/logs/dolrecomp_build.log 2>&1
ctest --test-dir $D/build -C Release --output-on-failure > $ROOT/logs/dolrecomp_ctest.log 2>&1
tail -3 $ROOT/logs/dolrecomp_ctest.log
