#!/bin/zsh
# Fase 1: diagnóstico não destrutivo da ISO. O original (zip ou iso) só é lido, nunca escrito.
#
#   scripts/phase1_iso.sh "/Volumes/SSD/GameCube/Auto Modellista (USA)/Auto Modellista (USA).rvz" /Volumes/AMRecomp/work
#
# WORK recebe a ISO descompactada e a árvore extraída (~1,5 GB cada): use o SSD, o disco interno está apertado.
# Metadados (sem bytes do jogo) vão para analysis/iso/ e são versionáveis.
set -euo pipefail
ROOT=${0:A:h:h}
SRC=${1:?caminho da ISO ou do .zip}
WORK=${2:?pasta de trabalho no SSD}
DTK=${DTK:-$HOME/.local/share/decompnet/toolchains/dtk/dtk}
OUT=$ROOT/analysis/iso
mkdir -p $OUT $WORK $ROOT/logs
LOG=$ROOT/logs/phase1_iso.log
: > $LOG

echo "== origem: $SRC" | tee -a $LOG
shasum -a 256 "$SRC" | tee $OUT/source.sha256 | tee -a $LOG

ISO=$SRC
if [[ $SRC == *.zip ]]; then
  unzip -l "$SRC" | tee $OUT/zip_listing.txt | tee -a $LOG
  inner=$(unzip -Z1 "$SRC" | grep -iE '\.(iso|gcm|rvz|ciso|nkit\.iso)$' | head -1)
  ISO=$WORK/$inner
  [[ -f $ISO ]] || unzip -o -q "$SRC" "$inner" -d $WORK
fi
echo "== imagem: $ISO" | tee -a $LOG
shasum -a 256 "$ISO" | tee $OUT/image_file.sha256 | tee -a $LOG
# RVZ/CISO são comprimidos: os hashes da ISO (comparáveis ao Redump) vêm do dtk disc verify, não do arquivo.
$DTK disc info "$ISO" 2>&1 | tee $OUT/dtk_disc_info.txt | tee -a $LOG
$DTK disc verify "$ISO" 2>&1 | tee $OUT/dtk_disc_verify.txt | tee -a $LOG || true

EXT=$WORK/extracted
[[ -f $EXT/sys/main.dol ]] || $DTK disc extract "$ISO" $EXT 2>&1 | tee -a $LOG
python3 -I $ROOT/scripts/iso_census.py header $EXT/sys/boot.bin | tee $OUT/header.json | tee -a $LOG
DOL=$(find $EXT -name main.dol | head -1)
echo "== main.dol: $DOL" | tee -a $LOG
shasum -a 256 "$DOL" | tee $OUT/main_dol.sha256 | tee -a $LOG
$DTK dol info "$DOL" 2>&1 | tee $OUT/dtk_dol_info.txt | tee -a $LOG

python3 -I $ROOT/scripts/iso_census.py files $EXT > $OUT/files.json
python3 -I $ROOT/scripts/iso_census.py libs "$DOL" | tee $OUT/libs.json | tee -a $LOG
python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); print(json.dumps({k:v for k,v in d.items() if k!="files"}, indent=2, ensure_ascii=False))' $OUT/files.json | tee -a $LOG
echo "== ok: metadados em $OUT, árvore extraída em $EXT"
