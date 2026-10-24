# M6: Primeiro quadro

Estado: **atingido localmente** (macOS arm64 e x86_64 via Rosetta, Release). A runtime desenha o que o jogo manda pelo
GX: o leitor da FIFO entrega os comandos ao vertex loader, ao XF e ao BP; os desenhos passam por montagem de
primitivas, recorte, rasterização, TEV e EFB, e a cópia EFB -> XFB grava o quadro em YUV na memória do guest, como o
hardware. O VI mostra esse XFB numa janela SDL3. O logo da Capcom aparece no mesmo quadro que no Dolphin e a captura
bate com a do Dolphin a 46,4 dB (logo inteiro) e 49,2 dB (no meio do fade), sem nenhum pixel com diferença visível.

Critério de saída do plano (`PLANO_PORT_C.md` §4): "logo da Capcom visível; screenshot comparado ao Dolphin (§6)".
Resultado: cumprido, com a comparação de §5 nos dois builds e o golden em `tests/golden/m6_frame.json`.

## 1. Resumo do resultado

```
$ scripts/verify_m6.sh --oracle
== oráculo: Dolphin (Metal, XFB na RAM) com o roteiro, capturas nos quadros 1100,1250
40 eventos, 1260 quadros em 30.2s; fim: frames
== arm64 Release (/Volumes/AMRecomp/build-arm64-release)              (idem x86_64 Release, Rosetta: 56 s)
100% tests passed out of 44
parada em 0x80051034: M5: 1260 quadros completos do laço fn_80051034 em 29 s
  GX render ligado: 77764 desenhos rasterizados, 156142 triângulos (...), 356636376 pixels no TEV
  EFB: 2248 cópias para o XFB (última 0x0047A780, 640x448), 0 para textura (formatos 0x0000), 2248 limpezas
runtime_arm64_f1100.png x dolphin_f1100.png: PSNR 49.18 dB, erro médio 0.213, máximo 13, pixels com diferença > 24: 0.0% -> OK
runtime_arm64_f1250.png x dolphin_f1250.png: PSNR 46.37 dB, erro médio 0.213, máximo 25, pixels com diferença > 24: 0.0% -> OK
== janela SDL3 (arm64): a imagem desenhada na janela no quadro 1250
window_f1250_janela.png x runtime_arm64_f1250.png: PSNR 99.0 dB (idênticas) -> OK
== golden (tests/golden/m6_frame.json)
igual ao golden (2 build(s)): q1100 49.18 dB, q1250 46.37 dB
```

Os dois builds dão os mesmos pixels (o golden guarda o SHA-256 do RGB de cada captura). Com o roteiro do M5 (até a
largada), a runtime com o GX ligado produz os mesmos 551 eventos do Dolphin (103 estritos idênticos, módulos nos
mesmos quadros): desenhar não muda o tempo emulado.

## 2. O que foi feito

```
runtime/gfx/gx_fifo.c     leitor do fluxo: LOAD_CP/XF/BP, LOAD_INDX_A..D, display lists, desenhos vértice a vértice
runtime/gfx/gx_vtx.c      vertex loader: VCD/VAT, arrays (base/passo), frações, cores 565/888/4444/6666/8888, NBT
runtime/gfx/gx_xf.c       XF: matrizes, projeção, viewport, iluminação dos dois canais, texgen (regular, emboss, cor)
runtime/gfx/gx_bp.c       registradores de BP com a máscara 0xFE, cores/konst do TEV, TMEM (TLUT e preload)
runtime/gfx/gx_tex.c      decodificadores I4 I8 IA4 IA8 RGB565 RGB5A3 RGBA8 C4 C8 C14X2 CMPR; amostrador (wrap, bilinear, mip)
runtime/gfx/gx_setup.c    montagem de quads/triângulos/faixas/leques/linhas/pontos, recorte, descarte pela face
runtime/gfx/gx_raster.c   rasterizador 28.4 em blocos de 2x2, scissor com deslocamento, LOD, interpolação perspectiva
runtime/gfx/gx_tev.c      TEV (16 estágios, compare, konst, swap), indireto, teste de alfa, textura de Z, neblina, Z tardio
runtime/gfx/gx_efb.c      EFB 640x528 (RGB8/RGBA6/RGB565, Z24): Z, blend, lógica, dither; cópias para XFB e textura; limpeza
runtime/platform/platform.c  janela SDL3 (SDL_Renderer) e PNG (SDL_SavePNG)
runtime/game/game_run.c   --shot (XFB do quadro em PNG), --window (o XFB que o VI mostra), --gfx soft|off
scripts/compare_shots.py  PSNR / erro / pixels ruins / pixels acesos entre duas capturas; lado a lado e diferença
scripts/check_m6_golden.py, scripts/verify_m6.sh, tests/golden/m6_frame.json
```

