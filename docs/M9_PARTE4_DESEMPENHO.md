# M9, parte 4: a corrida a 60 quadros/s

**Resultado:** no Apple M4 (arm64, Release), a corrida com a janela, GPU e som passou de **43–44** para **~60
quadros/s do jogo com 60 desenhados**. Na janela sem som (o `verify_m8.sh`), o x86_64 pela Rosetta passou de 48,7 para
52,3 quadros/s, com 59,7 desenhados. O `verify_m8.sh` passa nos dois builds com o golden igual: tempo emulado, capturas, e
`--gpu-shader hlsl` idêntico byte a byte.

## 1. O que estava errado na medição

O M8 media a "corrida" em q6240–6600, e esse trecho é a contagem antes da largada. Com o carro andando (q6660 em
diante, roteiro do `assistir.sh corrida`), a corrida caía para 43–44 quadros/s em qualquer configuração. Som, contador,
tela cheia e VSync deram o mesmo número. O tempo emulado estava certo (60 quadros por 60 campos de VI), então a
emulação é que andava mais devagar que o relógio. Sem desenhar nada (`--gfx off`, sem janela), a CPU emulada fazia só
~49 quadros/s, ou seja, 21 ms por quadro.

## 2. Perfil e mudanças

Perfil com o `sample` do macOS durante a corrida. Cerca de 30% do tempo da thread principal ia nos acessos à memória
emulada (`write_be`, `mem_read32`, `find_region`).

| Mudança | Onde | Corrida sem GX | Janela (GPU, som) |
| --- | --- | --- | --- |
| antes | — | ~49 | 43–44 |
| `-O2` no C gerado (era `-O1`) | `CMakeLists.txt` | 49,5: descartado | — |
| `load_be`/`store_be` com tamanho constante e `bswap` (always_inline) | `core/mem.c` | 52,1 | — |
| LTO no Release (`AM_LTO`, padrão ON) | `CMakeLists.txt` | 68,7 | 55–57 |
| rajada do gather pipe de uma vez; `flush` sem `memmove` vazio; atalho do pipe no `bus_write` | `hw/gather_pipe.c`, `hw/bus.c` | 77,7 | ~51 |
| cópia da textura pela época só quando o cache precisa dos bytes | `gfx/gx_tex.c` | — | **~60 / 60** |

- **LTO:** o C gerado chama `mem_read*` e `mem_write*` a cada load e store, e sem LTO cada um é uma chamada entre
  arquivos. Com o LTO, o caminho rápido da MEM1 entra em linha. O build Release leva ~2,5 min em vez de ~2.
- **O gargalo muda de lugar de novo:** com a CPU mais rápida, a janela piorou (51 / 43) porque o GX passou a limitar.
  A thread do GX gastava 41% do tempo em `gx_tex_prepare` → `read_bytes` → `memmove`. Quando a CPU escreve em qualquer
  página de uma textura (dados vizinhos, a cada quadro), a textura inteira era copiada da cópia na escrita **em todo
  desenho**, antes de olhar o cache. Agora a cópia só é tirada quando o cache precisa dos bytes, isto é, para conferir
  o hash ou decodificar. Dentro da mesma época, o acerto pela marca `view_epoch` dispensa a cópia.
- **Descartado:** QoS interativa na thread principal (sem efeito). `--gx-depth 6` piora: com o GX ainda lento, a CPU
  espera no `gx_fifo_feed`.

## 3. Núcleos

A CPU do jogo é uma só (o Gekko tem um núcleo) e o código traduzido roda em sequência. O que dá para dividir são as
outras tarefas: o GX já tem a sua thread (M8), e os 10 trabalhadores do rasterizador servem só ao `--gfx soft`. As
duas threads que limitam são a principal e a `gx-fifo`.

## 4. Verificação

`BUILD_ROOT=/Volumes/AMRecomp/wt-builds/m9-perf scripts/verify_m8.sh`: OK. 54/54 testes nos dois builds; `--gfx gpu`
até a largada em 66 s (arm64; antes 84) e 114 s (x86_64; antes 121); janela sem som, arm64: menu 61,0 / 60,0 e corrida
60,6 / 60,0; x86_64: 59,8 / 60,0 e 52,3 / 59,7.

## 5. Falta

- Corrida longa com o carro acelerando de verdade: o roteiro termina na largada e o carro fica parado.
- x86_64 nativo (Linux e Windows), sem a Rosetta: medir no Nobara (Ryzen 7 5700X3D), onde antes dava 28–31.
