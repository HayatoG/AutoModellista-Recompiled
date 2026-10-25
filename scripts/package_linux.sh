#!/bin/sh
# Pacote portátil do Linux x86_64 (docs/M9_PARTE3A_LINUX.md): build Release com a SDL remendada como biblioteca
# compartilhada (AM_SDL3_SHARED=ON), executável com rpath $ORIGIN, libSDL3.so.0, rodar.sh e LEIAME.txt em $DIST, e
# um .tar.gz ao lado. Nenhum byte do jogo entra no pacote (só o executável recompilado).
#   scripts/package_linux.sh [--smoke]
#     --smoke  depois de empacotar, roda o executável do pacote: 300 quadros em software, sem janela, e 30 quadros
#              com --gfx gpu (SDL_VIDEO_DRIVER=offscreen), para ver que não quebra
# No macOS, roda a si mesmo na máquina Linux do OrbStack ($AM_ORB_MACHINE, padrão am-linux-x64), que vê
# /Volumes/AMRecomp no mesmo caminho. Variáveis: BUILD_ROOT, DIST, AM_GENERATED_DIR, AM_SYS_DIR, AM_LOG_DIR, CC.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$(uname -s)" = Darwin ]; then
  exec orb -m "${AM_ORB_MACHINE:-am-linux-x64}" env BUILD_ROOT="${BUILD_ROOT:-}" DIST="${DIST:-}" \
    AM_GENERATED_DIR="${AM_GENERATED_DIR:-}" AM_SYS_DIR="${AM_SYS_DIR:-}" AM_LOG_DIR="${AM_LOG_DIR:-}" \
    sh "$ROOT/scripts/package_linux.sh" "$@"
fi
[ "$(uname -s)-$(uname -m)" = Linux-x86_64 ] || { echo "só Linux x86_64 (ou macOS com o OrbStack)" >&2; exit 1; }
SMOKE=0
for a in "$@"; do
  case $a in --smoke) SMOKE=1 ;; *) echo "opção desconhecida: $a" >&2; exit 2 ;; esac
done
GEN=${AM_GENERATED_DIR:-/Volumes/AMRecomp/recomp/m5}
BUILD_ROOT=${BUILD_ROOT:-/Volumes/AMRecomp/wt-builds/linux-x64}
DIST=${DIST:-/Volumes/AMRecomp/dist/linux-x64}
SYS=${AM_SYS_DIR:-/Volumes/AMRecomp/work/extracted/sys}
LOG=${AM_LOG_DIR:-/Volumes/AMRecomp/logs/m9/p3a-linux}
B=$BUILD_ROOT/build-x86_64-release-pkg
T=$ROOT/scripts/with_timeout.sh
ls /Volumes/AMRecomp > /dev/null || { echo "volume /Volumes/AMRecomp ausente" >&2; exit 1; }
[ -d "$GEN/dol" ] && [ -d "$GEN/rels" ] || { echo "falta o C gerado em $GEN" >&2; exit 1; }
mkdir -p "$LOG"

echo "== build Release x86_64 com libSDL3.so ($B)"
CC=${CC:-clang} cmake -S "$ROOT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DAM_GENERATED_DIR="$GEN" \
  -DAM_SDL3_SHARED=ON -DAM_BUILD_TESTS=OFF > "$LOG/cmake_pkg.log" 2>&1 || { tail -20 "$LOG/cmake_pkg.log"; exit 1; }
cmake --build "$B" > "$LOG/build_pkg.log" 2>&1 || { grep -E "error|FAILED" "$LOG/build_pkg.log" | head -20; exit 1; }
if grep -E "warning:" "$LOG/build_pkg.log" | grep -v "/_deps/" | grep -q .; then
  echo "warnings fora da SDL:"; grep -E "warning:" "$LOG/build_pkg.log" | grep -v "/_deps/" | head; exit 1
fi

