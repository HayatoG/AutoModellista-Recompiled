# M9, parte 1b: patch da SDL (texturas graváveis no fragment shader, na ordem da rasterização)

Estado: **patch pronto e provado no Metal; D3D12 e Vulkan compilam para Windows, falta rodar.** O patch
`patches/sdl/0001-gpu-storage-gravavel-no-fragmento.patch` (sobre a tag `release-3.4.18`, 594 linhas a mais e 21 a
menos em 10 arquivos) deixa um fragment shader ler e gravar texturas de storage na ordem da rasterização: ROV no
D3D12, `VK_EXT_fragment_shader_interlock` no Vulkan, `raster_order_group` no Metal. É um recurso opcional, pedido na
criação do device; quem não pede não vê diferença (o build do M8 continua passando, §5.2).

Continuação de `docs/M9_PARTE1A_INVESTIGACAO.md` (§2–4: por que a SDL 3.4.18 não dá conta sem patch). O compilador
HLSL → DXIL/SPIR-V/MSL (shadercross) é outra peça do M9; o teste daqui usa MSL escrito à mão.

## 1. A API nova

Tudo no estilo da SDL 3 (propriedades em vez de campos novos nas structs, para não mexer no ABI):

| O quê | Nome | Para que serve |
| --- | --- | --- |
| Propriedade de criação do device | `SDL_PROP_GPU_DEVICE_CREATE_FEATURE_GRAPHICS_STORAGE_READ_WRITE_BOOLEAN` (padrão `false`) | Pede o recurso. Não falha se o hardware não tiver: a consulta abaixo diz o que veio. No Vulkan é o que liga `fragmentStoresAndAtomics` e a extensão de interlock. |
| Consulta do device (`SDL_GetGPUDeviceProperties`) | `SDL_PROP_GPU_DEVICE_GRAPHICS_STORAGE_READ_WRITE_BOOLEAN` | Textura gravável no fragmento disponível. |
| Consulta do device | `SDL_PROP_GPU_DEVICE_RASTER_ORDERED_STORAGE_BOOLEAN` | Além disso, acesso na ordem da rasterização (`ROVsSupported`, `fragmentShaderPixelInterlock`, `rasterOrderGroupsSupported`). É esta que decide entre a GPU e o `--gfx soft`. |
| Flag de uso da textura | `SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE` (`1u << 7`) | Textura lida e gravada no mesmo fragment shader. Não combina com `SAMPLER` nem com `GRAPHICS_STORAGE_READ`; sem multisample. |
| Propriedade do shader (`SDL_GPUShaderCreateInfo.props`) | `SDL_PROP_GPU_SHADER_CREATE_NUM_READWRITE_STORAGE_TEXTURES_NUMBER` | Quantas texturas graváveis o fragment shader declara (até 4). Só no estágio de fragmento. |
| Função | `SDL_BindGPUFragmentReadWriteStorageTextures(render_pass, first_slot, bindings, n)` | Amarra as texturas graváveis. Usa a `SDL_GPUStorageTextureReadWriteBinding` do compute (textura, nível, camada); `cycle` tem de ser `false`, porque não dá para trocar a textura no meio de um passe. |

Sem pedir o recurso, a flag nova é recusada (`SDL_GPUTextureSupportsFormat` dá `false`, `SDL_CreateGPUTexture` dá
`NULL`) e o shader com contagem diferente de zero também; o teste confere isso (§5.1).

Visibilidade: o que um passe grava fica visível para o passe, a cópia ou o download seguintes (barreira no fim do
passe, §3). Dentro do passe, fragmentos sobrepostos só ficam em ordem se o shader pedir (ROV, interlock, raster order
group); a SDL não serializa nada sozinha.

Detecção no nosso código: com `AM_SDL3_SYSTEM=ON` (SDL do sistema, sem o patch) o macro
`SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE` não existe. O caminho novo fica atrás de
`#ifdef SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE` **e** da consulta `..._RASTER_ORDERED_STORAGE_BOOLEAN`;
sem um dos dois, o caminho atual (Metal com *framebuffer fetch*, ou o software).

## 2. Registros de cada linguagem

As texturas graváveis vêm **depois de todos os recursos só de leitura do fragmento**, para não mudar o índice de
nenhum shader que já existe:

