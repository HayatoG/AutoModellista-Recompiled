# M9, parte 3a: build e pacote do Linux x86_64

**Resultado: a runtime compila limpa no Linux x86_64 e sai num pacote portátil** (`scripts/package_linux.sh`):
executável, `libSDL3.so.0` remendada (rpath `$ORIGIN`), `rodar.sh` e `LEIAME.txt`. Glibc mínima **2.38**. O
executável do pacote roda em software e na GPU (Vulkan do lavapipe) sem quebrar. O escopo mudou no meio: a comparação
byte a byte com o Mac (verify_m8 no Linux, Vulkan contra o Metal) e o teste de janela sob Xvfb ficaram para depois
(§5).

## 1. Ambiente

| Item | Versão |
| --- | --- |
| Máquina | OrbStack `am-linux-x64` no Apple M4: Ubuntu 24.04.5, x86_64 pelo Rosetta, 9 núcleos, ~5 GB |
| Compilador | clang 18 (Ninja, CMake 3.28), glibc 2.39 |
| SDL | 3.4.18 do FetchContent com `patches/sdl/` (X11, Wayland, libdecor, ALSA, Pulse, PipeWire, udev, D-Bus, KMSDRM e Vulkan, todos por dlopen) |
| Vulkan | Mesa 25.2.8, lavapipe (`llvmpipe`, Vulkan 1.4, com `VK_EXT_fragment_shader_interlock`); instalado `vulkan-validationlayers` 1.3.275 |

O volume `/Volumes/AMRecomp` aparece no mesmo caminho dentro da máquina; builds em
`/Volumes/AMRecomp/wt-builds/linux-x64`, logs em `/Volumes/AMRecomp/logs/m9/p3a-linux`.

## 2. Mudanças

| Arquivo | O quê |
| --- | --- |
| `runtime/platform/platform.c` | `platform_user_dir`: no macOS, o caminho de antes (`SDL_GetPrefPath("automodellista", ...)`); nos outros, sem organização: `$XDG_DATA_HOME/AutoModellista/` (`~/.local/share/AutoModellista/`, onde fica o `MemoryCardA.raw`). A QoS de thread já estava atrás de `__APPLE__` |
| `CMakeLists.txt`, `runtime/CMakeLists.txt` | opção `AM_SDL3_SHARED` (padrão OFF): SDL como `.so`; no Linux, `cmake --install` põe o executável com `INSTALL_RPATH $ORIGIN` |
| `tests/shaders/rov_min.hlsl`, `tests/shaders/rov_min_shaders.h` | o teste do ROV em HLSL com a mesma conta do MSL à mão (semente só de leitura no binding 0 do set 2, contador gravável no 1); `-DAM_NO_INTERLOCK` dá o controle sem ordem |
| `scripts/build_shaders.sh rov` | gera o header com SPIR-V e DXIL (com e sem interlock), passa no `spirv-val` e confere um só `OpBeginInvocationInterlockEXT` |
| `tests/unit/sdl_rov_test.c` | MSL à mão no Metal, SPIR-V/DXIL de `rov_min.hlsl` nos outros; sem tela, tenta o driver `offscreen`; `--no-order` (o antigo `--no-rog`) |
| `scripts/package_linux.sh`, `packaging/linux/` | o pacote (§4) |

Nenhum warning novo: a runtime compila com `-Werror` no clang 18 do Linux; as únicas mensagens do build são do
`wayland-scanner` da SDL (validação do XML dos protocolos).

## 3. Testes no Linux

- Build Release do zero: 2 min 36 s (configure 150 s, a maior parte nas checagens da SDL pelo Rosetta).
- `ctest`: **52/52**, com `gpu_shader` pulado (precisa do MSL à mão) e `sdl_rov` passando no Vulkan.
- `sdl_rov` no lavapipe: device com `GRAPHICS_STORAGE_READ_WRITE` e `RASTER_ORDERED_STORAGE` = 1; SPIR-V com
  interlock: **0 de 4096** pixels diferentes da CPU. Camada `VK_LAYER_KHRONOS_validation` carregada
  (`VK_INSTANCE_LAYERS`, conferido com `VK_LOADER_DEBUG`): nenhuma mensagem.
