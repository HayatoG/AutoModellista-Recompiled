# M7: Menus

Estado: **atingido localmente, com uma ressalva de desempenho** (macOS arm64 e x86_64 via Rosetta, Release). A janela
lê teclado e gamepad e o jogo é navegável da abertura à largada por uma pessoa; o áudio do AX sai pelo SDL3 e o
`--audio-dump` grava o mesmo som que o Dolphin, amostra por amostra; o filme da abertura (Sofdec) aparece na janela,
decodificado pelo próprio código do jogo. A ressalva: desenhando na CPU, os menus mais pesados ficam em 18–22
quadros/s desenhados (arm64), não 60; o jogo continua no ritmo certo porque a janela em tempo real pula o desenho de quadros
quando atrasa (§4). Os 60 quadros/s desenhados ficam para a GPU (M8).

Critério de saída do plano (`PLANO_PORT_C.md` §4): "navegar todos os menus com controle; áudio e vídeos de abertura
tocando". Resultado: cumprido, verificado contra o Dolphin 2609a em §5 e no golden `tests/golden/m7_menus.json`.

## 1. Resumo do resultado

```
$ scripts/verify_m7.sh --oracle
== oráculo: Dolphin (Metal, XFB na RAM, DumpAudio) com o roteiro, capturas nos quadros 1600,1800,2300,2880,3200,3500
252 eventos, 3600 quadros em 77.2s; fim: frames
== arm64 Release (/Volumes/AMRecomp/build-arm64-release)            (idem x86_64 Release, Rosetta: 143 s, 12,8 quadros/s)
100% tests passed out of 47
parada em 0x80051034: M5: 3600 quadros completos do laço fn_80051034 em 97 s; menu principal a 18.4 quadros/s
  AX: 12394 listas, 74364 comandos (usados 0x4C08D), 12394 quadros de 5 ms na MRAM, 0 amostras saturadas;
      vozes: até 64 por quadro, 6421248 amostras lidas, 6 fins, 1465 laços
  áudio: 1983536 quadros estéreo pela DMA (61.9 s a 32028 Hz), gravados no --audio-dump
  GX render ligado: 620630 desenhos, 2385927 triângulos, 2771567608 pixels no TEV; 116222 desenhos em faixas paralelas
  GX cache de texturas: 1059380 acertos, 25253 decodificações (1924775936 texels)
  GX recursos: formatos de textura 0x0340, TLUT 0x7, até 1 coordenadas; blend 620630 desenhos; testes de alfa 0x38;
      neblina 0x05; estágios 0x0007
runtime_arm64_f1600.png x dolphin_f1600.png: PSNR 34.96 dB, ... pixels com diferença > 24: 0.837% -> OK
runtime_arm64_f2300.png x dolphin_f2300.png: PSNR 35.68 dB, ... pixels com diferença > 24: 0.456% -> OK
(... 6 capturas, todas OK; tabela em §5.1)
runtime_arm64.wav x dolphin.wav: 61.93 s x 61.99 s, defasagem 8.24 ms (264 amostras); 44 janelas ativas de 61,
  17 mudas nos dois, 0 divergentes; correlação mediana 1.0 (mín. 0.5187), |dif. de nível| mediana 0.0 dB (máx. 0.26);
  correlação ok em 93.2%, nível ok em 100.0% -> OK
== arm64: 1 trabalhador e sem cache de texturas (as capturas têm de ser as mesmas)
q1600: idêntica à captura com 10 trabalhadores e cache
q2300: idêntica à captura com 10 trabalhadores e cache
== janela SDL3 (arm64): a imagem desenhada na janela no quadro 2300 (menu principal)
window_f2300_janela.png x runtime_arm64_f2300.png: PSNR 99.0 dB (idênticas) -> OK
== tempo real (arm64): janela com pulo de quadros, sem som, até o menu principal
2400 quadros em 42 s (tempo do jogo: 40 s); menu principal a 59.3 quadros/s do jogo; 245 quadro(s) pulado(s)
== golden (tests/golden/m7_menus.json)
```