echo "== pacote ($DIST)"
rm -rf "$DIST"
mkdir -p "$DIST"
cmake --install "$B" --strip --prefix "$DIST" > "$LOG/install_pkg.log"
cp -L "$B/_deps/sdl3-build/libSDL3.so.0" "$DIST/libSDL3.so.0"
strip --strip-unneeded "$DIST/libSDL3.so.0"
chmod 644 "$DIST/libSDL3.so.0"
# Dependências diretas: só libc/libm (o resto a SDL abre com dlopen); a glibc mínima é o maior GLIBC_x.y pedido.
for f in automodellista libSDL3.so.0; do
  extra=$(readelf -d "$DIST/$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' \
    | grep -v -E '^(libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|libSDL3\.so\.0)$' || true)
  [ -z "$extra" ] || { echo "$f depende de: $extra" >&2; exit 1; }
done
readelf -d "$DIST/automodellista" | grep -q 'R.*PATH.*\[\$ORIGIN\]' || { echo "automodellista sem rpath \$ORIGIN" >&2; exit 1; }
GLIBC=$(objdump -T "$DIST/automodellista" "$DIST/libSDL3.so.0" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' \
  | sort -uV | tail -1)
SDLV=$(sed -n 's/^#define SDL_\(MAJOR\|MINOR\|MICRO\)_VERSION *\([0-9]*\).*/\2/p' \
  "$B/_deps/sdl3-src/include/SDL3/SDL_version.h" | paste -sd. -)
DOLSHA=$(sed -n 's/^\([0-9a-f]\{64\}\).*/\1/p' "$ROOT/analysis/iso/main_dol.sha256")
sed -e "s/@GLIBC@/$GLIBC/g" -e "s/@SDL_VERSION@/$SDLV/g" -e "s/@MAIN_DOL_SHA256@/$DOLSHA/g" \
  "$ROOT/packaging/linux/LEIAME.txt" > "$DIST/LEIAME.txt"
sed -e "s/@MAIN_DOL_SHA256@/$DOLSHA/g" "$ROOT/packaging/linux/rodar.sh" > "$DIST/rodar.sh"
chmod 755 "$DIST/rodar.sh" "$DIST/automodellista"
TGZ=$(dirname "$DIST")/automodellista-recompiled-$(basename "$DIST")-$(git -C "$ROOT" rev-parse --short HEAD).tar.gz
tar -C "$(dirname "$DIST")" -czf "$TGZ" "$(basename "$DIST")"
echo "  glibc mínima $GLIBC, SDL $SDLV; $(du -sh "$DIST" | cut -f1) em $DIST; $(du -h "$TGZ" | cut -f1) em $TGZ"

if [ $SMOKE = 1 ]; then
  [ -f "$SYS/main.dol" ] || { echo "falta $SYS/main.dol para o --smoke" >&2; exit 1; }
  echo "== prova: executável do pacote"
  (cd "$LOG" && "$T" 900 "$DIST/automodellista" --sys "$SYS" --frames 300 --gfx soft --audio off \
    --shot 300:pkg_soft_f300.png --max-seconds 800 > pkg_soft.log 2>&1) || { tail -5 "$LOG/pkg_soft.log"; exit 1; }
  echo "  --gfx soft: $(grep -c . "$LOG/pkg_soft.log") linhas de log, $(tail -1 "$LOG/pkg_soft.log")"
  (cd "$LOG" && SDL_VIDEO_DRIVER=offscreen "$T" 1800 "$DIST/automodellista" --sys "$SYS" --frames 30 --gfx gpu \
    --audio off --shot 30:pkg_gpu_f30.png --max-seconds 1700 > pkg_gpu.log 2>&1) || { tail -5 "$LOG/pkg_gpu.log"; exit 1; }
  grep -m1 -E 'GX: desenho na GPU|WARN.*--gfx gpu' "$LOG/pkg_gpu.log" | sed 's/^/  --gfx gpu: /'
  tail -1 "$LOG/pkg_gpu.log" | sed 's/^/  --gfx gpu: /'
fi
echo "== OK"
