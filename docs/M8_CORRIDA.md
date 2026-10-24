# M8: Corrida

Estado da **parte 1** (tempo emulado e GX na GPU): **atingida localmente, com ressalva de desempenho na corrida**
(macOS arm64 e x86_64 via Rosetta, Release). **Parte 2b** (§12): 60 quadros/s desenhados na corrida no arm64, tempo
emulado sem defasagem contra o Dolphin, memory card gravado e lido (igual byte a byte ao do Dolphin) e cel-shading
conferido com o carro andando. A parte 2a (§11) pôs o GX numa thread própria.

Critério de saída do plano (`PLANO_PORT_C.md` §4): "uma corrida completa a 60 fps estáveis; save gravado e lido".
A parte 1 cobre o que vem antes: o tempo emulado igual ao do Dolphin (sem isso a câmera da largada e tudo que anda
pelo relógio sai em outro lugar) e o desenho na GPU (sem isso não há 60 quadros/s).

## 1. Resumo do resultado

| O quê | Antes (M7) | Parte 1 | Parte 2b (§12) |
| --- | --- | --- | --- |
| Defasagem do tempo emulado no q6400 | 82,4 campos de VI (1,37 s) | 2,1 campos (0,035 s); até o q5889, 0,000 | **0,000** (máximo 0,038 em qualquer quadro) |
| Câmera da largada (q6400, software x Dolphin) | 20,9 dB | **29,4 dB** (o melhor casamento é o mesmo quadro) | 29,38 dB (mesma imagem) |
| GPU x software (q1600 / q1800 / q2300 / q6400) | — | 36,9 / 44,6 / 39,2 / 30,7 dB | iguais |
| GPU x Dolphin (q1600 / q1800 / q2300 / q6400) | — | 39,4 / 43,3 / 40,0 / 34,9 dB | iguais |
| Janela, menu principal (arm64, quadros/s do jogo/desenhados) | 57–60 / 12–14 | 60 / 60 | 60,1 / 60,0 |
| Janela, corrida (arm64) | 59 / 13 (GPU antes das otimizações) | 52,8 / 30,8 | **59,4 / 59,7** |
| Janela, menu / corrida (x86_64, Rosetta) | — | 60,1 / 34,2 e 38,9 / 31,5 | 59,9 / 60,0 e 48,1 / 60,0 |
| Memory card | — | — | **salvo e lido; `.raw` igual ao do Dolphin** |
| Testes (ctest) | 47 | 49 | 50 |

Os números são das execuções de `verify_m8.sh` (§5 e §12.5); os dois builds dão o mesmo tempo emulado, os mesmos
pixels no software e as mesmas métricas na GPU.

## 2. Tempo emulado igual ao do Dolphin

### Como foi medido

O `--timebase-out` dos dois lados grava o timebase na entrada de cada quadro, em cada leitura do DVD e em cada
interrupção do DVD. Duas ferramentas novas ajudaram a achar onde o tempo diverge:

- `--profile q0:q1:arq` (runtime): ciclos emulados por trecho do C gerado e por thread do SO do jogo entre dois quadros;
- `--probes q0:a,b,...` (runtime e `dolphin_trace.py`): a partir do quadro q0, o timebase na entrada de funções
  escolhidas, nos dois lados, para alinhar as sequências e ver em que trecho a diferença nasce.

Detalhe que importa: o stub GDB do Dolphin mostra no registrador TBL **o valor do último `mftb` do jogo**, não o tempo
de agora (`GDBStub.cpp` lê o SPR, que só o `mftb` atualiza). A runtime gravava o tempo exato; agora grava a mesma coisa
que o oráculo (`timebase_guest_last`), senão a comparação mistura duas grandezas.

### Causas encontradas (em ordem de tamanho)