Os dois builds dão os mesmos pixels e as mesmas amostras de áudio (o golden guarda o SHA-256 de cada captura e do
PCM). Os contadores novos do GX mostram o que os menus usam: texturas RGBA8, C4 e C8 (TLUT IA8, RGB565 e RGB5A3), uma
coordenada de textura, 1 a 3 estágios de TEV, blend por alfa em todos os desenhos, testes de alfa `<=`, `>` e `!=`,
neblina desligada ou linear, **nenhuma cópia do EFB para textura**, nenhuma textura de Z, nenhum estágio indireto.
Tudo isso já estava implementado no GX do M6 (o TEV de 16 estágios, o indireto e as cópias para textura têm teste); o
M7 não precisou de recurso novo do GX para os menus, só de velocidade.

`verify_m4.sh` (5 builds, clang-tidy limpo, trace até `main`), `verify_m5.sh` (551 eventos) e `verify_m6.sh` (logo a
49,18 e 46,37 dB) continuam passando com os mesmos builds e os goldens inalterados.

## 2. O que foi feito

```
runtime/core/pad_input.c      teclado e gamepad (SDL) -> controle de GameCube; mapeamento fixo (§3)
runtime/hw/si.c               estado inteiro do controle (botões, sticks, gatilhos) nos modos 0 a 4 do GetData
runtime/audio/ax_voice.c      vozes do AX tocando: conversor de taxa, envelope, passa-baixa, mixagem com volume e rampa
runtime/audio/ax_mixer.c      lista de comandos do AX inteira: SETUP, DL_AND_VOL_MIX, AUX com a CPU, compressor, OUTPUT
runtime/audio/audio_dump.c    --audio-dump: WAV no formato do dspdump.wav do Dolphin
runtime/hw/dsp.c              a DMA de áudio entrega cada bloco de 32 bytes da MRAM a um coletor (WAV e/ou som)
runtime/platform/platform.c   SDL3: entrada (SDL_Gamepad, teclado), áudio (SDL_AudioStream), trabalhadores, relógio
runtime/gfx/gx_raster.c       fila por desenho e rasterização em faixas intercaladas pelos trabalhadores
runtime/gfx/gx_tex.c          cache de texturas decodificadas, validado por contadores de escrita da MEM1
runtime/gfx/gx_tev.c          tabela de entradas por desenho; atalho do estágio sem indireto
runtime/gfx/gx_efb.c          cópia EFB -> XFB linha a linha pelos trabalhadores
runtime/game/game_run.c       modo interativo: controle real, tempo real, pulo de quadros, som; --timebase-out
scripts/compare_audio.py      comparação de áudio com alinhamento (§5.2)
scripts/dolphin_trace.py      + --audio-dump (DumpAudio), --timebase-out, --dolphin-config
scripts/verify_m7.sh, scripts/check_m7_golden.py, tests/golden/m7_menus.json, scripts/assistir.sh jogar
```

O HLE do AX é uma tradução para C de `DSPHLE/UCodes/AX.cpp`, `AXVoice.h` e `DSPAccelerator.cpp` do Dolphin 2609a
(fonte local em `/Volumes/AMRecomp/ref/dolphin`), com atribuição no cabeçalho dos arquivos. Testes novos: `pad_input`,
`ax_mixer`, `audio_dump`; `ax_voice` ganhou o conversor de taxa, a mixagem, o passa-baixa e o bug do ADPCM (§8);
`gx_render` e `gx_tex` ganharam a igualdade das faixas paralelas e do cache. São 47 testes, todos com dados montados à
mão.

## 3. Controle

Sem `--pad-script`, a janela é o controle 1 (o roteiro continua com prioridade para os testes). O mapeamento padrão
está em `runtime/include/am/pad_input.h` (desde o M9 parte 5, remapeável na tela inicial; `docs/M9_PARTE5_LAUNCHER.md`
§3):

