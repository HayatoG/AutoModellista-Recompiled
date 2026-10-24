# M8 parte 2b (peça gx): a CPU não espera o GX

Estado: **atingida no arm64**. Com `--gfx gpu` na janela, a corrida desenha 60 quadros/s (60,0 do jogo / 59,8
desenhados, medição do `verify_m8.sh`, q6240–6600); antes eram 51,4 / 31,0 na mesma máquina e na mesma hora. No
x86_64 (Rosetta) todos os quadros são desenhados, mas o jogo anda a 48,7/s: lá o limite é a CPU emulada. A thread do
GX passa a ser o padrão com janela e GPU. Capturas da GPU e timebase até o q6400 continuam **idênticos byte a byte**
aos de `--gx-thread off`, nos dois builds.

Continuação de `docs/M8_CORRIDA.md` §11 (parte 2a: o GX numa thread própria, sem ganho porque a CPU esperava o GX
no DrawDone).

## 1. Resultado

Apple M4, Release, janela em tempo real com pulo de quadros, sem som; "quadros/s do jogo / desenhados" como no
`verify_m8.sh`. As duas colunas de cada linha foram medidas uma logo depois da outra, com a máquina sem as outras
peças rodando (com elas rodando, os números caem até 20% e variam de uma execução para outra; ver §5).

| Trecho | `--gx-thread off` | thread, `--gx-depth 3` (padrão novo) |
| --- | --- | --- |
| arm64, menu principal (q2160–2400) | 60,0 / 57,5 | **60,2 / 60,0** |
| arm64, corrida (q6240–6600) | 51,4 / 31,0 | **60,0 / 59,8** |
| x86_64, menu principal | 59,0 / 30,0 | **59,9 / 60,0** |
| x86_64, corrida | 38,1 / 31,5 | **48,7 / 60,0** |

Sem janela, até a largada (q6400) com a thread: 71 s no arm64 (eram 85 s sem ela). A CPU emulada sozinha
(`--gfx off`, sem janela) faz a corrida a 75,9 quadros/s no arm64 (13 ms por quadro): no arm64 sobra folga, no
x86_64 não.

## 2. Por que a parte 2a não ganhava, medido

Com um log de cada evento (DrawDone e tokens, com o relógio, o tempo ocupado da thread do GX e a espera da CPU), a
corrida tem **três eventos por quadro**: DrawDone, um token logo em seguida (no mesmo byte) e outro token. O
experimento "sem esperar no DrawDone" (sem cópia, só para medir) só passou a espera para o token: 38,6 s nos tokens em
vez de 33,6 s no DrawDone. Sem esperar em nenhum dos dois (corrida de dados, fora do determinismo), a corrida sem
janela chegou perto do limite da CPU emulada: a sobreposição existe, desde que nenhum dos três eventos espere.

## 3. Como funciona

### Épocas

Cada evento que o jogo enxerga fecha uma **época** do fluxo do GX. A CPU dispara o evento no mesmo byte de antes (o
tempo emulado não muda) e só espera o GX terminar a época de `--gx-depth` eventos atrás. Profundidade medida na
corrida (sem janela, q6300–6600, máquina com as outras peças rodando):

| `--gx-depth` | ms por quadro | GX ocupado | CPU esperando |
| --- | --- | --- | --- |
| 0 (= parte 2a) | 26,9 | 11,3 | 10,9 |
| 2 | 35,8 | 13,8 | 13,5 |
| 3 | **16,9** | 15,8 | 0,3 |
| 6 | 23,9 | 23,9 | 7,5 |

Com 2, o terceiro evento de cada quadro espera a época do DrawDone, que é o quadro inteiro: nenhuma sobreposição, e
mais custo. Com 3, o GX pode estar um quadro inteiro atrás. Padrão: 3 (`--gx-depth 0..6`).

### Cópia na escrita (`core/mem.c`)

A CPU, já na época seguinte, reescreve memória que o GX ainda vai ler (matrizes, vértices, display lists, texturas).
Em vez de copiar na hora o que o GX vai ler (precisaria decodificar cada vértice indexado na thread da CPU), a
primeira escrita da CPU numa página de 4 KiB em cada época guarda **antes** o conteúdo da página numa de `depth`
vagas, marcada com a época. O GX, lendo a época e, usa a cópia mais antiga marcada depois de e; sem cópia, lê a
memória e confere depois de ler que nenhuma marca nova apareceu (a CPU marca, faz uma barreira e só então escreve).
Assim o GX sempre vê **a memória do fim da sua época**, qualquer que seja o andamento das duas threads: o resultado
é determinístico por construção, e não por sorte do escalonador.

- Todas as escritas da CPU passam pelo gancho: `mem_write*`, o diário do `cpu.c` do DolRecomp (chamado antes da
  escrita), a DMA do DVD (a marca agora vem antes do `memcpy`) e a da ARAM (`mem_write32`).
- Se o GX já terminou as épocas anteriores, a página só é marcada, sem cópia: na corrida são ~120 páginas copiadas por
  quadro (meio MiB).