1. **Instruções de fallback contavam 0 ciclos.** O DolRecomp tira do downcount o custo de cada instrução pela tabela do
   Dolphin, mas as que vão para o interpretador (`dcbf`, `dcbst`, `dcbi`, `icbi`, `mfspr`/`mtspr` de SPRs raros,
   `mfsr`...) ficavam com 0. O laço do `DCFlushRange` (`dcbf`, `addi`, `bdnz`) custava 2 ciclos por linha de 32 bytes
   em vez de 7. Nos carregamentos o jogo passa boa parte do tempo nele (no perfil da carga da pista, q6150–6191, eram
   18% dos ciclos mesmo contados a 2 por linha: 67 milhões de iterações), e era daí a diferença "em degraus de um campo":
   cada quadro longo do carregamento terminava antes de um retrace na runtime e depois no Dolphin. Correção: o
   fallback desconta os ciclos do `PPCTables.cpp` (`ppc_sys_fallback_cycles`). Defasagem: 82 -> 2,5 campos.
2. **O tempo antes do entry point.** O Dolphin roda o apploader na CPU emulada (BS2 em HLE, `Boot_BS2Emu.cpp`), com o
   TB e o VI contando; a runtime começava do zero. A diferença na primeira interrupção do DVD era 200 059 tiques; a
   runtime agora começa com esse tempo (`BOOT_BS2_CYCLES`, medido e marcado como hipótese no código). Com isso a fase
   do VI fica igual e a primeira interrupção do DVD cai no mesmo tique (836 463 x 836 475). Defasagem no q1: 0,5 -> 0,0.
3. **O vetor de exceção em HLE contava 0 ciclos.** O caminho do `__OSEVStart` (38 ciclos pela tabela do Dolphin) e o
   do system call (12) agora entram no tempo. Efeito pequeno (~0,5 campo em 100 mil interrupções), mas é o mesmo
   princípio.

### Hipóteses descartadas, com a medição

- **Custo por instrução**: a tabela do DolRecomp é a do Dolphin (conferida contra `PPCTables.cpp`, inclusive `lmw`,
  divisões, `fdiv`, `ps_div`).
- **Idle skipping do Dolphin**: só pula até o próximo evento, o mesmo que a runtime faz rodando o laço; não muda o
  tempo.
- **GPU no tempo emulado (dual core x single core)**: o Dolphin dá exatamente o mesmo timebase com e sem a thread de
  GPU, e de uma execução para outra (determinístico).
- **DMA da ARAM**: a mesma fórmula do Dolphin (246 ciclos por 32 bytes).
- **HLE de `OSReport` do Dolphin**: é do tipo "início" (roda o original depois); não muda ciclos.

### O que sobra (2,1 campos no q6400)

Dois degraus de um campo na carga da pista (q5891 e q5973) e 0,1 campo no fim. Em cada quadro longo da carga o Dolphin
leva ~330 mil tiques (meio campo) a mais; na maioria dos quadros isso não atravessa um retrace, em dois atravessa. As
sondas mostram que a diferença está dentro de uma chamada indireta da função do quadro (depois de `fn_80048834`), num
trecho sem `mftb`, então o timebase "do último `mftb`" não a localiza melhor; fica para a parte 2 (sondas na tabela de
chamadas indiretas). Os 5889 primeiros quadros batem com menos de 0,01 campo.

**Resolvido na parte 2b (§12.2):** era o laço contado do DolRecomp cobrando só o primeiro bloco do corpo (o `memmove`
do SDK custava 2 ciclos por byte em vez de 4). Com o patch, 0,000 campo no q6400.

## 3. GX na GPU

### Arquitetura

O caminho até o vértice transformado (FIFO, vertex loader, XF, recorte, descarte pela face) é o mesmo do software. Em
`--gfx gpu` (`gfx/gx_gpu.c`):

- cada triângulo vira 3 vértices em pixels do EFB (posição com o deslocamento do scissor, cores, texcoords já na escala
  do SU), e cada desenho, um comando com um uniform de 624 bytes com o estado do TEV e do Pixel Engine copiado dos
  registradores do BP (`am/gpu_batch.h`);
- as texturas vão decodificadas em RGBA (as mesmas do cache de texturas do software) para um buffer de 128 MiB na GPU,
  enviadas uma vez por decodificação (o "serial" do cache identifica);
- a GPU desenha com **um ubershader** (`runtime/shaders/gx_ubershader.metal`) que faz o que o software faz por pixel:
  TEV em aritmética inteira (os mesmos `combine`, `compare`, swap, konst), amostragem com wrap e filtro bilinear em
  s17.7 lendo os texels do buffer, LOD pelas derivadas do bloco 2x2, teste de alfa, textura de Z, neblina, teste de Z e
  blend/lógica/dither do Pixel Engine;
