# M9, parte 5: tela inicial, configurações e contador de quadros

**Resultado:** sem argumentos (ou com `scripts/assistir.sh jogar` e o `rodar.sh` do Linux), a runtime abre uma tela
inicial com o banner do disco do jogador, um menu grande para teclado e controle (JOGAR, Configurações, Controles,
Memory card, Sair) e um rodapé com backend, GPU e escala. JOGAR começa o jogo na mesma janela. No jogo há contador de
quadros (F3), volume (F9/F10), tela cheia (F11, Alt+Enter) e VSync; teclado e gamepad são remapeáveis; tudo fica num
`settings.ini`. Os roteiros (`--pad-script`, sem janela) não passam por nada disso: `verify_m8.sh` passa com o golden
igual (§7).

## 1. Arquitetura

| Peça | Onde | O quê |
| --- | --- | --- |
| Dear ImGui 1.92.9 (MIT) | `FetchContent` (`AM_IMGUI_TAG`), alvo `am_imgui` | sem cópia no repositório; backends oficiais `imgui_impl_sdl3` (eventos), `imgui_impl_sdlgpu3` e `imgui_impl_sdlrenderer3` |
| Interface na janela | `runtime/platform/platform_ui.cpp`, `am/platform_ui.h` | contexto, fontes, backends conforme a forma de apresentação, imagens (o banner); a API para o resto é C |
| Janela | `runtime/platform/platform_window.c` (saiu de `platform.c`) | apresentação, eventos, atalhos, tela cheia, VSync, gamepads, captura para o remapeamento, diálogos |
| Apresentação pela GPU | `runtime/platform/platform_present_gpu.c` | com `--gfx gpu`, a janela é do **mesmo device** da SDL3 GPU que desenha o GX (swapchain) |
| Tela inicial | `runtime/front/launcher.cpp` | telas e menus (só API do ImGui; nada de SDL) |
| Contador e atalhos | `runtime/front/overlay.cpp` | desenhado depois da imagem do jogo |
| Sessão | `runtime/front/session.c` | lê o arquivo, põe a linha de comando por cima, abre a janela, roda a tela inicial, grava no fim |
| Configurações | `runtime/front/settings.c`, `am/settings.h` | INI (§4) |
| Pasta do jogo | `runtime/front/gamedir.c`, `core/sha256.c` | acha `sys/main.dol`, confere o SHA-256 |
| Arte | `runtime/front/banner.c` | `files/opening.bnr` pelo decodificador de texturas do GX (§5) |
| Controle | `core/pad_input.c` | mapeamento por controle do GameCube (§3) |

C++ entra só onde o ImGui exige (`platform_ui.cpp` e `front/*.cpp`), sem exceções nem RTTI, com os mesmos warnings
estritos (`AM_STRICT_CXX_FLAGS`). No Linux a `libstdc++` entra estática: o pacote continua dependendo só de
libc/libm e da SDL.

### 1.1 Apresentação

Antes, as duas formas de desenhar o GX mostravam o XFB pelo `SDL_Renderer`. Agora:

- **`--gfx gpu`**: `SDL_ClaimWindowForGPUDevice` no device do GX. O XFB vai para uma textura e é copiado para o
  swapchain com `SDL_BlitGPUTexture` (nearest, barras pretas, a mesma conta do `SDL_LOGICAL_PRESENTATION_LETTERBOX`);
  a interface vem depois num passe com LOAD. Nenhum shader novo: o blit é da SDL, e os shaders do
  `imgui_impl_sdlgpu3` vêm embutidos no próprio backend (SPIR-V para Vulkan, **DXBC** para D3D12, metallib para
  Metal). Por causa do DXBC, o device passou a pedir também `SHADERS_DXBC` (o D3D12 da SDL aceita DXIL e DXBC).
  A thread do GX (`--gx-thread`) e a apresentação usam o mesmo device, cada uma com os seus command buffers.
- **`--gfx soft`** (ou GPU indisponível): `SDL_Renderer`, como antes. Com a interface ligada, a apresentação lógica
  (letterbox) é desligada só para desenhar o ImGui, e o `SDL_SetRenderScale` recebe a densidade de pixels (como no
  exemplo oficial do backend; sem isso a interface sai num quarto da janela no Retina).