| Formato | Onde ficam | Exemplo (1 sampler, 1 storage texture, 0 storage buffers, 2 graváveis) |
| --- | --- | --- |
| SPIR-V | set 2, bindings a partir de `num_samplers + num_storage_textures + num_storage_buffers` | bindings 2 e 3 do set 2 (`[[vk::binding(2, 2)]]` no HLSL) |
| DXIL / DXBC | `(u[n], space2)` a partir de `u0` | `RasterizerOrderedTexture2D<uint> efb : register(u0, space2)` e `u1` |
| MSL | `[[texture]]` a partir de `num_samplers + num_storage_textures` | `[[texture(2)]]` e `[[texture(3)]]`, com `access::read_write` e `raster_order_group(0)` |

Dois cuidados para o HLSL único (vão para a peça do shadercross): no HLSL, `t0` e `u0` do `space2` caem no mesmo
binding do SPIR-V se não houver `[[vk::binding]]` explícito (`docs/M9_PARTE1A_INVESTIGACAO.md` §5); e no MSL gerado
pelo SPIRV-Cross o índice de `[[texture]]` da gravável tem de ser o da tabela, não o que o SPIRV-Cross escolheria.

## 3. O que o patch muda em cada backend

Linhas do patch (a mais / a menos), contadas com `git diff --numstat` sobre a tag:

| Parte | Linhas | O quê |
| --- | --- | --- |
| API e documentação (`SDL_gpu.h`, dynapi) | 76 / 1 | flag, propriedades, função nova e a documentação dos registros |
| Comum (`SDL_gpu.c`, `SDL_sysgpu.h`) | 114 / 1 | validação (flag, contagem, `cycle`, nível e camada, amarração faltando no desenho), `graphics_storage_read_write` no device, ponteiro da função nova nos três backends |
| D3D12 | 134 / 4 | ver abaixo |
| Vulkan | 184 / 15 | ver abaixo |
| Metal | 86 / 0 | ver abaixo |

**D3D12.** O root signature gráfico ganha uma faixa `UAV` `(u0…, space2)` visível só ao pixel shader quando o
shader declara graváveis. A textura com a flag nova é criada com `ALLOW_UNORDERED_ACCESS` e uma UAV por
subrecurso (a mesma estrutura das graváveis do compute) e tem `UNORDERED_ACCESS` como estado padrão: amarrar dentro do
passe não precisa de transição, só copiar os descritores UAV para o heap visível e chamar
`SetGraphicsRootDescriptorTable`. No fim do passe vai uma barreira UAV por textura amarrada; as transições de e para
cópia já levam barreira UAV também (a flag entrou em `needsUAVBarrier`). Na criação do device, com o recurso pedido,
consulta `D3D12_FEATURE_D3D12_OPTIONS.ROVsSupported` (UAV no pixel shader existe desde o feature level 11_0; ROV é
opcional abaixo de 12_1). Formato: exige `UAV_TYPED_LOAD` e `UAV_TYPED_STORE`.

**Vulkan.** Um modo de uso novo, `GRAPHICS_STORAGE_READ_WRITE` (layout `GENERAL`, `SHADER_READ | SHADER_WRITE`,
estágio de fragmento), que vira o padrão da textura: dentro de um render pass não se pode trocar layout, então a
textura já está em `GENERAL` quando é amarrada. As barreiras de cópia de e para esse modo levam `SHADER_WRITE`. O set 2
do pipeline ganha bindings `STORAGE_IMAGE` depois dos storage buffers (a criação do layout e do pool de descritores
passou a pôr a "categoria 2" depois da "categoria 1", o que não muda nada nos sets que só têm uma delas). No fim do
render pass, se houve gravável amarrada, uma barreira de memória `FRAGMENT_SHADER/SHADER_WRITE` → shaders e cópias.
Na criação do device, com o recurso pedido: liga `fragmentStoresAndAtomics` se o hardware tiver; se o device tiver
`VK_EXT_fragment_shader_interlock`, consulta a struct de interlock (`vkGetPhysicalDeviceFeatures2KHR`, função nova
na lista do backend), encadeia `VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT` com só
`fragmentShaderPixelInterlock` no `pNext` do `vkCreateDevice` e acrescenta a extensão. Sem o pedido, a extensão nem
entra na lista.

**Metal.** A flag nova dá `MTLTextureUsageShaderRead | ShaderWrite`. A amarração usa a textura direto quando ela tem
um nível e uma camada, ou uma *view* do nível e da camada pedidos (como no compute), e entra em `setFragmentTextures`
a partir de `num_samplers + num_storage_textures`. O Metal rastreia as dependências entre passes sozinho, então não há
barreira. A consulta usa `rasterOrderGroupsSupported` e `readWriteTextureSupport`; no nível 1 de read-write só passam
`R32_UINT`, `R32_INT` e `R32_FLOAT` (o EFB do GX é `R32_UINT`).