- o EFB da GPU são **duas imagens R32Uint** (cor empacotada em 24 bits no formato do PE e Z de 24 bits, iguais às do
  `gx_efb.c`) lidas e escritas pelo próprio shader com *framebuffer fetch* das GPUs da Apple, que garante a ordem dos
  fragmentos que se sobrepõem. Não há blend nem teste de profundidade fixos, então a aritmética do PE é a mesma do
  software;
- numa cópia do EFB, o lote vai para a GPU, **só as linhas que a cópia lê** voltam para a CPU (o Z só numa cópia de Z),
  e a cópia para o XFB ou para textura roda no código do software (filtro vertical, gama, YUV, formatos de textura).
  A limpeza da cópia vira um desenho na GPU. Cópias EFB -> textura e EFB -> XFB funcionam pelo mesmo caminho (a de
  textura não aparece até a largada; está no teste unitário com uma GPU de mentira);
- lotes parciais a cada 192 desenhos, enviados sem esperar, para a GPU desenhar enquanto a CPU emula.

### Shaders offline sem SDL_shadercross

O plano previa HLSL compilado com SDL_shadercross para SPIR-V/MSL/DXIL. Não há SDL_shadercross, dxc nem glslang nesta
máquina; há o compilador Metal do Xcode. Decisão: o ubershader é **MSL escrito à mão**, compilado offline para
`metallib` por `scripts/build_shaders.sh` e embutido como header C (`runtime/shaders/gx_ubershader_metallib.h`, gerado
e versionado). A runtime cria o shader com `SDL_GPU_SHADERFORMAT_METALLIB`: nada é compilado em tempo de execução. O
build não precisa do compilador Metal.

Consequências: só o driver Metal da SDL3 GPU funciona; o *framebuffer fetch* exige GPU da Apple (M1 em diante; Macs
Intel não). Vulkan e D3D12 precisam de outra forma de ler o EFB no shader (cópia do EFB por desenho ou
*fragment shader interlock*) e de SPIR-V/DXIL: fica registrado como pendência; o software continua sendo o caminho
em qualquer plataforma.

### Fora do que a GPU faz (contado no log)

Estágios indiretos (o estágio usa a coordenada sem deslocamento) e zfreeze (Z do próprio triângulo). Até a largada o
jogo não usa nenhum dos dois (0 desenhos). A amostragem de pixels nas bordas difere um pouco do software (a GPU amostra
no centro do pixel com o vértice a -1/16; o software avalia os atributos a +0,495): é a maior parte da diferença entre
GPU e software, e por isso a GPU fica até mais perto do Dolphin, que também desenha numa GPU.

## 4. Desempenho

Medido no Apple M4, Release, janela em tempo real com pulo de quadros, sem som (`verify_m8.sh`, §5). "Quadros/s do jogo
/ desenhados", média das linhas de progresso.

| Trecho | arm64 | x86_64 (Rosetta) |
| --- | --- | --- |
| menu principal (q2160–2400) | 60,9 / 60,0 | 60,1 / 34,2 |
| corrida (q6240–6600) | 52,8 / 30,8 | 38,9 / 31,5 |

Sem janela, até a largada: `--gfx gpu` 84 s (arm64) e 121 s (x86_64); `--gfx soft` 186 s e 294 s.

Sem janela, desenhando todos os quadros (`--gfx gpu`, arm64), a corrida roda a 46–51 quadros/s; a CPU emulada sozinha
(`--gfx off`), a 81–88. Para chegar aí:

- **cache de texturas**: o contador de escritas é por página de 4 KiB, e dados vizinhos escritos a cada quadro
  invalidavam texturas que não mudaram: 87 mil decodificações até a largada. Agora um hash dos bytes confirma antes de
  decodificar: 264. O hash da TLUT é memorizado enquanto a TMEM não recebe carga. (Vale também para o software.)