- O controle sem interlock (`--no-order`) também dá **0 de 4096** no lavapipe: o llvmpipe rasteriza cada tile numa só
  thread, na ordem dos primitivos, então a ordem vem de graça e o controle não discrimina. No Mac (Metal) o mesmo
  controle dá 4096 de 4096. A prova de que o interlock é necessário fica para uma GPU real.

## 4. Pacote

`scripts/package_linux.sh [--smoke]` (no Mac, roda a si mesmo no OrbStack): build Release em
`build-x86_64-release-pkg` com `AM_SDL3_SHARED=ON` e sem testes, `cmake --install --strip`, cópia da `libSDL3.so.0`,
`LEIAME.txt` e `rodar.sh` com a glibc, a versão da SDL e o SHA-256 do `main.dol` preenchidos, e o `.tar.gz` ao lado.
O script para se o executável ou a SDL dependerem de algo além de libc/libm (e da própria SDL), ou sem o rpath.

| Item | Valor |
| --- | --- |
| Pasta | `/Volumes/AMRecomp/dist/linux-x64/` (26 MB: executável 23,6 MB, `libSDL3.so.0` 3,3 MB) |
| Arquivo | `/Volumes/AMRecomp/dist/automodellista-recompiled-linux-x64-<commit>.tar.gz` (8,2 MB) |
| Dependências diretas | executável: `libSDL3.so.0`, `libm.so.6`, `libc.so.6` (RUNPATH `$ORIGIN`); SDL: `libm.so.6`, `libc.so.6` |
| Glibc mínima | **2.38** (maior `GLIBC_x.y` pedido; o build é do Ubuntu 24.04 com 2.39). Fedora 39+ e Nobara atuais servem |

`rodar.sh <pasta extraída> [--gfx gpu|soft] [--escala N] [opções...]` aceita a raiz do `dtk disc extract` ou a
própria `sys/`, avisa se o `main.dol` não for o GAUE08 rev 0 e abre a janela no modo interativo. O `LEIAME.txt`
explica a extração com o dtk (o mesmo do `scripts/phase1_iso.sh`), os pacotes de runtime do Fedora/Nobara e o
requisito do `--gfx gpu` (interlock; sem ele, recuo para o software com aviso). O pacote não leva bytes do jogo, mas
o executável é código recompilado do jogo: é para uso pessoal.

Prova (`--smoke` e execuções à mão, executável do pacote, sem janela):

| Execução | Resultado |
| --- | --- |
| `--gfx soft`, 300 quadros | termina limpo |
| `--gfx gpu`, `SDL_VIDEO_DRIVER=offscreen`, 400 quadros | `GX: desenho na GPU (driver vulkan, HLSL (SPIR-V), fragment shader interlock, ubershader TEV)`; 87 564 desenhos em 12 s; captura do q400 contra a do software: 39,2 dB, 0,56% dos pixels com diferença > 24 (a mesma ordem de grandeza da GPU contra o software no Mac) |
| `rodar.sh <extraído> --gfx soft --frames 60 --audio off` (offscreen) | termina limpo e grava o cartão em `~/.local/share/AutoModellista/MemoryCardA.raw` |

## 5. O que não foi feito ou não dá para testar aqui

- Comparação byte a byte com o Mac (`verify_m8.sh` no Linux: capturas do software e timebase contra o golden) e das
  capturas do Vulkan contra o Metal: descartadas nesta rodada por mudança de escopo.
- Janela de verdade (X11/Wayland, eventos, áudio) e Xvfb: não rodados. Só o driver `offscreen`.
- GPU real (Nvidia, RADV, ANV), desempenho nativo e Wayland real: não há máquina aqui; o Rosetta e o lavapipe não são
  representativos.
- Build Debug com sanitizers no Linux: não rodado.

No Mac (`/Volumes/AMRecomp/wt-builds/m9-linux-mac`, arm64 Release): build limpo e ctest 52/52; `sdl_rov` 0 de 4096 com
`raster_order_group` e 4096 de 4096 com `--no-order`, como na parte 1b.