O GX é uma tradução para C do backend Software do Dolphin 2609a (`VideoBackends/Software`: SetupUnit, Clipper,
Rasterizer, Tev, TextureSampler, SWEfbInterface, TransformUnit, SWVertexLoader) e dos decodificadores de
`VideoCommon/TextureDecoder_Common.cpp`, com atribuição no cabeçalho de cada arquivo. A exceção é a cópia EFB -> XFB,
que segue o shader de codificação do caminho de hardware (`TextureConversionShader.cpp`: filtro vertical em três
linhas, gama, Y/U/V com as constantes 66/129/25, -38/-74/112, 112/-94/-18 e arredondamento do shader), porque é ele que
o oráculo executa. Os arquivos do GX compilam com `-ffp-contract=off`: sem isso o clang funde `a*b+c` em FMA no arm64 e
não no x86_64, e as capturas dos dois builds deixariam de ser iguais bit a bit.

Testes: `ctest` roda 44 testes; os novos (gx_vtx, gx_xf, gx_tex, gx_efb, gx_render) usam blocos de textura e cenas
montados à mão: quad e faixa com cobertura de centros de pixel, descarte e recorte, textura I8 modulada pelo TEV com
teste de alfa, blend por alfa, máscara de BP, carga de TLUT, cópia para textura RGB565 em blocos e cópia para o XFB
com limpeza (branco = Y 235, preto = Y 16). Nenhum byte do jogo.

## 3. Onde está o logo

`dolphin_trace.py events --video Metal --shots` gravou o XFB de 25 quadros (850 a 1450, de 25 em 25) com o roteiro
mínimo do M5 (`320:D_LEFT:8,360:A:8`, só o "No Memory Card"). A abertura, sem nenhum botão: aviso de trânsito até
~q850, "Licensed by Nintendo" de ~q875 a ~q1050, logo da Capcom em fade a partir de ~q1076, inteiro de ~q1110 a
~q1410, tela preta em q1425. O critério usa **q1250** (logo inteiro, longe das transições: um quadro de diferença não
muda a imagem) e **q1100** (meio do fade: testa o TEV modulando a cor e a sincronia de quadros, porque um quadro a
mais ou a menos muda o brilho).

A runtime grava a captura do mesmo jeito que o oráculo: no início do quadro N (entrada de fn_80051034 vinda de
0x8004839C), lê o ponteiro que o jogo passou ao `VISetNextFrameBuffer` (variável 0x8026E648) e converte 640x448 YUYV
para RGB com a mesma fórmula BT.601 truncada de `write_xfb_png`. Nos 25 quadros os dois lados mostram a mesma tela e
o mesmo ponto do fade (os eventos do M5 já caíam nos mesmos quadros).

## 4. Decisões e desvios do plano

- **Rasterizador em software** em vez do ubershader TEV em HLSL com SDL_GPU/SDL_shadercross previsto em §2.1. Para o
  M6 o que precisava ser provado é que o fluxo do jogo é entendido por inteiro (comandos, estado, formatos, TEV,
  EFB, cópia para o XFB) e comparável ao oráculo pixel a pixel. Em software isso é determinístico, igual nas duas
  arquiteturas, testável sem GPU e permite gravar o XFB na RAM exatamente como o hardware. O ubershader fica para
  quando o desempenho importar (M8); o rasterizador em software continua como referência para os testes.