- **limpeza na GPU e leitura só das linhas copiadas**: sem reenviar o EFB inteiro a cada quadro;
- **lotes parciais**: a espera média pela GPU numa cópia ficou em ~2,3 ms;
- **pulo de quadros com GPU**: no máximo 1 seguido (com o software são 4). O quadro desenhado custa ~21 ms e o pulado
  ~12; pulando um de cada vez a corrida desenha 30 por segundo em vez de 13–18.

O que falta para 60 desenhados na corrida: o trabalho do GX na CPU (vertex loader, XF, montagem, envio, cópia para o
XFB) roda na mesma thread que a CPU emulada, ~9 ms por quadro em cima dos ~12 da emulação. O próximo passo é uma thread
para o GX (como o "dual core" do Dolphin), com as sincronizações que o jogo enxerga (tokens, DrawDone, cópias) no ponto
certo: parte 2.

## 5. Verificação

```sh
scripts/verify_m8.sh             # format, builds, ctest, oráculo se faltar, tempo, capturas soft/GPU, janela, golden
scripts/verify_m8.sh --no-window # sem as execuções com janela
```

- **Tempo emulado**: `compare_timebase.py` contra o Dolphin; critério |defasagem no q6400| <= 3 campos; os dois builds
  têm de dar o mesmo arquivo de timebase.
- **Capturas** (q1600 filme, q1800 "PRESS START", q2300 menu principal, q6400 largada), na resolução nativa:
  software x Dolphin >= 30 dB (largada: >= 28 dB, até 4% de pixels com diferença > 24); GPU x software >= 30 dB (até
  3%); GPU x Dolphin >= 30 dB (até 1%).
- **Golden** `tests/golden/m8_corrida.json`: tempo emulado, métricas e SHA-256 dos pixels do software, e só "passou"
  para a GPU (a imagem da GPU depende do computador).

Resultado da última execução (arm64 e x86_64 iguais):

| Quadro | software x Dolphin | GPU x software | GPU x Dolphin |
| --- | --- | --- | --- |
| q1600 filme | 34,96 dB | 36,91 dB | 39,44 dB |
| q1800 "PRESS START" | 40,64 dB | 44,56 dB | 43,34 dB |
| q2300 menu principal | 35,68 dB | 39,21 dB | 39,95 dB |
| q6400 largada | 29,38 dB | 30,65 dB | 34,92 dB |

Tempo emulado: defasagem de 2,096 campos no q6400, 0,000 no q1, arquivo de timebase idêntico nos dois builds. O
oráculo rodado de novo nesta verificação deu os mesmos 551 eventos e o mesmo timebase do M7 (o Dolphin é
determinístico). Testes: 49 no ctest de cada build, todos passando (novos: `profile`, `gx_gpu` e casos em `ppc_sys`, `timebase`, `boot`).

## 6. Goldens anteriores

O tempo emulado mudou (§2); os eventos estritos e a ordem continuam iguais. Regravados com `--update-golden`, todos
para mais perto do Dolphin:

- `m5_events.json`: das 441 leituras do fluxo ADX (música), 441 idênticas ao Dolphin na mesma posição (eram 231); os
  103 eventos estritos têm o mesmo SHA-256.
- `m7_menus.json`: áudio sem atraso contra o Dolphin (0 ms, era 8,2 ms) e correlação ok em 100% das janelas (era
  93,2%); q3200 de 32,76 para 36,23 dB e q3500 de 31,43 para 31,94 dB; q1600–q2880 iguais.
- `m4`: o estado inicial passa a explicar TB e DEC (a runtime começa com o tempo do BS2; o GDB do Dolphin mostra o
  SPR ainda não lido). O golden do trace não mudou. `m6`: igual.

## 7. Reproduzir

```sh
scripts/verify_m8.sh
scripts/assistir.sh jogar                  # janela com a GPU (AM_GFX=soft volta ao software)
scripts/build_shaders.sh                   # só ao mudar o .metal: regenera o metallib e o header

# estudo do tempo emulado
automodellista --sys <sys> --frames 6400 --pad-script "<corrida>" --gfx off --timebase-out rt_tb.txt \
    --profile 6150:6191:perfil.txt --probes 5889:8003BE18,800D1458
python3 -I scripts/dolphin_trace.py --iso <rvz> events --pad-script "<corrida>" --frames 6400 \
    --timebase-out d_tb.txt --probes 5889:8003BE18,800D1458 --out d.txt
python3 -I scripts/compare_timebase.py rt_tb.txt d_tb.txt
```

