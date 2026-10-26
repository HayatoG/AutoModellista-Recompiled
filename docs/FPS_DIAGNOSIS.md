# Diagnóstico de FPS no Windows (M9 parte 6)

Estado em 26/10/2026, PC com Windows 11, Ryzen 7 5700X3D (8 núcleos, 16 lógicos) e RTX 2060, Direct3D 12. Os
números vêm da corrida automática (`work/run-perf/corrida.sh`: 8.400 quadros, roteiro de botões fixo), no trecho
6.900–8.400, o mais pesado, com o carro andando atrás dos adversários. Experimentos um a um em
[EXPERIMENT_RESULTS.md](EXPERIMENT_RESULTS.md).

## Resumo

O jogo roda a lógica a 60 Hz (um quadro por campo de VI) e o tempo é todo emulado. Não há limite de 30 quadros nem
espera de VI que segure o FPS: quando o PC não acompanha, o jogo inteiro (lógica, áudio e imagem) fica mais lento que
o relógio, o frameskip pula desenhos para recuperar e a janela repete a última imagem. A causa é a vazão do host em
duas threads quentes:

- a **thread do GX**, que faz na CPU, a cada um dos ~2.400 desenhos por quadro da corrida, o que o Dolphin e o Aurora
  fazem na GPU ou só quando um registrador muda (transformação e iluminação de vértices, decodificação, texturas,
  estado do TEV), e espera a GPU a cada cópia do EFB;
- a **CPU emulada**, que sozinha já passa de 100 quadros/s, mas cai para ~65 quando divide o processador com o GX.

## Fase 1: como o jogo e o runtime controlam o tempo

| Item | Onde | Evidência |
| --- | --- | --- |
| Recompilador | DolRecomp (C), patches em `patches/dolrecomp/` | 84 trechos de 16 KB do DOL e 12 RELs |
| Despacho | `chassis_run`, `runtime/core/chassis.c:120` | uma volta por fatia de código traduzido |
| Relógio emulado | 486 MHz, `GAME_RUN_CPU_HZ`, `runtime/game/game_run.c:53` | VI, DSP/áudio, DVD e decrementer contam ciclos emulados |
| Laço principal | fn_8004838C chama fn_80051034 (um quadro) em 0x80048398 | `runtime/include/am/game.h:34–38` |
| VIWaitForRetrace | 0x800D276C | `analysis/known_addresses.md:35` (Ghidra/REA no Mac) |
| VIFlush, VISetNextFrameBuffer | 0x800D345C, 0x800D358C | `analysis/known_addresses.md:36–38` |
| Frequência da lógica | 60 Hz | corrida: 8.400 quadros em 8.687 campos de VI (log `v6_off.log`) |
| Ritmo em tempo real | `pace()`, `game_run.c:271` | dorme com a emulação > 60 ms adiantada; realinha com > 250 ms de atraso |
| Frameskip | `decide_skip()`, `game_run.c:295` | não desenha com > 16,7 ms de atraso e GX atrasado; com a GPU, no máximo 1 seguido |
| Contador "desenhados" | `game_run.c` (linha de progresso) | 60 menos os pulados: não vê imagem repetida na janela |
| Apresentação | `capture_frame` → `platform_window_present`, na thread da CPU emulada | a janela mostra a última conversão pronta do XFB, sem esperar o GX |
| Vsync | desligado por padrão (`main.c`, `--vsync`) | IMMEDIATE ou MAILBOX (`platform_present_gpu.c:58–66`) |
| Áudio | DMA do DSP no tempo emulado | atrasos de áudio = emulação atrás do relógio (`pace_rebases`) |
| Stubs | vetores de exceção (`os/exception.c:92`); `am_fatal` com TODO em CP, DI, EXI | nenhum é usado na corrida |

Não identificados ainda: VIGetRetraceCount, VISetPostRetraceCallback, GXDrawDone, OSGetTime, OSGetTick, OSSetAlarm.
O projeto Ghidra está no volume do Mac (`/Volumes/AMRecomp`); este PC não tem Ghidra.

## Fase 2: instrumentação

`--frame-trace [q0:q1:]arq.csv` (`runtime/core/frame_trace.c`): uma linha por quadro, gravada no fim, e relatório com
P50/P95/P99 em `arq.csv.txt`. Colunas: relógio, ciclos, campos de VI, pulado, janela atualizada, imagem nova, tempo na
apresentação, sono do ritmo, CPU esperando o GX (DrawDone, tokens, leituras, fila cheia), GX ocupado, espera pela GPU
nas leituras do EFB, páginas copiadas. Desligado, custa um teste por quadro.

Corrida com a GPU sem janela (`--gfx gpu --gx-thread on --gx-depth 6`), build `a818e28`, quadros 6.900–8.399:

| Medida | P50 | P95 | P99 | Máx |
| --- | --- | --- | --- | --- |
| Quadro (relógio) | 16,45 ms | 25,68 ms | 28,21 ms | 32,76 ms |
| GX ocupado | 16,36 ms | 26,01 ms | 28,88 ms | 32,95 ms |
| CPU emulada sem esperas | 15,39 ms | 21,14 ms | 23,96 ms | 28,24 ms |
| CPU esperando o GX | 0,00 ms | 7,38 ms | 9,62 ms | 12,21 ms |
| Espera pela GPU (EFB) | 1,39 ms | 3,32 ms | 5,16 ms | 11,54 ms |

Para 60 quadros visuais únicos por segundo, o GX e a CPU emulada precisam ficar abaixo de 16,7 ms no P95, e hoje
ficam em 26 e 21 ms.

## Perfis que levaram a isto

- Thread do GX (amostragem, trecho pesado): 66% código nosso, 13% esperas, 10% driver da NVIDIA, 5% memcpy. No nosso
  código: leitura de vértices pela visão da memória do GX (`read_bytes`, 10%), `gx_xf_transform` (7%),
  `gx_vtx_decode` (7%), preparação de texturas (8%), `transform_channel` (5%), estado do TEV e do rasterizador (6%).
- A corrida tem 20,1 milhões de desenhos em 8.400 quadros (~2.400 por quadro) com 88,7 milhões de vértices (~4 por
  desenho): o custo é fixo por desenho.

## O que falta (ordem proposta)

1. Leitura de vértices sem a cópia por atributo (`read_bytes`) e decodificação por formato.
2. Transformação e iluminação na GPU (vértices crus e matrizes como constantes), como o Dolphin e o Aurora.
3. Cópias do EFB sem esperar a GPU a cada uma (resolve na GPU, leitura só se a CPU precisar).
4. Apresentação fora da thread da CPU emulada e com o ritmo do VI.
