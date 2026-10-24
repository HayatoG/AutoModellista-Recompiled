#!/bin/sh
# Monta as imagens do projeto que estiverem faltando (AMRecomp e DecompAM, no SSD USB) e diz o que achou.
# Sai com 0 se todas estão montadas, 1 se o SSD não está ligado, 2 se alguma imagem não montou.
#
# Fica instalado fora do volume (scripts/instalar_montagem.sh copia para ~/.local/bin): se o volume cair, a cópia
# do repo some junto. O mesmo script roda a cada minuto pelo launchd para remontar sozinho quando o SSD volta.
ssd=/Volumes/SSD
status=0
if [ ! -d "$ssd" ]; then
  echo "SSD ausente ($ssd): religue o cabo USB; as imagens montam quando ele voltar"
  exit 1
fi
for name in AMRecomp DecompAM; do
  if [ -d "/Volumes/$name" ] && ls "/Volumes/$name" >/dev/null 2>&1; then
    # No launchd (saída para o log, não terminal) só as mudanças são escritas: o log não cresce a cada minuto.
    [ -t 1 ] && echo "$name: montado"
    continue
  fi
  if [ ! -d "$ssd/$name.sparsebundle" ]; then
    echo "$name: imagem $ssd/$name.sparsebundle não encontrada"
    status=2
    continue
  fi
  # Quando o USB cai, a imagem pode continuar presa a dispositivos antigos (/dev/diskN sem volume montado) e o attach
  # novo falha com "sistemas de arquivos não montáveis": solta esses dispositivos antes (aconteceu em 24/10, 04:10).
  for dev in $(hdiutil info | awk -v img="$ssd/$name.sparsebundle" \
      '$1 == "image-path" { f = ($3 == img) } f && /^\/dev\/disk[0-9]+[[:space:]]/ { print $1; f = 0 }'); do
    hdiutil detach -force "$dev" >/dev/null 2>&1 && echo "$name: soltei $dev, preso de uma queda anterior"
  done
  # -nobrowse não: o Finder deve mostrar o volume. -noverify pula a verificação longa da imagem.
  if err=$(hdiutil attach -noverify "$ssd/$name.sparsebundle" 2>&1 >/dev/null); then
    echo "$name: montado agora ($(date '+%d/%m %H:%M:%S'))"
  else
    echo "$name: falhou ao montar $ssd/$name.sparsebundle: $(echo "$err" | grep -v WARNING | tail -1)"
    status=2
  fi
done
exit $status