Logs, capturas e traces em `/Volumes/AMRecomp/logs/m8/` (fora do git).

## 8. Parte 2: o que era pedido e onde está

- Cel-shading da corrida conferido contra o Dolphin, na GPU e no software: §12.4 (o jogo não usa cópia EFB ->
  textura nem textura de Z na corrida; o contorno é geometria).
- Neblina e textura de Z da pista em movimento: neblina em 98,6% dos desenhos da corrida, conferida pelas capturas
  (§12.4); textura de Z: 0 desenhos.
- Memory card (CARD): save gravado e lido, §12.3.
- 60 quadros/s desenhados na corrida: thread do GX (§11) sem esperar no DrawDone (§12.1).
- Os 2 campos restantes do tempo emulado (§2): §12.2.

## 9. Pendências e o que não foi verificado

- Vulkan/D3D12 e SPIR-V/DXIL do ubershader (§3); a GPU só funciona com Metal em GPU da Apple.
- Estágios indiretos e zfreeze na GPU (contados; o jogo não usa até a largada).
- A GPU não é determinística entre computadores: o golden guarda só o "passou" dela.
- Builds Debug e CI Linux/Windows sem execução do jogo (herdado).
- Parte 2b: x86_64 (Rosetta) desenha todos os quadros na corrida, mas o jogo anda a ~48/s (limite da CPU emulada);
  memory card sem `.gci`, slot B, troca a quente e gravação periódica (§12.3); cel-shading não conferido em replay,
  garagem, outras pistas, chuva/noite e câmera interna (§12.4).

## 10. Lições

- **O oráculo também tem jeito de medir.** O timebase que o GDB do Dolphin mostra é o do último `mftb`; comparar com o
  tempo exato da runtime escondia meio campo de diferença no começo e confundia a leitura dos degraus.
- **O que custa 0 se esconde.** A diferença de 1,37 s vinha de um punhado de instruções que o recompilador manda para
  o interpretador e que não entravam na conta de ciclos; o perfil por trecho do C gerado mostrou o `DCFlushRange` com
  2 ciclos por iteração, e o resto foi conta.
- **Antes do entry point também passa tempo.** O apploader rodando na CPU emulada do Dolphin deixa o VI numa fase que
  a runtime não tinha; acertar isso zerou a defasagem dos primeiros 5889 quadros.
- **Um shader que é o software.** Com o EFB em inteiros lido e escrito pelo próprio shader, o TEV e o Pixel Engine da
  GPU são a mesma aritmética do backend em software; a diferença que sobra é de rasterização, e a GPU fica mais perto
  do Dolphin (que também desenha numa GPU) do que o software.
- **O gargalo muda de lugar.** Tirar o pixel da CPU revelou um cache de texturas que decodificava a mesma textura a
  cada quadro por causa de uma página de 4 KiB vizinha; o hash do conteúdo levou 87 mil decodificações para 264.

## 11. Parte 2a: o GX numa thread própria

Estado: **thread do GX feita e determinística, sem ganho de quadros/s**; fica atrás de `--gx-thread on` (padrão
off). O objetivo (60 desenhados na corrida) não foi atingido; o motivo foi medido e está abaixo.

### Como funciona

Como o "dual core" do Dolphin, mas com os eventos no mesmo ponto do tempo emulado de antes (`gfx/gx_fifo.c`):

- a thread da CPU emulada lê o fluxo **só por cima**: opcode e cabeçalho de cada comando, uma cópia dos registradores
  do CP que dão o tamanho do vértice (VCD e VAT, `gx_vtx_size_of`), uma cópia dos registradores do BP (com a máscara)
  e as display lists, pulando vértices e palavras de XF sem lê-los;
- os bytes vão para uma fila de blocos de 32 KiB (até 1 MiB em voo) que a thread do GX consome com o leitor completo
  de antes: vertex loader, XF, montagem, rasterização ou GPU, cópias do EFB. O pulo de quadro anda pela fila junto
  com os bytes;