- O que o GX lê passa por `mem_gx_read`: display lists, `LOAD_INDX` do XF, elementos dos arrays de vértices, cargas da
  TMEM/TLUT, texturas. As escritas do GX (cópias do EFB para o XFB e para textura) vão também para as cópias que
  épocas dele ainda vão ler, sob uma trava curta contra a cópia da página feita pela CPU ao mesmo tempo.
- Cache de texturas: o contador de escritas por página não vale para bytes da cópia da época, então a textura é
  conferida pelo hash; o resultado da conferência vale para a época inteira (sem isso, 40% do tempo do GX na corrida
  era hash da mesma textura a cada desenho).
- O software continua com profundidade 0: ele amostra texturas da memória durante o desenho, fora dessa visão.

### Janela, pulo de quadros e núcleos

- A janela não espera o GX: no início do quadro, a CPU pede pela fila que o GX converta o XFB quando chegar àquele
  ponto, e mostra a última imagem pronta (no máximo um quadro de atraso). As capturas `--shot` continuam sincronizando,
  por isso ficam iguais byte a byte.
- Pulo de quadros com a thread: só pula se a CPU esperou o GX pelo menos 2 ms no último quadro. Quando o atraso é da
  CPU emulada (x86_64), pular só tirava imagens da tela sem acelerar nada; agora todos os quadros são desenhados.
- No macOS, a thread do GX tinha QoS padrão e ia para os núcleos de eficiência quando disputava com a CPU emulada;
  agora é `QOS_CLASS_USER_INTERACTIVE` (`platform_thread_start`).

## 4. Conferência

- Capturas da GPU (q1600, q1800, q2300, q6400) e arquivo de timebase até o q6400 com `--gx-thread on --gx-depth 2` e
  `3`, arm64 e x86_64: idênticos byte a byte aos de `--gx-thread off` (da verificação da parte 2a). O
  `verify_m8.sh` já confere isso com `--gx-thread on`, que agora usa a profundidade 3 e a cópia na escrita.
- ctest: 49/49 nos dois builds, com dois testes novos em `mem_test.c` (o GX lê o fim da sua época; a escrita do GX
  chega à cópia que ele mesmo lê).
- `verify_m8.sh` completo (com janela, sem `--oracle`): **passou**. `--gx-thread on` idêntico byte a byte nos dois
  builds; espera da CPU pelo GX até o q6400 de 2,5 s no arm64 e 3,0 s no x86_64 (eram 27,2 s e 36,0 s na parte 2a);
  janela arm64 menu 60,1/60,0 e corrida **59,7/59,5**; x86_64 menu 61,0/60,0 e corrida 47,7/60,0. Testes 49/49.
- Golden `tests/golden/m8_corrida.json`: nada muda (tempo emulado, software e o "passou" da GPU iguais). Mudam só as
  linhas de quadros/s da janela no log do `verify_m8.sh`, que o golden não guarda.

## 5. Onde está o tempo agora

- **arm64**: CPU emulada ~13 ms por quadro na corrida; GX ~11–16 ms em paralelo. Perfil da thread da CPU (`sample`,
  corrida, janela): `mem_read32` 17%, `write_be` 12%, o resto no C gerado; a leitura do fluxo por cima e a fila
  ficam em ~6%.
- **x86_64 (Rosetta)**: a CPU emulada sozinha não chega a 60 na corrida; o jogo anda a 48,7/s com todos os quadros
  desenhados. Ganhar aqui é acelerar o acesso à memória do C gerado (chamada de função por load/store), fora desta peça.
- Medição com as outras três peças rodando ao mesmo tempo (4 núcleos de desempenho no M4): a CPU emulada sozinha cai
  para 52 quadros/s na corrida, e a mesma configuração variou de 43,7 a 56,2 entre execuções. Os números da §1 são da
  máquina livre.

## 6. Hipóteses descartadas

- **Copiar na thread da CPU o que o GX vai ler** (caminho 1 da parte 2a): obrigaria a decodificar os índices de cada
  vértice na thread da CPU, que é o gargalo; a cópia na escrita custa ~meio MiB por quadro e só na primeira escrita
  de cada página.
- **Profundidade 2**: pior que 0 (§3), pela estrutura de três eventos por quadro.
- **Profundidade maior que 3**: não ajudou na medição (mais páginas copiadas e o GX mais lento lendo cópias frias).

## 7. Fora desta peça

- Afinar o acesso à memória do C gerado para o x86_64 chegar a 60 na corrida.
- Os trabalhadores do rasterizador (`gx`) continuam com QoS padrão (só a conversão para o XFB os usa com a GPU).
- A visão por época supõe que o jogo não reescreve, dentro da mesma época, memória que já mandou o GX ler (a mesma
  suposição da parte 2a, conferida pelas capturas byte a byte).

## 8. Lições

- **O evento certo para medir é o que espera.** Tirar a espera do DrawDone só a mudou para o token seguinte; o
  quadro tem três eventos, e a profundidade útil é a de um quadro inteiro.
- **Determinismo pela visão, não pelo relógio.** O GX lê sempre a memória do fim da sua época; não importa quanto ele
  está atrás, a imagem é a mesma.
- **Uma otimização vira gargalo noutro lugar.** Com o GX atrás, o cache de texturas passou a conferir pelo hash a
  cada desenho; e a thread nova ia para os núcleos de eficiência até receber QoS interativa.
