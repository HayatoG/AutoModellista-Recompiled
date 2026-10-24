# M8 parte 2b, peça cel: cel-shading com o carro andando

Pergunta: o contorno preto e o sombreado chapado dos carros saem certos com o carro andando, em software e na GPU,
contra o Dolphin? E a hipótese do M6 (contorno por geometria, sem cópia de EFB para textura, textura de Z nem TEV
indireto) vale fora da largada?

Resposta curta: **sim nas duas**. Nenhum bug de desenho achado; nenhum código da runtime mudou. A hipótese se confirma
até o quadro 8400 (largada, aceleração, derrapagem com fumaça, rodada e encosto no muro).

## 1. Roteiro

O roteiro do `verify_m8.sh` parava em 6400, ainda no voo de câmera da pista (sem carro). A contagem regressiva acaba
perto do quadro 7050. Roteiro estendido (`--cel` no `verify_m8.sh`): o roteiro normal mais `6900:A:1500` (acelera) e
`7700:D_RIGHT:120` (vira na primeira curva), até 8400 (cerca de 22 s de corrida). O botão é digital (o roteiro não tem
analógico): o carro entra na curva, derrapa, roda e para no muro, o que dá cenas variadas de cel-shading (reta, fumaça,
carro de lado, carro parado perto do muro). Garagem e replay **não** foram cobertos (exigem navegar menus ou terminar a
corrida; ficam para depois).

## 2. Capturas lado a lado (esquerda = runtime, direita = Dolphin)

Pasta `/Volumes/AMRecomp/logs/m8/p2b-cel/cel/`:

| quadro | cena | GPU x Dolphin | software x Dolphin |
| --- | --- | --- | --- |
| 7400 | reta, 97 mph | `lado_gpu_dolphin_f7400.png` | `lado_soft_dolphin_f7400.png` |
| 7600 | reta, 122 mph | `lado_gpu_dolphin_f7600.png` | `lado_soft_dolphin_f7600.png` |
| 7750 | derrapando na curva (fumaça) | `lado_gpu_dolphin_f7750.png` | `lado_soft_dolphin_f7750.png` |
| 7850 | carro de lado | `lado_gpu_dolphin_f7850.png` | `lado_soft_dolphin_f7850.png` |
| 7950 | rodando, perto do muro | `lado_gpu_dolphin_f7950.png` | `lado_soft_dolphin_f7950.png` |
| 8100 | muro e arquibancada | `lado_gpu_dolphin_f8100.png` | `lado_soft_dolphin_f8100.png` |
| 8400 | carro parado no muro | `lado_gpu_dolphin_f8400.png` | `lado_soft_dolphin_f8400.png` |

Também há `lado_gpu_soft_f<q>.png`, as imagens soltas (`gpu_f<q>.png`, `soft_f<q>.png`, `dolphin_f<q>.png`) e mapas de
diferença ampliados 8x em `diff_soft_dolphin_f7750.png`, `diff_gpu_dolphin_f7750.png` (e f8400). A exploração antes do
roteiro final (montagens de 8 quadros) está em `../explora/montagem*.png`.

## 3. Números (PSNR, dB)

| quadro | GPU x Dolphin | soft x Dolphin | GPU x soft |
| --- | --- | --- | --- |
| 7400 | 35,86 | 31,89 | 33,84 |
| 7600 | 35,34 | 31,06 | 32,88 |
| 7750 | 35,74 | 31,34 | 33,18 |
| 7850 | 35,68 | 31,57 | 33,66 |
| 7950 | 35,33 | 31,93 | 34,13 |
| 8100 | 36,91 | 32,34 | 34,28 |
| 8400 | 37,59 | 32,77 | 34,75 |

Pixels com diferença > 24: GPU x Dolphin 0,31% a 0,48%; soft x Dolphin 1,2% a 1,7%; GPU x soft 0,9% a 1,3%. Na largada
(q6400) eram 34,92 dB (GPU) e 29,38 dB (soft): com a pista e o carro o resultado é igual ou melhor.

Os mapas de diferença mostram só ruído fino na arquibancada (textura de multidão), nas faixas do céu e no hodômetro; **não
há borda clara de contorno no carro, no muro ou nas placas**, ou seja, o contorno preto está no mesmo lugar e com a mesma
espessura nos três. O software fica uns 4 dB atrás da GPU contra o Dolphin porque emula o dither do pixel do hardware
(RGBA6) e o Dolphin/Metal não, o que espalha erro de 1 a 8 níveis em áreas de textura detalhada; é diferença já presente
no M7/M8 (largada) e não ligada ao cel-shading. As posições coincidem mesmo com os ~2 campos de diferença no tempo
emulado da carga da pista (placar de tempo do HUD e posição do carro iguais nas duas imagens): nenhuma captura precisou
ser descartada por divergência de cena.