- **DrawDone e tokens** são disparados pela thread da CPU no mesmo byte de antes, depois de esperar o GX terminar tudo o
  que veio antes deles. O tempo emulado não muda (o evento sai na mesma instrução), e quando o jogo reaproveita memória
  que o GX lê, o que só faz depois desses eventos, o GX já a leu;
- quem lê o que o GX escreveu fora desses pontos (o XFB da janela e das capturas, as estatísticas) sincroniza antes.

Conferido (`verify_m8.sh`, arm64 e x86_64, passou; ctest 49/49; golden igual, sem regravar): com `--gx-thread on` as capturas da GPU e do software e o arquivo de timebase até o q6400 são **idênticos
byte a byte** aos de `--gx-thread off` (`verify_m8.sh` passou a conferir isso para a GPU nos dois builds).

### Por que não ganhou

O log da thread (`GX thread: ... espera de N ms no DrawDone`) mostra para onde foi o tempo. Sem janela, desenhando
todos os quadros até o q6600 (arm64): 19 804 sincronizações (3 por quadro), 14 170 com o GX ainda trabalhando, e
**46,7 s esperando no DrawDone** contra 0,07 s nos tokens. O jogo manda o quadro quase todo em display lists (para a
CPU, um `CALL_DL` é barato; o trabalho fica no GX) e logo em seguida pede o DrawDone e espera por ele. A CPU fica
parada esperando o GX: na execução do `verify_m8.sh` (sozinha, até o q6400) foram 27,2 s no DrawDone em arm64 e
36,0 s em x86_64, contra 85 s e 123 s de execução. A sobreposição é pequena e os números não se mexem:

| Medição (arm64) | `--gx-thread off` | `--gx-thread on` |
| --- | --- | --- |
| sem janela, todos os quadros desenhados, menu / corrida (duas execuções em paralelo) | 47,4 / 41,4 | 47,5 / 41,1 |
| janela (medição do `verify_m8.sh`), menu 2160–2400 | 60,9 / 60,0 (§4) | 59,8 / 55,0 |
| janela, corrida 6240–6600 | 52,8 / 30,8 (§4) | 51,8 / 30,8 |

Por isso o padrão continua off: ligado, a corrida fica igual e o menu, uma execução, saiu um pouco abaixo.

### O que falta para ganhar (parte 2b)

Para o GX andar junto com a lógica do quadro seguinte, a CPU não pode esperar no DrawDone, e então o GX passa a ler
memória que a CPU já pode estar reescrevendo (matrizes carregadas por índice, vértices dinâmicos, display lists). Os
caminhos possíveis, do mais simples ao mais completo:

1. a thread da CPU tira na hora as cópias do que o GX vai ler da memória (as cargas indexadas do XF, as display
   lists e os elementos dos arrays de vértices pelos índices) e as manda na fila, como o Dolphin faz com a FIFO;
   texturas continuam lidas pelo GX, protegidas pelo contador de escritas por página;
2. cópia na escrita: depois de um DrawDone sem espera, a primeira escrita da CPU numa página copia a página antiga
   para o GX (exige um gancho no caminho de escrita e cuidado com a ordem de memória entre as threads).

A sincronização pela fila, a leitura por cima e a conferência byte a byte já estão prontas para qualquer um dos dois.

### Lições

- **Medir onde a thread espera antes de otimizar.** Três sincronizações por quadro pareciam pouco; o tempo delas é
  que conta, e quase tudo estava num único ponto, o DrawDone.
- **Determinismo de graça vem do lugar do evento.** Disparar DrawDone e tokens na thread da CPU, no mesmo byte, deixou
  tempo emulado e imagens idênticos sem nenhum relógio de GPU emulado; o preço é esperar o GX nesse ponto.

## 12. Parte 2b: 60 quadros/s, tempo exato, memory card e cel-shading

Feita em quatro peças paralelas, cada uma num worktree e num branch, juntadas depois em `master`. Cada peça tem o seu
relatório completo (como foi achado, medições, hipóteses descartadas, lições); aqui fica o resumo e o resultado da
verificação depois de juntar.

