# Roadmap: alta taxa de quadros, resoluções, ultrawide e melhorias

Planejamento de 27/10/2026, feito de leitura de código e de análise do `main.dol` no Ghidra (nada foi implementado nem
medido para este documento). Cada afirmação traz o endereço do Ghidra (`FUN_XXXXXXXX` = `fn_XXXXXXXX` do runtime) ou o
arquivo do port, e vem marcada **CONFIRMADO**, **INFERIDO** ou **NÃO VERIFICADO**.

## 1. O que o jogo original faz (Ghidra)

- **A lógica avança a passo fixo, uma vez por iteração do laço, sem delta de tempo (CONFIRMADO).** O laço real é
  `FUN_800719ac` (entrada, despacho de modo por `FUN_8003b2d0`, `FUN_8004838c` que fecha o quadro). `FUN_80051034` é só
  o fim de quadro (cópia, espera e troca de buffer), não a lógica. Não há 1/60 em ponto flutuante no DOL, e as rotinas
  de tempo contam **quadros**: contadores de `80007f58`, desvanecimentos de `80099164`, repetição de tecla em 30 e 4
  quadros, timers de 600 quadros.
- **O replay depende desse passo fixo (INFERIDO):** `FUN_80034b70` guarda um anel de 32 amostras de controle por porta,
  uma por quadro, e há a string `Replay.RD`.
- **O relógio (`mftb`) não aparece no caminho de entrada, lógica, câmera nem render (INFERIDO);** só no SDK, no áudio e
  no cartão. Os cronômetros 6 e 7 do jogo são stubs de `blr` (código morto).
- **O alvo de retraços por quadro (`DAT_80225de5`) vale 0 e só é escrito na inicialização (CONFIRMADO)** (limitado a 3);
  nenhuma cena escolhe 30 fps. Só `FUN_8001f934` (~60 chamadores) o usa, para deslocar durações.
- **Dois XFB (CONFIRMADO):** `DAT_8022a8f8` e `DAT_8022a910`. O quadro só é "completo" para o jogo quando o token do
  GX bate; o flip acontece no callback do retraço (`FUN_8004faa8`).
- **Projeção (CONFIRMADO):** há **uma** função de perspectiva, `FUN_80056bd4(near, far, fov, aspect, slot)`, que divide o
  elemento [0] da matriz por `aspect`. As câmeras buscam o aspecto numa tabela em `80150428` (passo 0x20; o tipo 0 é
  viewport 0,0,640,448, fov 45, aspecto 4/3). Telas divididas usam 2/3 e 8/3. O viewport e o scissor passam por
  `FUN_800567f0`, que limita a largura e a altura a 640,0 e 448,0 (globais em `r13-0x7908/-0x790c`). O HUD usa projeção
  ortográfica (`FUN_800cd6cc`) com extentos 640x448 gravados por `FUN_80048bd8(0x280,0x1c0)`.
- **Culling (NÃO VERIFICADO):** não achei teste de frustum no caminho de desenho; cada malha é emitida direto. Pode
  haver visibilidade por setor da pista que não localizei. Isso decide se o ultrawide terá pop-in lateral.
- **Resolução e modo (CONFIRMADO):** 640x448 de EFB e XFB, 480i com deflicker de 7 taps, sem AA, 4:3 fixo; 480p
  opcional por uma tela Sim/Não no boot; nenhuma constante de 16:9.

## 2. O que o port tem hoje (código)

- O **T&L é feito na CPU, na thread do GX** (`runtime/gfx/gx_xf.c`, `gx_xf_transform` e `gx_xf_perspective_divide`; usados
  por `gx_setup.c`): é a razão do custo de ~15 ms por quadro e também o gancho natural para ultrawide e para
  interpolação no nível dos vértices.
- Ritmo em `game_run.c` (`pace()`), apresentação ligada à lógica e não ao vblank do monitor; blit final em
  `platform_present_gpu.c` (nearest, linear e sharp já implementados).
- EFB em duas texturas `R32_UINT` com ROV: MSAA de hardware é inviável; a resolução interna é supersampling.
- Não há compilador de shader no Windows; o DXC v1.9.2609 existe no GitHub e os scripts do repositório são zsh/Mac.

## 3. 120/144 Hz

A lógica é 60 Hz por construção, então **dobrar a simulação está descartado** (replay, timers, repetição de tecla e fades
quebram, e a física muda de velocidade). O ganho possível é de **apresentação**:

1. **Fase 1, apresentar por vblank do monitor, sem interpolar** (esforço P, risco zero para o golden): melhora a
   regularidade e a latência; a 144 Hz o padrão 2-3-2-3 é inevitável sem interpolar. Marco: P95 do intervalo de
   apresentação ≤ 1,1× o período do monitor (o `--frame-trace` já mede o intervalo entre imagens; falta contar
   apresentações e repetições).