Fora do escopo do patch, de propósito: textura gravável no vertex shader, MSAA, `cycle` dentro do passe, e o caso de
quem passa a mesma struct de interlock pelas `SDL_GPUVulkanOptions` (ficaria duplicada no `pNext`).

## 4. Como o patch é aplicado

- `runtime/CMakeLists.txt`: o `FetchContent_Declare(SDL3 …)` ganhou `PATCH_COMMAND`, que chama
  `runtime/cmake/AmApplyPatches.cmake` com o fonte baixado e a pasta `patches/sdl`.
- O script é idempotente: guarda em `<fonte>/.am_patches` o SHA-256 dos patches. Se o carimbo bate, não faz nada; se
  não bate (primeira vez, patch novo ou alterado), volta o fonte ao commit da tag (`git checkout --force` + `git clean`
  na cópia baixada) e aplica todos em ordem com `git apply`. O hash também entra nos argumentos do `FetchContent`, para
  que mudar um patch refaça o passo.
- Conferido: build limpo aplica (10 arquivos alterados no fonte), reconfigurar não reaplica, rodar o script de novo
  diz "já aplicado", e apagar o carimbo reaplica do zero sem erro.
- `AM_SDL3_SYSTEM=ON`: nada é aplicado; o teste do patch sai como pulado e o resto do build é o de antes.
- `-DFETCHCONTENT_SOURCE_DIR_SDL3=<fonte>` (para mexer no patch): o FetchContent usa o fonte local e não aplica nada.
  O fonte de trabalho do patch é `/Volumes/AMRecomp/ext/sdl-m9` (clone da tag, branch `m9-rov`, um commit); o patch
  sai dele com `git format-patch -1 --zero-commit`.

## 5. Testes

### 5.1 O patch de ponta a ponta no Metal (`tests/unit/sdl_rov_test.c`, CTest `sdl_rov`)

Um fragment shader em MSL lê uma textura de storage só de leitura (a semente, em `[[texture(0)]]`) e uma
`R32_UINT` 64×64 gravável (`[[texture(1)]]`, `raster_order_group(0)`). Cada triângulo cobre a textura toda e faz
`v = v * 31 + (tri + 1)`, partindo da semente no triângulo 0. São 384 triângulos por pixel: 192 + 64 em dois desenhos
de um passe e mais 128 num segundo passe. O resultado lido de volta só bate com a conta da CPU se os fragmentos de cada
pixel rodarem na ordem da API (dentro do desenho, entre desenhos e entre passes) e se os índices de `[[texture]]`
forem os da tabela do §2.

Apple M4, Metal com a validação da API ligada:

| Execução | Pixels diferentes da CPU |
| --- | --- |
| `sdl_rov_test` (com `raster_order_group`) | **0 de 4096** (11 verificações, 0 falhas) |
| `sdl_rov_test --no-rog` (o mesmo shader sem o atributo, só informativo) | **4096 de 4096** |

O controle sem o atributo mostra que a ordem não vem de graça: sem o raster order group todos os pixels saem errados.
O teste também confere que, sem pedir o recurso, a consulta dá `false` e a textura com a flag nova é recusada. Sai com
77 (pulado no CTest) sem a SDL remendada ou sem GPU com MSL.

### 5.2 O build normal não muda

`BUILD_ROOT=/Volumes/AMRecomp/wt-builds/m9-sdl AM_LOG_DIR=/Volumes/AMRecomp/logs/m9/p1b-sdl
scripts/verify_m8.sh --no-window` (oráculo do Dolphin copiado de `logs/m8/verify`, sem abrir o Dolphin): **OK**, com a
SDL baixada e remendada pelo `PATCH_COMMAND`.

- ctest: 51 de 51 no arm64 e no x86_64 (Rosetta), já com o `sdl_rov`.
- Até a largada (q6400): soft em 194 s / 303 s, GPU em 87 s / 123 s (arm64 / x86_64); `--gx-thread on` com capturas e
  tempo emulado idênticos aos de `off`; tempo emulado idêntico nos dois builds e defasagem 0 campo contra o Dolphin.
- Capturas iguais às do M8: GPU x soft 36,91 / 44,56 / 39,21 / 30,65 dB e GPU x Dolphin 39,44 / 43,34 / 39,95 /
  34,92 dB (q1600 / q1800 / q2300 / q6400), nos dois builds; golden `tests/golden/m8_corrida.json` igual.
