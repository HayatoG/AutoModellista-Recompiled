# Status do port (25/10/2026)

Port nativo de Auto Modellista (GameCube, GAUE08 rev 0) por recompilação estática (DolRecomp → C) e runtime própria
em C17/SDL3. O código do jogo já roda nativo (C recompilado); o hardware (OS, DVD, ARAM, VI, SI, AI, DSP/AX, EXI,
GX) é emulado pela runtime. O Dolphin 2609a é só oráculo de desenvolvimento (`scripts/dolphin_trace.py`, no Mac).

## Marcos
| Marco | Estado | Destaques |
| --- | --- | --- |
| M0–M3 | feito | ISO extraída com o dtk, DolRecomp, esqueleto CMake/SDL3 |
| M4 boot até `main` | feito | trace idêntico ao Dolphin |
| M5 OS e disco | feito (23/10 23:51) | menus até a largada em Suzuka; 551 eventos iguais aos do Dolphin |
| M6 primeiro quadro | feito (24/10 01:16) | GX em software (backend Software do Dolphin traduzido para C) |
| M7 menus | feito (24/10 03:49) | controle, áudio AX igual ao do Dolphin, filmes SFD |
| M8 corrida | feito (24/10 19:03) | GX na GPU (Metal), relógio exato (patch do laço contado no DolRecomp), GX sem esperar no DrawDone (cópia na escrita), memory card idêntico ao do Dolphin, cel-shading = geometria; 60 fps no Mac |
| M9 portabilidade | em andamento | parte 1 (shader único) feita; 3a (pacote Linux) feita; 5 (launcher) feita; 4 (corrida a 60 no Mac: LTO, memória, GX) feita, falta medir no x86\_64 nativo; **2 (Windows) próxima**; resto da 3 (Linux byte a byte, CI) pendentes |
| M10 melhorias | não começado | widescreen, resolução interna, funções decompiladas |

## M9 até agora
- 1a investigação (`docs/M9_PARTE1A_INVESTIGACAO.md`): a SDL 3.4.18 não deixa o fragment shader gravar textura;
  hardware tem ROV (D3D12) e interlock (Vulkan).
- 1b-1 shadercross (`docs/M9_PARTE1B_SHADERCROSS.md`): **sempre `--msl-version 2.3.0`** (no padrão 1.2 o
  `raster_order_group` some sem aviso).
- 1b-2 patch da SDL (`docs/M9_PARTE1B_SDL.md`, `patches/sdl/`, aplicado pelo FetchContent): storage gravável no
  fragmento na ordem da rasterização; teste `sdl_rov`.
- 1b-3 ubershader em HLSL (`docs/M9_PARTE1B_HLSL.md`): idêntico byte a byte ao MSL à mão no Mac;
  `--gpu-shader auto|hand|hlsl` (Metal usa o à mão; D3D12/Vulkan o HLSL); sem o recurso, cai em software com aviso.
  Bug do SPIRV-Cross com várias regiões de interlock: o shader usa uma região só, e o build recusa SPIR-V com mais.
- 3a Linux (`docs/M9_PARTE3A_LINUX.md`, `scripts/package_linux.sh`): pacote x86\_64 com glibc ≥ 2.38.
- 5 launcher (`docs/M9_PARTE5_LAUNCHER.md`): tela inicial, `settings.ini` em `SDL_GetPrefPath`, F3 FPS, F9/F10 volume,
  F11 tela cheia, `--no-launcher`, `--selftest` (antigo autoteste sem argumentos).

## Primeiro teste em hardware real (Nobara, Ryzen 7 5700X3D, RTX 2060, driver Nvidia)
Vulkan com interlock funcionou de primeira, mas: corrida 28–31 fps de jogo (Mac pelo Rosetta: 48), menus 60 só em
trechos, música travando (137 atrasos), "farelos" na imagem desde o menu. GPU folgada (213 ms de espera no
DrawDone). Causas não medidas. Suspeitas: 16 threads do rasterizador em software acordadas mesmo na GPU; `dcbf`
(280 milhões) ou cópia na escrita (1 milhão de páginas) caros; farelos = derivadas coarse (`ddx`/`ddy`) no LOD da
textura (remédio provável: `ddx_fine`/`ddy_fine`, conferindo que o Mac continua idêntico). `packaging/linux/perfil.sh`
(perf) existe mas não foi testado. Falta saber se os farelos somem com `--gfx soft`.

## Regras e lições
- Tudo pesado fora do repositório; nunca versionar bytes do jogo (ISO, DOL/REL, C gerado, dumps, capturas com arte).
- Commits pequenos em português `M<n>: ...`; sem push sem o ok do usuário.
- Medir os dois lados com a mesma régua; uma calibração esconde o erro anterior; o gargalo muda de lugar;
  peças paralelas precisam de conferência na junção; um controle negativo só vale onde o erro pode acontecer;
  o Rosetta não prevê o desempenho de um PC de verdade.