- **SDL_Renderer só para apresentar**: a janela recebe o XFB já convertido em RGBA numa textura de streaming
  (letterbox, sem filtro). Não há código de GPU próprio no M6.
- **A janela mostra o XFB que o VI está varrendo** (`vi_xfb_top`), não o último `VISetNextFrameBuffer`; a captura
  usa o segundo, como o oráculo. No logo os dois são a mesma imagem (o verify confere: 99 dB).
- **Renderização ligada por padrão** (`--gfx soft`); `--gfx off` volta ao comportamento do M5 (só contar comandos),
  que o `verify_m5.sh` passou a usar para não gastar ~10 min por arquitetura (§7).

## 5. Comparação com o Dolphin

O oráculo usa o backend Metal com `XFBToTextureEnable=False` (o XFB vai para a RAM e o GDB lê); o Dolphin desenha na
GPU, em ponto flutuante, com filtro bilinear do hardware. Igualdade de bytes não é esperada. `compare_shots.py` mede
sobre RGB:

- **PSNR** (critério: >= 30 dB);
- **pixels ruins**: algum canal com diferença > 24, o que já é visível (critério: <= 1%);
- **pixels acesos** (luminância > 32) dos dois lados (critério: >= 1% em cada um), para que duas telas pretas não
  passem por iguais.

Resultados (arm64 = x86_64):

| Quadro | Tela | PSNR | Erro médio | Máx. | Pixels > 24 | Acesos runtime / Dolphin |
| --- | --- | --- | --- | --- | --- | --- |
| 1100 | logo em fade | 49,18 dB | 0,213 | 13 | 0% | 4,35% / 4,34% |
| 1250 | logo inteiro | 46,37 dB | 0,213 | 25 | 0% | 4,69% / 4,67% |

As diferenças ficam só no contorno das letras (a imagem de diferença x4 mostra o desenho do logo e nada mais): a
borda filtrada pelo bilinear em ponto fixo s17.7 contra o da GPU.

Fora do critério, na varredura de 25 quadros: o aviso de trânsito e o "Licensed by Nintendo" ficam entre 34 e 60 dB;
no "Licensed by Nintendo" 0,31% dos pixels passam de 24 (o miolo de algumas letras, onde a amostragem bilinear da
fonte cai noutro ponto do texel; pendente em §8). Com o roteiro do M5, os menus (q1800 a q6000) dão 31 a 41 dB com
<= 0,5% de pixels ruins, e a diferença visível é um texto em animação de máquina de escrever um quadro adiantado. Na
largada (q6400) a pista de Suzuka aparece com o visual de cel-shading nos dois lados, mas a câmera da introdução está
em posição um pouco diferente (20,9 dB): é tempo de jogo, não desenho (ver §8).

## 6. Reproduzir

```sh
scripts/verify_m6.sh --oracle      # format, oráculo, Release arm64/x86_64, ctest, capturas, janela, golden
scripts/verify_m6.sh --no-window   # sem a execução com janela (sessão sem tela)
AM_GENERATED_DIR=/Volumes/AMRecomp/recomp/m5 scripts/verify_m4.sh && scripts/verify_m5.sh

# passos isolados
automodellista --sys <sys> --frames 1260 --pad-script "320:D_LEFT:8,360:A:8" \
    --shot 1250:/Volumes/AMRecomp/logs/m6/rt.png --window 1
python3 -I scripts/dolphin_trace.py --iso <rvz> --video Metal events --pad-script "320:D_LEFT:8,360:A:8" \
    --frames 1260 --shots 1250 --out /Volumes/AMRecomp/logs/m6/d.txt
python3 -I scripts/compare_shots.py rt.png d_f1250.png --side-by-side lado.png --diff diff.png
```

