#!/bin/zsh
# Compila o SDL_shadercross (HLSL -> DXIL / SPIR-V / MSL) com dependências vendored (DXC, SPIRV-Cross, SPIRV-Tools).
# Tudo em /Volumes/AMRecomp/ext: fonte em shadercross-src, build em shadercross-build, instalação em shadercross.
# O build do DXC é uma árvore LLVM (40-90 min); pode ser retomado rodando o script de novo.
# Variáveis: SC_COMMIT (padrão 1ff05be), SC_SEED (clone local para acelerar), JOBS.
set -euo pipefail
ROOT=${0:A:h:h}
EXT=/Volumes/AMRecomp/ext
SC_COMMIT=${SC_COMMIT:-1ff05be}
SC_SEED=${SC_SEED:-$EXT/SDL_shadercross}
SRC=$EXT/shadercross-src
BLD=$EXT/shadercross-build
INST=$EXT/shadercross
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
TETO=${TETO:-14400}   # 4 h por etapa
mkdir -p $ROOT/logs $EXT
ls /Volumes/AMRecomp >/dev/null || { echo "volume AMRecomp ausente" >&2; exit 1; }
export TMPDIR=$EXT/tmp; mkdir -p $TMPDIR

if [[ ! -d $SRC/.git ]]; then
  if [[ -d $SC_SEED/.git ]]; then git clone --quiet $SC_SEED $SRC; else git clone --quiet https://github.com/libsdl-org/SDL_shadercross.git $SRC; fi
fi
git -C $SRC checkout --quiet $SC_COMMIT
# Submódulos nos commits fixados pela árvore (--depth 1 busca o commit exato; DXC é grande).
git -C $SRC submodule sync --quiet
$ROOT/scripts/with_timeout.sh $TETO git -C $SRC submodule update --init --recursive --depth 1 \
  > $ROOT/logs/shadercross_submodules.log 2>&1
git -C $SRC submodule status --recursive | tee -a $ROOT/logs/shadercross_submodules.log | head -20

# A SDL3 não é submódulo: vem do pacote do Homebrew (SDL_DIR) só para o CLI; o shadercross usa poucas APIs dela.
SDL_PREFIX=${SDL_PREFIX:-$(brew --prefix sdl3)}
cmake -S $SRC -B $BLD -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_INSTALL_PREFIX=$INST \
  -DSDLSHADERCROSS_VENDORED=ON -DSDLSHADERCROSS_DXC=ON -DSDLSHADERCROSS_CLI=ON \
  -DSDLSHADERCROSS_INSTALL=ON -DSDLSHADERCROSS_STATIC=OFF -DSDLSHADERCROSS_SHARED=ON \
  -DCMAKE_PREFIX_PATH=$SDL_PREFIX -DCMAKE_INSTALL_RPATH='@loader_path/../lib' \
  > $ROOT/logs/shadercross_configure.log 2>&1
$ROOT/scripts/with_timeout.sh $TETO cmake --build $BLD -j $JOBS > $ROOT/logs/shadercross_build.log 2>&1
cmake --install $BLD > $ROOT/logs/shadercross_install.log 2>&1
ls -la $INST/bin $INST/lib | head -30