| GameCube | Teclado (padrão do Dolphin, `GCPadEmu.cpp`) | Gamepad (SDL_Gamepad, por posição) |
| --- | --- | --- |
| A, B, X, Y | X, Z, C, S | sul, oeste, leste, norte |
| Z | D | ombro direito ou esquerdo |
| START | Return | Start |
| direcional | T, G, F, H (cima, baixo, esquerda, direita) | direcional |
| stick | setas (Shift = meia inclinação) | stick esquerdo (zona morta radial de 12%) |
| stick C | I, K, J, L | stick direito |
| L, R | Q, W (curso inteiro + clique) | gatilhos analógicos (clique a partir de 90%) |

Teclado e gamepad valem juntos: botões somam, e em cada eixo vale o mais inclinado. Os eixos seguem o `MapFloat` do
Dolphin (-1 -> 1, 0 -> 128, +1 -> 255) e o SI entrega o estado em todos os modos do `GetData` (o jogo usa o 3). O
teste `pad_input` monta as entradas à mão e confere botões, eixos, zona morta, gatilhos e as palavras do SI por modo.

## 4. Desempenho

Medido no Apple M4 (4 núcleos de desempenho + 6 de eficiência), Release, quadros/s do laço do jogo no menu principal
(quadros 2160 a 2400), sem pular quadros:

| Build | M6 | M7 |
| --- | --- | --- |
| arm64 | ~9 | 18–22 |
| x86_64 (Rosetta) | não medido | 12–13 |

O perfil (`sample`) do M6 mostrava 97% do tempo no GX: o TEV, a decodificação de textura texel a texel e o
rasterizador. Três mudanças, todas com a imagem idêntica byte a byte (as 7 capturas de referência do logo, do filme e
dos menus, e o golden do M6):

1. **Faixas paralelas.** Os triângulos de um desenho vão para uma fila; no fim do desenho cada trabalhador (threads da
   SDL, um por núcleo lógico) rasteriza todos eles, mas só nas linhas de blocos 2x2 da sua faixa (faixas intercaladas,
   distribuídas sob demanda). Cada pixel é tocado por uma faixa só, na ordem dos triângulos, então o resultado não
   depende do número de threads (`--gx-threads 1` dá a mesma imagem; o `verify_m7` confere). Desenhos pequenos
   (< 4096 pixels de área) seguem sem threads.
2. **Cache de texturas decodificadas.** Cada nível usado é decodificado inteiro para RGBA uma vez e reaproveitado
   enquanto a memória dele não mudar: a MEM1 tem um contador de escritas por página de 4 KiB (as escritas do C
   gerado, o diário de escrita do `cpu.c` do DolRecomp, a DMA do DVD), e a soma dos contadores da faixa só cresce.
   A TLUT entra na chave pelo hash do conteúdo.
3. **TEV e cópia para o XFB.** Seletores do TEV decodificados por desenho numa tabela de entradas; atalho do estágio sem
   indireto; a cópia EFB -> XFB vai linha a linha pelos trabalhadores.

O ganho ficou em 2 a 2,4x (as medidas variam com a carga da máquina: o indexador do Spotlight chegou a ocupar três
núcleos durante as medições), não os ~7x que 60 quadros/s pediriam. O perfil depois das mudanças explica: no menu principal
são ~1 milhão de pixels por quadro no TEV (as camadas do fundo, a moldura e o filme da pista), a ~150–250 ns por
pixel no código traduzido do backend Software do Dolphin, e metade do tempo do quadro é serial (a CPU emulada,
~9 ms por quadro, e o que o GX faz antes de rasterizar), quando os trabalhadores esperam. Desenhar a 60 quadros/s
pede a GPU (M8) ou reescrever o pixel para algo dez vezes mais barato, mantendo a mesma aritmética.