| Peça | Relatório | Resultado |
| --- | --- | --- |
| GX sem esperar no DrawDone | `docs/M8_PARTE2B_GX.md` | corrida na janela a 60 desenhados (arm64); thread do GX ligada por padrão com janela e GPU |
| Tempo emulado | `docs/M8_PARTE2B_TEMPO.md` | defasagem no q6400 de 2,096 para 0,000 campo; patch no DolRecomp |
| Memory card | `docs/M8_PARTE2B_CARD.md` | salvar e ler pelo jogo; o `.raw` gravado é igual byte a byte ao do Dolphin |
| Cel-shading | `docs/M8_PARTE2B_CEL.md` | contorno e sombreado conferidos com o carro andando até o q8400, sem nenhuma mudança na runtime |

### 12.1 GX sem esperar no DrawDone

A parte 2a esperava o GX em cada DrawDone e token. Agora cada evento que o jogo enxerga fecha uma **época** do fluxo
do GX: a CPU dispara o evento no mesmo byte (o tempo emulado não muda) e só espera a época de `--gx-depth` eventos
atrás (padrão 3: a corrida tem três eventos por quadro, então o GX pode estar um quadro inteiro atrás; 2 é pior que 0).
Como a CPU já reescreve memória que o GX ainda vai ler, há **cópia na escrita** em `core/mem.c`: a primeira escrita da
CPU numa página de 4 KiB em cada época guarda antes a página antiga, e o GX, pelo `mem_gx_read`, lê sempre a memória
do fim da sua época. Todas as escritas passam pelo gancho (`mem_write*`, o diário do C gerado, DMA do DVD, da ARAM e,
ao juntar as peças, a DMA de leitura do memory card). A janela mostra a última imagem pronta sem esperar o GX; o pulo
de quadros com a thread só pula se a CPU esperou o GX; a thread do GX tem QoS interativa no macOS. Determinístico por
construção: capturas e timebase com `--gx-thread on` idênticos byte a byte aos de `off`, nos dois builds.

### 12.2 Tempo emulado: o laço contado do DolRecomp

Sondas com o timebase exato no Dolphin (`dolphin_trace.py --probes-exact`: um `mftb` injetado num trecho na memória do
jogo) mostraram todas as funções da carga da pista com o mesmo tempo, tique a tique, menos o `memmove` do SDK: 4 ciclos
por byte no Dolphin, 2 na runtime. O laço tem 4 instruções em dois blocos (o código anterior entra pelo meio), e o
laço contado do backend C cobrava só o primeiro. O patch `patches/dolrecomp/0001-ciclos-do-laco-contado.patch`
(aplicado pelo `scripts/fetch_tools.sh`) cobra todos os blocos do corpo: mudam só linhas `ctx->downcount -= N`, 130
laços no DOL e 33 nos RELs. A constante do BS2 (§2), que tinha absorvido o memcpy barato do boot, foi remedida:
199 532 tiques (era 200 059).

### 12.3 Memory card

O SDK do jogo (CARD, EXI) é recompilado; a runtime responde pelo EXI como o `CEXIMemoryCard` do Dolphin 2609a
(`hw/exi_card.c`, `hw/memcard.c`): imagem `.raw` do Dolphin, cartão novo formatado como o `GCMemcard::Format` (4 Mbit,
59 blocos, data do RTC emulado), comandos, e o `TCINT` atrasado pela taxa de leitura/escrita do Dolphin, o que mantém o
tempo emulado igual. `--memcard <arq.raw|none>`; no modo interativo o padrão é `MemoryCardA.raw` no diretório de dados
do usuário, nos roteiros é sem cartão (os goldens de M5 a M8 não mudam por isso). O jogo cria o System File no boot,
salva e lê pelo menu save/load; o `.raw` depois de salvar é **igual byte a byte** ao que o Dolphin grava com o mesmo
roteiro, com defasagem de tempo 0,000 em todos os 4300 quadros. `scripts/verify_card.sh`, golden
`tests/golden/m8_card.json`.

### 12.4 Cel-shading com o carro andando