- Byte a byte: as 24 capturas PNG (soft, GPU e GPU com thread, nos dois builds) são idênticas às de
  `logs/m8/verify`. O timebase de lá é de antes da parte 2b do M8 (DVD), então serve de referência um build arm64
  desta mesma branch com a SDL **sem** patch (`-DFETCHCONTENT_SOURCE_DIR_SDL3` apontando para a tag pura): `--gfx gpu`
  até o q6400 dá as quatro capturas e o timebase **idênticos byte a byte** aos do build remendado.

### 5.3 Compilação cruzada para Windows x86_64 (llvm-mingw)

llvm-mingw **20261020** (release mais recente do GitHub em 25/10/2026; clang 23.1.4, UCRT, pacote
`macos-universal`) em `/Volumes/AMRecomp/ext/llvm-mingw`. SDL estática, Release, backends de GPU `d3d12` e `vulkan`
ligados pelo CMake da SDL; toolchain em CMake com `x86_64-w64-mingw32-clang`.

| Build | Resultado | Warnings |
| --- | --- | --- |
| SDL 3.4.18 sem patch, flags da SDL | compila | 6 no total, nenhum em `src/gpu` |
| SDL com o patch, flags da SDL | compila (`libSDL3.a` de 4,99 MB) | 6, os mesmos; nenhum em `src/gpu` |
| `SDL_gpu.c`, `SDL_gpu_d3d12.c` e `SDL_gpu_vulkan.c` com `-Wall -Wextra -Wshadow`, sem e com o patch | compilam | 20 e 20, o mesmo conjunto (só `-Wunused-parameter` em código que o patch não toca) |

No Mac, `SDL_gpu.c`, `SDL_gpu_metal.m` e `SDL_gpu_vulkan.c` com as mesmas flags extras: 27 warnings sem e 27 com o
patch. Não foi rodado nada no Windows (não há máquina aqui).

## 6. O que falta testar no Windows

O teste do §5.1 precisa de um shader por backend; quando o shadercross estiver pronto, o mesmo teste em HLSL gera
DXIL e SPIR-V e roda nos casos abaixo. Até lá, D3D12 e Vulkan só estão compilados.

| Caso | O que conferir |
| --- | --- |
| D3D12 com WARP (feature level 12_1, tem ROV; serve de CI sem GPU) | root signature aceito com o `u0, space2` (sem erro do PSO), consulta `RASTER_ORDERED` = `true`, contador com 0 pixels errados, camada de debug do D3D12 sem avisos de estado de recurso |
| D3D12 em GPU real: Nvidia (Maxwell 2+), AMD (Vega/RDNA), Intel (Gen9+) | o mesmo, mais desempenho do ubershader inteiro dentro da região ordenada |
| D3D12 em AMD GCN até Polaris (12_0, sem ROV) | consulta `RASTER_ORDERED` = `false` e a runtime caindo no `--gfx soft` |
| Vulkan na Nvidia e na Intel (Windows) e no RADV/ANV (Linux) | extensão e `fragmentShaderPixelInterlock` ligados (log com `verbose`), camada de validação sem erros (layout `GENERAL`, barreira do fim do passe, capability de interlock no SPIR-V), contador com 0 pixels errados |
| Vulkan na AMD no Windows (sem interlock) | `GRAPHICS_STORAGE_READ_WRITE` = `true`, `RASTER_ORDERED` = `false`: a runtime não pode usar o caminho |
| Os dois backends | cópia do EFB para a CPU depois do passe (download) e entre passes, que é onde as barreiras do patch importam |

## 7. Decisões

- **Propriedades em vez de campos novos.** `SDL_GPUShaderCreateInfo` e as structs públicas ficam iguais; um programa
  compilado contra a SDL sem patch continua compatível com a SDL remendada.
- **Recurso opt-in.** Só o Vulkan precisaria (para não ligar features e extensão à toa), mas o pedido vale nos três
  backends para a consulta ter o mesmo significado em todos.
- **Estado padrão gravável.** A textura com a flag nova mora em `UNORDERED_ACCESS`/`GENERAL`. No Vulkan não havia
  escolha (sem troca de layout dentro do render pass); no D3D12 seria possível transicionar ao amarrar, mas o mesmo
  modelo nos dois deixa o código e as barreiras iguais. Por isso a flag não combina com `SAMPLER` nem com
  `GRAPHICS_STORAGE_READ`.
- **Até 4 graváveis por shader.** Cabe no resource binding tier 1 do D3D12; o EFB usa 2 (cor e Z).
- **Propor para a SDL depois.** O patch está escrito para isso (documentação no header, dynapi, validação no modo
  debug), mas o `\since` da função diz que é patch local até lá.