- **`--shot` com janela** (`<arq>_janela.png`): lido de volta antes da interface; no caminho da GPU, de uma textura do
  tamanho do swapchain com o mesmo blit.
- A interface é desenhada depois da imagem do jogo e da leitura do `--shot`: **nunca entra no XFB nem nas
  capturas.** Sem interface (roteiros), o caminho do `SDL_Renderer` é o de antes, chamada por chamada.

A tela inicial e o jogo usam a mesma janela. Trocar o backend nas Configurações liga ou desliga o device da GPU fora
do quadro do ImGui, e a janela troca de forma de apresentação no quadro seguinte.

## 2. Telas, teclas e opções

Menu: setas/direcional movem; **A** (sul do gamepad) ou Enter confirmam; **B** (leste) ou Esc voltam. Nas linhas de
opção, esquerda/direita mudam o valor.

| Tela | Conteúdo |
| --- | --- |
| Principal | banner, JOGAR, Configurações, Controles, Memory card, Sair; rodapé "Metal · Apple M4 · escala 2x" (ou "software · janela metal (SDL_Renderer)") e "A / Enter Confirmar · B / Esc Voltar" |
| Configurações | backend do GX (GPU/software), escala da janela (1x a 6x), tela cheia, VSync, filtro da imagem (`nearest` pixels, `linear` suave, `sharp` escala inteira por nearest e bilinear nas bordas; `video.filter` no INI, `--present-filter` na linha de comando; só a janela, o XFB e as capturas não mudam), contador de FPS, volume (de 10 em 10%), pasta do jogo |
| Controles | controle da porta 1 (automático = o primeiro conectado, ou um pelo nome) e uma linha por controle do GameCube: escolher a linha e apertar a tecla **ou** o botão novo (Esc cancela); "Restaurar os padrões do Dolphin" |
| Memory card | arquivo do slot A (padrão ou escolhido no diálogo de salvar da SDL, filtro `.raw`) |
| Pasta do jogo | diálogo de pasta da SDL (`SDL_ShowOpenFolderDialog`) ou o caminho digitado; mostra o resultado da conferência |

Atalhos (na tela inicial e no jogo): **F3** contador de quadros; **F9/F10** ou **-/+ do teclado numérico** volume;
**F11** ou **Alt+Enter** tela cheia ("de mesa", sem trocar o modo de vídeo). Essas teclas não podem ser remapeadas.
No Mac, F9/F10/F11 pedem a tecla fn (ou "usar F1, F2 como teclas de função").

O contador mostra os quadros/s do jogo (laço `fn_80051034` contra o relógio), os desenhados (sem os pulados pelo
`--frameskip`), ms por quadro, o backend (Metal/Vulkan/D3D12 e a placa, ou software e a janela) e é a média de meio
segundo.

Linha de comando nova (`runtime/main.c`):

| Opção | O quê |
| --- | --- |
| (sem argumentos) | tela inicial (com o C gerado; sem ele, o autoteste, como no CI) |
| `--selftest` | o autoteste do M3 (antes era o "sem argumentos"; `verify_m3.sh` e o CI passam a usar) |
| `--no-launcher` | pula a tela inicial: o jogo começa com a pasta e as opções do arquivo |
| `--interactive` | modo interativo com `--sys` mesmo sem `--window` (o `rodar.sh` usa) |
| `--fps-overlay on\|off`, `--volume 0..100`, `--fullscreen on\|off`, `--vsync on\|off` | o mesmo das Configurações, só nesta execução |

**Modo interativo** = sem `--sys`, ou janela sem `--pad-script`, ou `--interactive`. Só nele o arquivo é lido e há
tela inicial e contador; sem `--frames` roda até a janela fechar, sem watchdog de tempo, e fechar a janela sai com 0.
Os roteiros (`--pad-script`, execuções sem janela, todos os `verify_*.sh`) continuam exatamente como antes.

## 3. Controles remapeáveis

`PadControl` (`am/pad_input.h`) lista os 21 controles: A, B, X, Y, Z, START, as 4 direções do direcional, do stick e
do stick C, L, R e o modificador de meia inclinação. Cada um tem **uma tecla** (nome da SDL, resolvido pela plataforma
em `platform_input_set_keys`; o bit `1 << controle` de `PlatformInput.keys` diz se está apertada) e **um conjunto de
botões do gamepad** (bits de `PlatformPadButton`, que ganhou os cliques dos sticks e os dois gatilhos). Os padrões são
os de `docs/M7_MENUS.md` §3 (Dolphin). Regras:

- Sticks analógicos do gamepad fixos (esquerdo = stick, direito = stick C); botões ligados a uma direção dão a
  inclinação inteira.
- L e R são analógicos quando ligados a um gatilho do gamepad (clique a partir de 90%, como antes); ligados a um botão,
  curso inteiro e clique. Um gatilho ligado a um botão digital conta a partir da metade.
- Teclado e gamepad continuam somando. O teste `pad_input` ganhou o remapeamento e os nomes; os testes antigos passam
  com os padrões.

## 4. Arquivo de configuração

`settings.ini` em `SDL_GetPrefPath("", "AutoModellista")`: `~/Library/Application Support/AutoModellista/` (macOS),
`~/.local/share/AutoModellista/` (Linux), `%APPDATA%\AutoModellista\` (Windows). No Linux e no Windows é o mesmo
diretório do memory card; no macOS o cartão continua em `.../automodellista/AutoModellista/` (M8), para não mudar de
lugar o save de ninguém.

```ini
[game]
dir = /jogos/automodellista      ; raiz com sys/ e files/ (vazio = perguntar)
memcard =                        ; vazio = MemoryCardA.raw no diretório de dados
[video]
backend = gpu                    ; gpu | soft
scale = 2                        ; 1..6
fullscreen = 0
vsync = 0
fps_overlay = 0
[audio]
volume = 100                     ; 0..100
[input]
gamepad =                        ; nome do gamepad da porta 1 (vazio = o primeiro)
[keyboard]
A = X                            ; nome da tecla na SDL: "Return", "Left Shift", "Keypad 8"...
...
[gamepad]
Z = leftshoulder+rightshoulder   ; south east west north back start leftshoulder rightshoulder dpup dpdown
...                              ; dpleft dpright leftstick rightstick lefttrigger righttrigger
```

Linhas desconhecidas são ignoradas (com aviso no log). **A linha de comando tem prioridade** e não é gravada: no fim,
cada campo que veio da linha de comando e que o jogador não mexeu na tela inicial volta ao valor do arquivo antes de
gravar. Atalhos usados no jogo (F3, volume, tela cheia) são gravados.

## 5. De onde vem a arte

Nada do jogo é embutido no executável nem no repositório. A tela inicial lê `files/opening.bnr` da pasta do jogador
em tempo de execução: é o banner do disco (formato BNR1 do SDK, YAGCD 14), o mesmo que o menu do GameCube mostra —
o logo "auto modellista" em 96x32 texels RGB5A3, em blocos de 4x4 a partir de 0x20. `banner.c` decodifica com o
`gx_tex_decode_texel` do GX e a interface amplia com filtro linear (3:1, até ~70% da largura). O nome longo do banner
(0x1860) também é lido.

O logo grande da tela de título está nos `afs*.afs` (contêineres AFS com formatos próprios da Capcom, parte deles
comprimida); achar e decodificar exigiria engenharia reversa do carregador: fica para depois. Sem pasta do jogo (ou
sem `opening.bnr`), a tela mostra "AUTO MODELLISTA / Recompiled" em texto sobre o fundo.

Fontes: a primeira que existir entre Arial (macOS), DejaVu Sans e Noto Sans (Linux) e Segoe UI (Windows), normal e
negrito; sem nenhuma, a fonte vetorial embutida no ImGui. Os acentos vêm da própria fonte.

## 6. Pasta do jogo

Na primeira vez (arquivo sem `dir`, ou pasta que não confere) a tela inicial abre em "Pasta do jogo". Aceita a raiz
(com `sys/` e `files/`) ou a própria `sys/`; lê o `main.dol` e compara o SHA-256 com `GAME_MAIN_DOL_SHA256`
(`am/game.h`, o mesmo de `analysis/iso/main_dol.sha256`: GAUE08 revisão 0). Conferida, a pasta é gravada no arquivo.
Com `--sys` a pasta vem da linha de comando; se não conferir, a runtime avisa e segue (o boot já recusa um DOL
diferente pelo entry point).

**Extrair o `.rvz` direto (para depois).** Hoje o jogador extrai com o dtk ou o Dolphin. Para aceitar a imagem:
RVZ é o formato do Dolphin (cabeçalho, tabela de grupos comprimidos com zstd/LZMA/bzip2, partes "junk" reconstruídas
pelo gerador de lixo do disco). Duas saídas: (a) ler o RVZ em tempo de execução com um leitor próprio (blocos de 128
KiB a 2 MiB, cache dos grupos) por baixo de `core/disc.c`, sem extrair nada, ou (b) extrair uma vez para o diretório
de dados (`sys/` + `files/` pela FST, como o `dtk disc extract`), com barra de progresso na tela inicial. A (b) é mais
simples e reaproveita todo o resto; a (a) evita 1,4 GB extras. Ambas precisam de zstd (e de LZMA para RVZ antigos)
como dependência; ISO/GCM sem compressão seria trivial pela FST.

## 7. Verificação

Mac (Apple M4), `BUILD_ROOT=/Volumes/AMRecomp/wt-builds/m9-launcher`, `AM_LOG_DIR=/Volumes/AMRecomp/logs/m9/p5-launcher`:

| Verificação | Resultado |
| --- | --- |
| `verify_m8.sh --no-window` (oráculo do Dolphin reaproveitado de `logs/m8/verify`) | **passou**, golden `m8_corrida.json` igual sem regravar: defasagem 0,000 campo no q6400, timebase idêntico nos dois builds; software x Dolphin 34,96 / 40,64 / 35,68 / 29,38 dB, GPU x Dolphin 39,44 / 43,34 / 39,95 / 34,92 dB (os mesmos números da parte 2b); `--gx-thread on` e `--gpu-shader hlsl` idênticos byte a byte |
| ctest | 54/54 em arm64 e em x86_64 (novos: `sha256`, `settings`; `pad_input` com remapeamento) |
| clang-format | limpo |
| `--shot` com janela pela GPU (roteiro até o q1800, `--window 1 --gfx gpu`) | `_janela.png` (lido do swapchain via textura) igual ao XFB: 0 pixels diferentes |
| Tela inicial e jogo (arm64, interativo) | GPU: 60,0 quadros/s e 60,0 desenhados no jogo com o contador; software: 59,9 / 59,9 |

Telas (abertas, navegadas com o teclado e fechadas; capturas com `screencapture -l <janela>` em
`/Volumes/AMRecomp/logs/m9/p5-launcher/telas/`, fora do repositório porque mostram a arte e imagens do jogo):

| Captura | O quê |
| --- | --- |
| `tela_inicial.png` | menu principal com o banner, GPU (Metal, swapchain) |
| `configuracoes.png` | Configurações |
| `controles.png` | Controles, com a tecla do A remapeada para P |
| `pasta_do_jogo.png` | Pasta do jogo conferida (caminho digitado), software (`SDL_Renderer`) |
| `jogo_overlay_gpu.png`, `jogo_overlay_soft.png` | o jogo depois do JOGAR com o contador: 60,0 quadros/s e 60,0 desenhados nos dois backends |

Linux: `scripts/package_linux.sh --smoke` no OrbStack (Ubuntu 24.04, clang 18): build sem warnings, pacote com as mesmas
dependências diretas (libc, libm, libSDL3; libstdc++ estática), glibc mínima 2.38, 28 MB (8,6 MB comprimido); o
executável do pacote roda 300 quadros em software e 30 na GPU (Vulkan do lavapipe). Com `SDL_VIDEO_DRIVER=offscreen`,
`--interactive --no-launcher --fps-overlay on` roda 120 quadros em software e na GPU (interface ligada, sem tela).

## 8. O que ficou para depois

- Extrair/ler o `.rvz` direto (§6).
- Volumes separados de música e efeitos: o AX mistura tudo antes da DMA de áudio; separar exigiria marcar as vozes
  por tipo no mixer (`audio/ax_mixer.c`) ou o streaming do ADX (AI) à parte do AX. Hoje há só o volume geral, no
  ganho do `SDL_AudioStream`.
- Logo grande da tela de título (§5).
- Remapear os sticks analógicos do gamepad e zona morta configurável.
- Voltar à tela inicial sem fechar o jogo (o jogo emulado não tem "reset" limpo ainda).
- Testes de janela no Linux/Windows reais (o OrbStack não tem tela; só o build e os testes sem janela rodaram).