2. **Fase 2, cortar o custo do GX** de ~15 ms para ≤ 7 ms, que é o que renderizar a 120 Hz de verdade exigiria: menos
   trabalho por desenho no setup, T&L no vertex shader (precisa de shader novo), resolve do EFB na GPU, e perfil antes
   de decidir (não se sabe a fração de cada parte).
3. **Fase 3, interpolação:** por vértices (render interpolation), viável porque a lógica é de passo fixo, mas com
   identidade de objeto a resolver por hash e HUD sem interpolar (esforço G, risco alto); ou por pós-processamento do
   XFB (fluxo óptico, experimental, precisa de shaders).

## 4. Resoluções

- Fator interno **inteiro** N = altura de saída ÷ 448 arredondada para cima, entre 1 e 4 (1080p dá 3×, 1440p e 4K dão 4×);
  a parte fracionária fica no blit final. O `--internal-res` já existe na etapa 1a (reduz para nativo na CPU) e a 1b
  apresenta a imagem grande direto (branch do worktree, ainda a mesclar).
- Custo cresce com N² mais o ROV: 2× deve caber; 3× e 4× provavelmente não no trecho pesado enquanto a redução for na
  CPU. Caminho: resolve na GPU com shader do DXC, ou renderizar 2× e usar FSR1/CAS no blit.
- Contornos do cel-shading: **NÃO VERIFICADO** se são geometria de largura fixa em pixels (ficariam finos com N).

## 5. Ultrawide e 16:9

- **Fácil:** a perspectiva passa por um ponto só. Multiplicar `aspect` em `FUN_80056bd4` por (largura/448)/(4/3), ou
  interceptar o carregamento da projeção (`FUN_800ee214`, XF 0x1020) quando o tipo de câmera é 0.
- **Não fácil:** viewport e scissor (`FUN_800567f0`), o EFB mais largo que 640 (a textura do port é parametrizável), a
  cópia para o XFB, e o HUD, que está ancorado em 0..640 × 0..448 e precisa ser centralizado sem esticar.
- Ordem: (i) barras com cenário espelhado/borrado (só no blit, sem ganho de campo de visão); (ii) FOV real (Hor+) no
  T&L do port ou em `FUN_80056bd4`; (iii) corte vertical. Risco: pop-in lateral se houver culling por setor (a verificar);
  32:9 provavelmente inviável sem patch por função.

## 6. Outras melhorias

| Item | Valor | Esforço | Risco (golden) | Depende de |
| --- | --- | --- | --- | --- |
| Toolchain DXC e script `.ps1` de shader | alto | P/M | baixo | DXC |
| Apresentação por vblank e métricas | alto | P | baixo | nada |
| Mesclar `--internal-res` e opção no launcher | alto | M | baixo | worktree |
| Custo do GX (perfil, setup, depois T&L no vertex shader) | muito alto | M/G | baixo/médio | perfil |
| Resolve do EFB na GPU | alto | M | médio | DXC |
| Ultrawide (i) barras | médio | P | baixo | nada |
| Ultrawide (ii) FOV real | alto | G | médio | verificar culling |
| Vibração, gatilhos, zona morta, remapeamento | médio | P/M | baixo | launcher |
| CAS/FSR1/sharpen no blit | médio | M | baixo | DXC |
| Texturas HD por hash (cache de texturas do GX) | alto | G | baixo | pacote |
| Captura de tela/vídeo na UI | médio | P | baixo | readback |
| Savestates | alto | G | médio | não investigado |
| 120 Hz por render interpolation | alto | G | alto | custo do GX |

## 7. Plano

- **Amanhã (modo BUILD, sem janela):** script do DXC (compilar o HLSL atual para DXIL e comparar); contador de
  apresentações e repetições no `--frame-trace`; mesclar `--internal-res` com política Auto por saída.
- **Esta semana:** pacing por vblank com bloco curto de janela para medir a base a 144 Hz; ultrawide (i); vibração e
  remapeamento; medir o 2× e o custo do GX com perfil.
- **Depois:** resolve na GPU, CAS/FSR1, ultrawide (ii), T&L no vertex shader, render interpolation, texturas HD e
  savestates.

## 8. Não verificado

Qual modo de jogo é a corrida e onde está a física dos carros; os emissores de fumaça e partículas; o culling por setor;
a largura dos contornos do cel-shading; a fração de cada parte do custo do GX; o comportamento de DPI e tela cheia em
`platform_window.c`; a viabilidade de savestates; se o `sleep` de `pace()` interfere num vsync a 144 Hz.