Para a janela ser usável já, o **modo interativo** (`--window` sem `--pad-script`) segura o tempo emulado no ritmo do
relógio (`--realtime`) e **pula o desenho** de até 4 quadros seguidos quando atrasa (`--frameskip`): os comandos do GX
continuam sendo lidos (tokens, DrawDone, estado), mas desenhos e cópias para o XFB não acontecem, o EFB fica limpo
para o próximo quadro e a janela mantém a última imagem. Cópias do EFB para textura nunca são puladas (se o jogo fez
uma nos últimos 120 quadros, não pula). Medido no arm64 com o roteiro (log a cada 60 quadros):

| Trecho | Quadros/s do jogo | Quadros desenhados por segundo |
| --- | --- | --- |
| logos, filme da abertura, "PRESS START" | 60 | 53–60 |
| menu principal | 57–60 | 12–14 |
| escolha de pista e de música | 59–61 | 15–17 |

O jogo, a música e os efeitos andam no ritmo certo; a imagem dos menus atualiza 12 a 17 vezes por segundo. O som sai
certo porque o ritmo é o do tempo emulado (§5).

## 5. Comparação com o Dolphin

### 5.1 Imagem

`compare_shots.py`, o mesmo critério do M6 (PSNR >= 30 dB, <= 1% de pixels com diferença > 24, os dois lados
acesos), com o roteiro do M5 até a escolha da música:

| Quadro | Tela | PSNR | Erro médio | Máx. | Pixels > 24 |
| --- | --- | --- | --- | --- | --- |
| 1600 | filme da abertura (Sofdec) | 34,96 dB | 1,52 | 194 | 0,84% |
| 1800 | "PRESS START" | 40,64 dB | 1,60 | 20 | 0% |
| 2300 | menu principal | 35,68 dB | 2,73 | 81 | 0,46% |
| 2880 | modo (single race) | 36,06 dB | 2,69 | 105 | 0,38% |
| 3200 | escolha da pista | 32,76 dB | 2,37 | 243 | 0,88% |
| 3500 | escolha da música | 31,43 dB | 2,24 | 254 | 0,49% |

Fora do `verify_m7`, a mesma rota até o quadro 5600 (carro, estilo, ajuste, regras, "go race") deu de 34,6 a 37,4 dB
em todas as 15 capturas.

O que ainda difere, e por quê:

- **Arredondamento** de ±1 a ±6 em quase todo pixel dos menus (erro médio 2–2,7; a runtime sai ~1 nível mais escura):
  o Dolphin desenha na GPU, em ponto flutuante, e o software segue a aritmética inteira do TEV e do blend do
  hardware. As camadas do menu são várias, com blend por alfa, e o erro de cada uma soma.
- **Bordas** (< 1% de pixels acima de 24): a amostragem bilinear em ponto fixo s17.7 contra a da GPU, já vista no M6.
- **Animações ligadas ao tempo**: o texto em "máquina de escrever" da caixa de ajuda e o filme de prévia da pista
  ("Select a BGM." na runtime, "Select a LCT." no Dolphin no q3500) mostram outro instante. O tempo emulado do
  Dolphin anda mais nos quadros de carregamento (§7), então animações por tempo ficam adiantadas lá.

O filme da abertura (q1600) bate a 35 dB no mesmo quadro do filme; a diferença visível é uma linha horizontal no meio
da imagem e as bordas (HIPÓTESE: o filme é desenhado em faixas de textura e a costura entre elas é amostrada de outro
jeito pelo bilinear).

### 5.2 Áudio