`verify_m8.sh --cel` estende o roteiro até o q8400 (acelera em 6900, vira em 7700: reta, derrapagem com fumaça, carro
de lado, rodada e muro) e compara 7 quadros, software e GPU, contra o Dolphin. A hipótese do M6 se confirma: na
corrida o jogo não copia o EFB para textura, não usa textura de Z, TEV indireto nem zfreeze; o contorno vem de
geometria (34% dos triângulos descartados pela face, o "casco invertido") e o sombreado, de rampa e neblina (98,6% dos
desenhos). Os mapas de diferença não mostram borda de contorno, só ruído de textura fina.

### 12.5 Verificação depois de juntar as peças

Os quatro branches entraram em `master` com merge normal. Um conflito só, em `runtime/main.c` (as opções
`--gx-depth` e `--memcard` no mesmo ponto do analisador; ficaram as duas). Dois ajustes na junção: a DMA de leitura do
memory card passou a marcar a página **antes** de copiar (a cópia na escrita do GX precisa ver a página antiga), e o
`--cel` do `verify_m8.sh` nomeia as capturas com `${f}` (no zsh, `"$f:cel/..."` aplicava o modificador `:c` e
estragava o nome; o bloco nunca tinha rodado de ponta a ponta).

O C compartilhado (`/Volumes/AMRecomp/recomp/dol` e `recomp/m5/rels`) foi regenerado com o patch do tempo
(`fetch_tools.sh`, `build_dolrecomp.sh`, `regen_generated.sh`); o anterior ficou em `dol.pre2b` e `m5/rels.pre2b`. O
novo é idêntico ao da peça do tempo e difere do anterior só em 163 linhas de `downcount`.

Resultado (Apple M4, Release, arm64 e x86_64; logs em `/Volumes/AMRecomp/logs/m8/integracao-2b/`):

| Verificação | Resultado |
| --- | --- |
| ctest | 50/50 nos dois builds |
| `verify_m8.sh --oracle` | Dolphin rodado de novo: os mesmos 551 eventos. Defasagem 0,000 campo no q6400 e no q1, nenhum degrau, timebase idêntico nos dois builds; `--gx-thread on` idêntico byte a byte a `off` (espera da CPU no DrawDone até o q6400: 6,0 s arm64, 3,1 s x86_64). Capturas iguais às da parte 1: software x Dolphin 34,96 / 40,64 / 35,68 / 29,38 dB; GPU x software 36,91 / 44,56 / 39,21 / 30,65; GPU x Dolphin 39,44 / 43,34 / 39,95 / 34,92 |
| golden `m8_corrida.json` | só o bloco `timebase` mudou, como a peça do tempo previu: `lag_fields` 2,096 -> 0,0 e `max_abs_lag_fields` 2,096 -> 0,038; regravado e conferido de novo sem `--update-golden` |
| janela, arm64 (máquina livre) | menu 60,1 / 60,0 e 60,2 / 60,0; corrida **59,4 / 59,7** e **59,7 / 59,3** (duas execuções) |
| janela, x86_64 | menu 59,9 / 60,0; corrida 48,1 / 60,0 (as duas execuções) |
| `verify_m8.sh --cel` (Dolphin rodado de novo, 601 eventos até o q8400) | passou; os mesmos PSNR e contadores do GX da peça cel (GPU x Dolphin 35,33–37,59 dB, software x Dolphin 31,06–32,77 dB) |
| `verify_card.sh --oracle` | passou nos dois builds; o `.raw` do Dolphin com o mesmo roteiro: 0 bytes diferentes; golden `m8_card.json` igual |
| `verify_m7.sh`, `verify_m6.sh`, `verify_m5.sh` | passaram com os goldens **sem regravar**: o memmove agora custa o certo desde o boot, mas os 551 eventos, as 441 leituras do ADX na mesma posição, as capturas (q1100–q3500) e o áudio (defasagem 0 ms, correlação 100%) ficaram iguais |

Lições da junção:

- **Um gancho novo vale para quem chega depois.** A cópia na escrita do GX exige que toda escrita na memória passe por
  ele antes; a peça do cartão, feita ao mesmo tempo, tinha uma DMA que marcava a página depois de copiar. Só a leitura
  das duas peças juntas mostrou isso; nenhum teste de uma peça sozinha pegaria.
- **Um bloco de script que nunca rodou não está verificado.** O `--cel` tinha a mesma armadilha do zsh que a própria
  peça registrou como lição, num nome que ela não usava.