## 4. Estatísticas do GX no trecho (quadros 6400 a 8400)

Contadores finais de `--gfx soft` (8400 quadros) menos os do M8 até 6400 (`logs/m8/verify/runtime_soft_arm64.log`, mesmo
roteiro e mesmo código gerado); a GPU conta o mesmo número de desenhos, triângulos e cópias.

| contador | 0 a 6400 | 0 a 8400 | trecho 6400 a 8400 |
| --- | --- | --- | --- |
| desenhos rasterizados | 4 714 747 | 20 121 034 | 15 406 287 (7 703 por quadro, contra 737 antes) |
| triângulos | 9 064 612 | 29 014 187 | 19 949 575 |
| descartados pela face | 1 615 239 | 8 436 722 | 6 821 483 (34%) |
| fora da tela | 2 159 540 | 11 709 901 | 9 550 361 (48%) |
| triângulos efetivamente desenhados | 5 289 833 | 8 867 564 | 3 577 731 (18%) |
| desenhos com neblina | 4 340 449 | 19 534 346 | 15 193 897 (98,6% dos desenhos) |
| cópias EFB para o XFB | 7 780 | 14 252 | 6 472 (2 a 3 por quadro) |
| cópias EFB para textura | 0 | 0 | **0** |
| desenhos com textura de Z / cópias de Z | 0 / 0 | 0 / 0 | **0 / 0** |
| desenhos com TEV indireto | 0 | 0 | **0** |
| desenhos com zfreeze (GPU) | 0 | 0 | **0** |
| estágios de TEV no máximo | 3 | 3 | 3 |

Outros: 328 texturas decodificadas (26,4 M de texels) em 8400 quadros, só formatos de textura 0x0340 e TLUT 0x7,
no máximo 1 coordenada de textura, 100% dos desenhos com blend; GPU com 113 998 lotes (99 746 parciais, por causa das
14 252 leituras do EFB) e 3,2 ms de espera média por leitura. A corrida desenha **~10 vezes mais por quadro** que o menu
e a abertura (7,7 mil contra 0,7 mil), e quase 3 em cada 10 triângulos da corrida são descartados pela face.

## 5. Veredito sobre a hipótese do contorno

CONFIRMADA. Com o carro andando, derrapando e parado, o jogo continua sem copiar o EFB para textura, sem textura de Z,
sem TEV indireto e sem zfreeze; o único tráfego de EFB é a cópia para o XFB (2 a 3 por quadro). O contorno preto e o
sombreado chapado vêm de geometria e textura de rampa: neblina em 98,6% dos desenhos, no máximo 3 estágios de TEV e 34%
dos triângulos descartados pela face da frente (o "casco invertido" do contorno). Isso é exatamente o que o rasterizador
em software e o shader da GPU já fazem bem, e por isso batem com o Dolphin. Lição: o maior risco do plano (cel-shading
por cópias de EFB e textura de Z) nunca se materializou neste trecho.

Fora do que foi conferido: replay, garagem, outras pistas, chuva/noite e a câmera dentro do carro (o jogo pode usar
cópias de EFB em efeitos de replay ou de menu; os contadores acima não cobrem isso).

## 6. Mudanças e o que muda no golden

- `scripts/verify_m8.sh --cel`: roteiro estendido (Dolphin com `lockf`, software e GPU até 8400, comparação nos 7 quadros
  e os contadores do GX), localizado num bloco que sai antes do resto; não altera a execução normal. Limiares do `--cel`:
  soft x Dolphin >= 30 dB com no máximo 2% de pixels ruins; GPU x Dolphin >= 34 dB com no máximo 1%.
- Nenhum arquivo da runtime foi alterado; o golden `tests/golden/m8_corrida.json` **não muda** (o roteiro padrão continua
  em 6400 quadros). Se a sessão principal quiser fixar este trecho no golden, os valores são: contadores da coluna "0 a
  8400" da tabela acima e os 7 PSNR da seção 3.
- Tempo: GPU 8400 quadros em cerca de 3 min; software, cerca de 16 min a 9 quadros/s (arm64 Release); Dolphin cerca de
  10 min.

## 7. Lições para o vídeo

- Os números do M6 viraram previsão que a corrida de verdade confirmou: "zero cópias de EFB" aguentou 22 s de corrida.
- O erro maior do software contra o Dolphin é o dither de hardware (que o Dolphin não faz), não o contorno: olhar o mapa
  de diferença ampliado diz mais que o PSNR.
- Um `$q:e` em zsh vira modificador de variável: nomes de captura em loop precisam de `${q}`.
- O oráculo é um recurso compartilhado: com quatro agentes, a fila do `lockf` e um Dolphin morto por sinal custaram
  uma execução repetida.