Logs e capturas em `/Volumes/AMRecomp/logs/m6/` (fora do git; as imagens são do jogo).

## 7. Estudo para o cel-shading (risco do plano, §7)

O plano previa que o cel-shading dependesse de cópias de EFB e de textura de Z. Contadores no GX (resumo no fim da
execução) com o roteiro do M5 até 669 quadros depois da largada, GX ligado (9 min 31 s para 6400 quadros em arm64,
contra ~57 s com `--gfx off`):

```
GX render ligado: 4704330 desenhos rasterizados, 9053126 triângulos (1610152 descartados pela face, 2151782 fora da tela)
EFB: 7778 cópias para o XFB, 0 para textura, 7778 limpezas
TEV: até 3 estágios num desenho; 0 desenhos com textura de Z, 4329629 com neblina, 0 com estágios indiretos;
     0 cópias de Z para textura
```

Até a largada o jogo **não copia o EFB para textura nem usa textura de Z ou TEV indireto**; usa neblina em 92% dos
desenhos e no máximo 3 estágios de TEV. A captura da largada já mostra o contorno escuro dos objetos e a cor chapada.
HIPÓTESE: o contorno é feito com geometria (o modelo de novo, um pouco maior, com a face da frente descartada: o
"casco invertido"), o que combina com os 1,6 milhão de triângulos descartados pela face; a cor em faixas viria de
textura/TLUT de rampa no TEV. Falta confirmar com o carro em movimento e em outros modos (replay, garagem), onde o
jogo ainda pode usar cópias. Para o M7/M8 isso tira o maior risco do plano: as cópias de EFB precisam existir (o
código está pronto e testado), mas o caminho quente é neblina + geometria.

## 8. Pendências e o que não foi verificado

- **Desempenho:** em software, depois da abertura (menus e largada) a média é ~0,11 s por quadro em arm64 (a
  abertura, ~0,02 s). Passar para a GPU
  (ubershader com SDL_GPU) é o caminho do M8; o rasterizador em software fica como referência.
- **Câmera da introdução da corrida** um pouco diferente do Dolphin em q6400: os eventos de disco e módulo batem, mas
  a animação de câmera não. É a mesma falta de igualdade de ciclo do M5 (§5.3 de M5_OS_DISCO.md) ou o tempo do
  áudio; investigar no M7, junto com o AX.
- **Amostragem bilinear** difere da GPU no miolo de letras pequenas ("Licensed by Nintendo"): suspeita na posição de
  amostragem em LOD > 0 ou no deslocamento de meio texel; não afeta o critério.
- **Não implementado/verificado:** cópias para textura com AA, bounding box, peek/poke do EFB pela CPU (nenhum acesso
  apareceu), formato de pixel YUV420, `frame_to_field` das cópias para o XFB, o caso NaN da neblina. A janela não tem
  entrada (o controle continua por roteiro) nem áudio (M7).
- **Builds Debug** do M6 só rodaram ctest e o boot até `main` (`verify_m4.sh`); as capturas foram verificadas em
  Release arm64 e x86_64. CI Linux/Windows continua sem execução (herdado do M3).

## 9. Lições

- **O oráculo para o XFB é o caminho de hardware do Dolphin, não o de software.** As duas codificações RGB -> YUV do
  Dolphin diferem no arredondamento e na média de U/V (o software filtra U/V em 1/4, 1/2, 1/4; o shader faz a média
  do par). A runtime copia a do shader, que é a que o oráculo executa; com isso o erro médio ficou em 0,2.
- **FMA muda pixels.** O mesmo C dava capturas diferentes em arm64 e x86_64 até desligar a contração de `a*b+c`
  só nos arquivos do GX; o golden agora prova que os dois builds desenham bit a bit igual.
- **Desenhar não mexe no tempo emulado:** o relógio é o dos ciclos estimados da CPU, então ligar o GX deixou os 551
  eventos do M5 idênticos, mesmo com a execução 10 vezes mais lenta.