O Dolphin grava o que a DMA de áudio entrega ao mixer (`DumpAudio`, `dspdump.wav`, 32028 Hz, desde o primeiro
bloco); a runtime grava o mesmo ponto (`--audio-dump`). Os dois andam no tempo emulado, mas o jogo não chega ao mesmo
ponto no mesmo ciclo nas duas máquinas, então o mesmo som começa alguns quadros de 5 ms antes ou depois.
`compare_audio.py` alinha antes de comparar: envelope de 10 ms, defasagem global pela correlação dos envelopes,
refinada em amostras; depois, por janela de 1 s, um ajuste local de até ±0,5 s e a correlação normalizada das amostras
e a diferença de nível RMS. Critério (justificativa no cabeçalho do script): nenhuma janela com som de um lado e
silêncio do outro; >= 95% das janelas ativas com nível dentro de 1 dB; >= 80% com correlação >= 0,9 (uma janela que
mistura música e efeito começados em instantes diferentes não alinha as duas fontes com uma defasagem só).

| Trecho | Duração | Janelas ativas | Correlação ok | Nível ok | Correlação mediana | Dif. de nível mediana |
| --- | --- | --- | --- | --- | --- | --- |
| abertura e logos (2400 quadros) | 41,8 s | 24 | 100% | 100% | 1,0000 | 0,00 dB |
| até a escolha da música (`verify_m7`, 3600 quadros) | 61,9 s | 44 | 93,2% | 100% | 1,0000 | 0,00 dB |
| até "go race" (5700 quadros) | 97,2 s | 80 | 90,0% | 100% | 1,0000 | 0,00 dB |

Onde uma fonte só domina a janela, a correlação é **1,0000**: as amostras são as mesmas do Dolphin, deslocadas de um
número inteiro de quadros do AX (264, 424, 744... amostras = 8,2 ms + n x 5 ms). O nível bate a 0,0 dB de mediana.

## 6. Vídeos (Sofdec)

Os filmes são arquivos SFD (MPEG-PS com vídeo MPEG-1 e áudio ADX) dentro dos `.afs`: o da abertura é
`afs00.afs#241` (21 MB) com o áudio em `afs00.afs#240`; há outros 49 SFD em `afs01_gu.afs` e 96 em `afs02.afs` (as
prévias das pistas e dos carros nos menus). A decodificação é do próprio jogo: o DOL traz a CRI Middleware inteira
(strings no REA/Ghidra: `MWSFD/GC Ver.2.65b`, `CRI SFD/GC Ver.1.845`, `CRI MPV/GC Ver.1.843` para o vídeo,
`CRI DCT/GC`, `ADXT/GC Ver.8.66` para o som) e o quadro decodificado vira textura do GX (formatos usados nos menus:
RGBA8, C4 e C8). O relógio do filme é o do áudio (HIPÓTESE pelas strings de depuração do SFD, `master_time`,
`adxt_stat`: o vídeo segue o tempo da voz ADX), que anda pelo AX. Não faltava nada na runtime: o filme já aparecia no M6, só não tinha som; com o AX tocando, imagem e
som saem juntos.

## 7. Câmera da introdução da corrida (q6400)

Não é o áudio: com o AX completo a câmera continua no mesmo lugar de antes (20,9 dB contra o Dolphin no q6400; o
melhor casamento com os quadros vizinhos do Dolphin também é o q6400, então não é um quadro de atraso). A causa medida
com `--timebase-out` nos dois lados (timebase na entrada de cada quadro, em cada leitura e em cada interrupção do
DVD) é o **tempo emulado nos carregamentos**: com os mesmos 551 eventos e o mesmo número de quadros, o Dolphin está
1,37 s (82 campos de VI) mais adiante no q6400. A diferença nasce nos quadros de carregamento (menus e, sobretudo, a
carga da pista: quadros de 21–25 campos no Dolphin contra 14–16 na runtime). As leituras do DVD duram o mesmo nos dois
lados (o modelo do drive é o do Dolphin); o que dura mais no Dolphin é o trabalho da CPU entre uma leitura e a seguinte
(até 1,9x em `afs01`), em degraus de um campo de VI. A animação da câmera depende do tempo (não do número de
quadros), então começa de outro ponto. Corrigir pede igualar o custo em ciclos que a runtime estima para o C gerado ao
que o JIT do Dolphin conta, o que fica para o M8 (desempenho e tempo).

## 8. Bugs que valem registro

- **Saturação do ADPCM** (`ax_voice.c`, desde o M5): o acelerador saturava em -0x7FFF; o Dolphin
  (`SaturatingCast<s16>`) vai até -0x8000. Sem som não aparecia; com a mixagem, uma amostra no fundo da escala sairia
  diferente. Teste `adpcm_saturates_at_minus_32768`.
- **Ordem de avaliação num inicializador** (`ax_mixer.c`, pego antes do primeiro teste): `{arg16(l), arg16(l),
  arg16(l)}` não tem ordem definida em C; os três volumes do DL_AND_VOL_MIX passaram a ser lidos um por vez.

## 9. Reproduzir

```sh
scripts/verify_m7.sh --oracle        # format, oráculo, Release arm64/x86_64, ctest, capturas, áudio, threads, janela
scripts/verify_m7.sh --no-window     # sem as execuções com janela
scripts/assistir.sh jogar            # janela interativa: teclado/gamepad, som, tempo real

# passos isolados
automodellista --sys <sys> --frames 3600 --pad-script "<roteiro>" --shot 2300:rt.png --audio-dump rt.wav
python3 -I scripts/dolphin_trace.py --iso <rvz> --video Metal events --pad-script "<roteiro>" --frames 3600 \
    --shots 2300 --audio-dump d.wav --out d.txt
python3 -I scripts/compare_audio.py rt.wav d.wav
automodellista ... --gfx off --timebase-out rt_tb.txt; python3 -I scripts/dolphin_trace.py ... --timebase-out d_tb.txt
```

Logs, capturas e WAV em `/Volumes/AMRecomp/logs/m7/` (fora do git).

## 10. Pendências e o que não foi verificado

- **60 quadros/s desenhados** nos menus: não (§4). O caminho é a GPU no M8; o rasterizador em software continua como
  referência dos testes e do golden.
- **Tempo emulado nos carregamentos** diferente do Dolphin (§7): afeta a câmera da largada e animações por tempo nos
  menus. A música e os efeitos batem amostra por amostra, só deslocados.
- **Entrada real só testada por unidade**: o mapeamento e as palavras do SI têm teste; a leitura da SDL (teclado e
  gamepad) foi exercitada só à mão, sem um roteiro automático. Não há arquivo de configuração do mapeamento ainda
  (o plano previa um).
- **Menus fora da rota de verificação** (garage life, VJ & theater, options, save/load, special movie): navegáveis pela
  janela, mas não comparados ao Dolphin; save/load depende do memory card (M8).
- **O conversor polifásico do AX** não existe sem os coeficientes da DROM do DSP; como o oráculo também não os tem,
  os dois usam o linear. O som de um GameCube de verdade é um pouco diferente nisso.
- **Builds Debug** do M7 não rodaram o jogo (só ctest); CI Linux/Windows continua sem execução (herdado do M3).

## 11. Lições

- **O áudio bateu amostra por amostra de primeira**: traduzir o HLE do Dolphin e gravar o mesmo ponto da cadeia que
  o `DumpAudio` grava (a DMA, não o mixer do host) deu correlação 1,0000 contra o oráculo. O que sobra é *quando* cada
  som começa, não *como* ele soa.
- **Determinismo primeiro, threads depois**: dividir o rasterizador por faixas de linhas (e não por triângulos) mantém
  cada pixel com uma dona só e na mesma ordem; a prova é a captura com 1 e com 10 threads ser a mesma, byte a byte.
- **Medir antes de escolher**: o primeiro perfil mandava paralelizar; o segundo mostrou que metade do quadro é serial
  e que a próxima ordem de grandeza não vem de mais threads, vem da GPU.
- **Câmeras e textos que andam pelo relógio** denunciam diferenças de tempo emulado que os eventos por quadro não
  mostram: os 551 eventos batem e mesmo assim a câmera está em outro lugar.
