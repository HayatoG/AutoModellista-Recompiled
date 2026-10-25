# M9, parte 1a: ordem dos pixels em D3D12 e Vulkan (investigação)

Pergunta: dá para levar o GX na GPU (EFB lido e escrito pelo próprio shader, sem blend nem depth fixos; ver
`docs/M8_CORRIDA.md` §3) para Direct3D 12 e Vulkan pela SDL3 GPU, escrevendo o shader uma vez só (HLSL →
SDL_shadercross → DXIL, SPIR-V e MSL)?

Resposta curta: **o hardware dá conta, a SDL 3.4.18 não.** A API da SDL GPU não tem textura de storage gravável nos
estágios gráficos; no D3D12 falta até o registro `u` no root signature do pixel shader. O caminho de shader único
existe (provado abaixo: SPIR-V com interlock vira `raster_order_group` em MSL e `RasterizerOrderedTexture2D` em HLSL),
mas exige um patch na SDL. Recomendação: caminho A com a SDL remendada em `patches/`, e o software como reserva.

Fontes: SDL `release-3.4.18` (a tag fixa do `CMakeLists.txt`), já baixada pelo FetchContent em
`/Volumes/AMRecomp/build-sdl3418/_deps/sdl3-src` (linhas abaixo são desse fonte); SDL_shadercross `1ff05be`
(2026-09-03), clonado só para leitura em `/Volumes/AMRecomp/ext/SDL_shadercross`; teste mínimo em
`/Volumes/AMRecomp/ext/m9_rov_test` (fora do repositório).

## 1. Versão da SDL3

- `CMakeLists.txt:20`: `AM_SDL3_TAG "release-3.4.18"` (FetchContent; `AM_SDL3_SYSTEM` usa a do sistema).
- O fonte já está em cada build (`build-*/_deps/sdl3-src`); `include/SDL3/SDL_version.h` dá 3.4.18. Não foi preciso
  baixar nada para `ext/`.

## 2. Fragment shader com storage texture de leitura e escrita?

**Não, pela API pública. No estágio gráfico, storage é só leitura.**

- `SDL_gpu.h:909-912`: as flags de uso são `GRAPHICS_STORAGE_READ`, `COMPUTE_STORAGE_READ`, `COMPUTE_STORAGE_WRITE` e
  `COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE`. Não existe `GRAPHICS_STORAGE_WRITE`.
- `SDL_gpu.h:2705-2745` (ordem dos recursos de `SDL_CreateGPUShader`): no gráfico há "sampled textures, storage
  textures, storage buffers" e nada de read-write; para DXIL o pixel shader só tem `t[n], space2`, sem `u`. Já o
  compute (`SDL_gpu.h:2608-2626`) tem o conjunto read-write separado (`u[n], space1`).
- `SDL_BindGPUFragmentStorageTextures` (`SDL_gpu.h:3497-3511`) pede `GRAPHICS_STORAGE_READ`.
- Backends:
  - **D3D12**: no root signature gráfico (`D3D12_INTERNAL_CreateGraphicsRootSignature`, `SDL_gpu_d3d12.c:2476`) o
    pixel shader recebe só faixas `SRV` (storage texture em `:2635`, storage buffer em `:2654`); `UAV` só aparece no
    compute (`:2873`, `:2891`). A textura de storage gráfica fica no estado de leitura de shader (`:1900`).
  - **Vulkan**: o descritor é `STORAGE_IMAGE` (`SDL_gpu_vulkan.c:3921`) em layout `GENERAL` (`:2769-2772`), que o
    Vulkan até deixaria gravar, mas a barreira só declara `SHADER_READ` (sem `SHADER_WRITE`), então o que o shader
    escrever não é tornado visível para a cópia seguinte.
  - **Metal**: a textura só ganha `MTLTextureUsageShaderWrite` com uma flag de compute (`SDL_gpu_metal.m:1460-1471`).
    Pedindo `GRAPHICS_STORAGE_READ | COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE` (a SDL só proíbe juntar
    `GRAPHICS_STORAGE_READ` com `SAMPLER`, `SDL_gpu.c:1270`), um fragment shader MSL com `access::read_write`
    funcionaria sem patch: o Metal não distingue o recurso na hora de amarrar.

**Como o caminho Metal funciona hoje** (sem nada especial da SDL): o pipeline declara **dois color targets
`R32_UINT`** (`runtime/platform/platform_gpu.c:61-72`; texturas `COLOR_TARGET | SAMPLER`, `:81-82`), o passe abre com
`LOADOP_LOAD`/`STOREOP_STORE` nos dois (`:223-225`), sem blend. O MSL feito à mão recebe o valor atual dos dois
attachments como **entrada do fragmento** (`uint dst_c [[color(0)]], uint dst_z [[color(1)]]`,
`gx_ubershader.metal:468`) e devolve os novos em `[[color(0)]]`/`[[color(1)]]` (`:84-85`). Isso é o *programmable
blending* das GPUs Apple (memória de tile), que já serializa fragmentos sobrepostos na ordem da API. A SDL não sabe de
nada disso: para ela é um pipeline comum de dois color targets sem blend; só funciona porque o `metallib` é nosso.
Em HLSL não existe jeito de escrever `[[color(n)]]` como entrada, então um shader único não reaproveita esse truque.

## 3. D3D12 e `RasterizerOrderedTexture2D` (ROV)

**Não, sem patch.** Um DXIL com `RasterizerOrderedTexture2D<uint> efb : register(u0, space2)` não casa com o root
signature que a SDL monta (só `t`/`s`/`b` no pixel shader): a criação do PSO falha. A SDL também nunca consulta
`D3D12_FEATURE_D3D12_OPTIONS.ROVsSupported` (nenhuma ocorrência no backend) e cria o device em feature level 11_0
(`SDL_gpu_d3d12.c:116`), onde ROV é opcional.

Patch necessário (estimativa 200-300 linhas na SDL, mais a API):

1. API: uma flag de uso gráfica gravável (ou aceitar `COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE` no gráfico) e
   `num_readwrite_storage_textures` em `SDL_GPUShaderCreateInfo` para o fragmento;
2. D3D12: faixa `UAV` (`u[n], space2`) visível ao pixel shader no root signature; descritores UAV no heap ao amarrar;
   estado `UNORDERED_ACCESS` durante o passe e barreiras de transição; checar `ROVsSupported` e expor como propriedade;
3. Nada a fazer no device além dessa checagem: ROV é obrigatório em 12_1 e opcional em 11_0/12_0
   ([tabela da Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-feature-levels)).

O passe não precisaria de render target (D3D12 aceita UAV-only), mas para não mexer na validação da SDL o mais simples
é deixar um color target falso com máscara de escrita zero.

## 4. Vulkan e `VK_EXT_fragment_shader_interlock`

**Não habilita, mas falta pouco.** Não há nenhuma menção a interlock ou a `rasterization_order` no backend. A SDL 3.4
tem `SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER` com `SDL_GPUVulkanOptions` (`SDL_gpu.h:2398-2424`):

- `device_extension_names` acrescenta extensões sem patch (`SDL_gpu_vulkan.c:12569-12575`);
- `vulkan_10_physical_device_features` liga `fragmentStoresAndAtomics` (necessário para gravar storage no fragmento);
- mas o `feature_list` só reconhece structs do núcleo 1.1/1.2/1.3 (`SDL_gpu_vulkan.c:11778-11885`): a
  `VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT` é ignorada, e o `pNext` do `vkCreateDevice` é montado só com
  essas (`:12577-12636`). Sem a feature `fragmentShaderPixelInterlock` ligada, usar a capability no SPIR-V é uso
  inválido (camada de validação reclama; o driver pode até funcionar).

Patch mínimo no Vulkan: ~15-25 linhas, em dois pontos: (a) encadear a struct de interlock no `pNext` do
`deviceCreateInfo` (`:12633`) quando a extensão estiver pedida e suportada (consultando com
`vkGetPhysicalDeviceFeatures2`); (b) `SHADER_WRITE` no `srcAccessMask` da saída de `GRAPHICS_STORAGE_READ`
(`:2734`) e no `dstAccessMask` da entrada (`:2771`). Mais a parte comum da API do item 3, que no Vulkan é só
contagem (o descritor já é `STORAGE_IMAGE`).

Alternativa Vulkan (*framebuffer fetch* com input attachments e
`VK_EXT_rasterization_order_attachment_access`): **mais cara pela SDL.** O render pass da SDL sempre cria o subpass
com `inputAttachmentCount = 0` (`SDL_gpu_vulkan.c:6193`, `:6290`) e o modelo de binding não tem `subpassInput`; seria
patch maior que o do interlock, para uma extensão de cobertura parecida (22,95% contra 26,4% dos relatórios do
[gpuinfo](https://vulkan.gpuinfo.org/listfeaturesextensions.php), a maioria de GPUs móveis e MoltenVK).

## 5. SDL_shadercross

**A tradução funciona; a ferramenta não está pronta para usar aqui.**

- HLSL → SPIR-V: o DXC traduz `RasterizerOrdered*` para `SPV_EXT_fragment_shader_interlock`, com
  `PixelInterlockOrderedEXT` e `OpBegin/EndInvocationInterlockEXT` em volta dos acessos
  ([SPIR-V.rst do DXC](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst), seção
  "Rasterizer Ordered Views").
- SPIR-V → MSL/HLSL: **testado aqui.** Um fragment shader mínimo no layout de SPIR-V da SDL (EFB `r32ui` em
  `set = 2`, uniforms em `set = 3`, interlock em volta de ler Z, comparar, misturar cor e gravar), compilado com
  glslang 16.6 (Homebrew), sai com `OpCapability FragmentShaderPixelInterlockEXT` e `PixelInterlockOrderedEXT`.
  O `spirv-cross` (Homebrew, 2026-09-16) gera:
  - MSL: `texture2d<uint, access::read_write> efb_depth [[texture(0), raster_order_group(0)]]` (idem cor), que o
    `xcrun metal -std=metal3.0` compila;
  - HLSL SM 5.1: `globallycoherent RasterizerOrderedTexture2D<uint> efb_color : register(u0, space2)`, ou seja,
    exatamente o registro que o root signature da SDL não tem (item 3).
- O shadercross passa por esse mesmo SPIRV-Cross (HLSL → DXC → SPIR-V → SPIRV-Cross → MSL; HLSL → DXC → DXIL direto)
  e, no gráfico, remapeia storage textures do set 2 para os índices de `[[texture]]` do MSL sem recusar as
  graváveis (`src/SDL_shadercross.c`, bloco a partir da linha 960). Dois cuidados: o MSL padrão é 1.2
  (`SDL_SHADERCROSS_PROP_SPIRV_MSL_VERSION_STRING`, `:950`), e `raster_order_group` pede MSL 2.0; e no HLSL os
  registros `t0` e `u0` do space2 caem no mesmo binding do SPIR-V, então é preciso `[[vk::binding(n, 2)]]` explícito.
- Compilar no Mac: não tem release do shadercross nem binário do DXC para macOS (a última release do DXC,
  v1.9.2609, só traz Windows e Linux x86_64). O shadercross com `SDLSHADERCROSS_VENDORED=ON` compila o DXC dos
  submódulos (uma árvore LLVM): estimo 40-90 min no M4 e alguns GB em `/Volumes/AMRecomp/ext/`. Alternativa: o Vulkan
  SDK da LunarG para macOS, que traz `dxc` e `spirv-cross` (a conferir na versão atual). Não compilei: não era
  rápido, e a parte que importa (SPIRV-Cross) já está provada.

## 6. Plano B: blend e depth fixos

O que o Pixel Engine faz e o blend fixo da SDL não faz (a SDL não expõe *logic op*, *dual-source blend* nem formato
de cor de 24 bits empacotado; ver `SDL_GPUBlendFactor`/`SDL_GPUBlendOp` em `SDL_gpu.h:1217-1251`):

| Recurso do PE | No blend fixo | Impacto provável |
| --- | --- | --- |
| Fatores e soma (`efb_blend`) | cabe (fatores 0, 1, cor, 1-cor, alfa da fonte e do destino) | arredondamento: o GX faz `f += f >> 7` e corta em 8 bits; a GPU arredonda em unorm. Erro de ±1 em superfícies com transparência. |
| Subtração (bit 11) | `REVERSE_SUBTRACT` com fatores 1/1 | igual, a menos do arredondamento |
| Operações lógicas (16) | **não há** na SDL (D3D12 só em RTV `UINT`, Vulkan exige `logicOp`; nenhum exposto) | aproximar as comuns (copy, clear, set, invert) com blend; o resto errado. Falta medir se o jogo usa. |
| Alfa constante do destino (`dst_alpha`) | precisa de *dual-source blend* ou de uma passada a mais | sem isso, o alfa no EFB fica errado onde há blend; aparece nas cópias que leem alfa |
| Formatos RGBA6 e RGB565 | renderizar em RGBA8 e quantizar no shader | a quantização acontece antes do blend, não depois: erro acumulado em camadas transparentes |
| Dither (Bayer 2x2 em RGBA6) | no shader, antes do blend | idem: o dither do GX é aplicado no resultado misturado |
| Z de 24 bits inteiro | `D32_FLOAT` representa 24 bits exatos, com cuidado no mapeamento | ok se o mapeamento for exato; formatos Z16 comprimidos não |
| Z antes do alfa (`zcomploc`) | `early_fragment_tests`/`[earlydepthstencil]` + `discard` | cabe, e grava o Z mesmo com alfa reprovado, como o GX |
| Z depois do alfa, textura de Z, neblina por Z | Z escrito pelo shader (`SV_Depth`) | cabe, mas desliga o early-Z: mais lento |
| Estado por desenho | blend e depth são parte do pipeline imutável da SDL | cache de pipelines por estado do PE; hoje é um pipeline só |
| Cópias do EFB | a leitura volta em RGBA8/D32F | converter para o empacotado de 24 bits na volta (CPU ou compute) antes do código de cópia atual |

Em resumo: é o que os backends de hardware do Dolphin fazem, e dá imagem perto do certo na maior parte dos quadros,
mas os casos acima são exatamente onde a GPU deixaria de bater bit a bit com o software (hoje GPU x software fica em
30,65-44,56 dB nas capturas do M8). E é outra implementação do PE para manter ao lado da do shader.

## 7. Suporte de hardware

| Fabricante | D3D12 ROV | Vulkan `fragmentShaderPixelInterlock` |
| --- | --- | --- |
| Nvidia | sim, Maxwell 2 (GTX 900) em diante (feature level 12_1) | sim (Windows e Linux) |
| Intel | sim, Gen9 (Skylake) em diante (12_1) | sim: driver Windows ([anúncio da Khronos](https://www.khronos.org/news/permalink/intel-graphics-windows-10-dch-driver-adds-vulkan-extensions)) e ANV no Linux |
| AMD | sim, Vega (GCN5) e RDNA (12_1); GCN até Polaris fica em 12_0, sem ROV | **Windows: não** (pedido antigo no AMDVLK, nunca exposto). **Linux (RADV): sim**, desde o Mesa 23.2, GFX9 em diante ([MR 22250](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/22250), [Phoronix](https://www.phoronix.com/news/RADV-Adds-Frag-Shader-Interlock), [POPS no Mesa](https://docs.mesa3d.org/drivers/amd/hw/pops.html)) |

Fontes: [feature levels do D3D12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-feature-levels)
(ROV obrigatório em 12_1), [Feature levels in Direct3D](https://en.wikipedia.org/wiki/Feature_levels_in_Direct3D)
(lista de GPUs; Skylake e Vega conferidos de memória, não na tabela), [gpuinfo](https://vulkan.gpuinfo.org/listfeaturesextensions.php)
(26,36% dos relatórios Vulkan com `fragmentShaderPixelInterlock`, todas as plataformas; a separação por fabricante é
gerada por script e não deu para ler). Detalhe do AMD: no RADV, interlock por pixel com MSAA é bem mais lento que por
amostra, e a Vega 10 tem um contorno de travamento com 8+ amostras (nós não usamos MSAA).

Leitura: **Windows → D3D12 com ROV** cobre as três marcas desde ~2015-2017; **Linux → Vulkan com interlock** cobre
as três com drivers atuais. O buraco é Vulkan da AMD no Windows, que não precisamos usar.

## 8. Recomendação

**Caminho A (ROV/interlock, shader único), com a SDL remendada, e o software como reserva.** O caminho B fica para
depois, só se aparecer GPU relevante sem ROV/interlock.

- Por quê: mantém um PE só, o do shader, bit a bit igual ao software (o que dá os goldens do M8); os buracos de
  hardware são pequenos e caem no `--gfx soft`, que já existe. O B é outra implementação do PE, com diferenças
  conhecidas na imagem e um cache de pipelines por estado.
- O patch da SDL (D3D12 ~200-300 linhas, Vulkan ~20, API comum) vira um arquivo em `patches/` aplicado pelo
  `PATCH_COMMAND` do FetchContent na tag fixa, com a ideia de propor para a SDL depois (busca rápida por issue
  aberta no repositório da SDL não achou nada, sem garantia).
- Metal: duas opções. (1) HLSL único também para Metal, com `raster_order_group` em storage textures, sem patch na
  SDL (só as flags de uso); custo provável em GPU Apple, porque sai da memória de tile, a medir. (2) Manter o MSL
  feito à mão com *framebuffer fetch* no Mac e gerar só DXIL e SPIR-V do HLSL: dois fontes do mesmo shader.
  Sugestão: começar por (1), medir a corrida contra os números do M8 §4, e ficar com (2) só se cair muito.
- Teste no Mac sem Windows: o MoltenVK deve expor o interlock por cima dos raster order groups (a conferir), o que
  permitiria rodar o caminho Vulkan aqui; e o WARP do D3D12 é feature level 12_1 (tem ROV), então o caminho D3D12
  roda num runner Windows de CI sem GPU.

## 9. Estimativa

| Etapa | Trabalho |
| --- | --- |
| Toolchain: DXC + shadercross em `ext/` (vendored ou Vulkan SDK), `build_shaders.sh` gerando DXIL/SPIR-V/MSL embutidos | 0,5 dia |
| Patch da SDL (API + D3D12 + Vulkan + Metal flags), em `patches/` | 1,5-2 dias |
| Ubershader em HLSL (612 linhas de MSL hoje, aritmética inteira; tradução quase direta) | 1 dia |
| `platform_gpu.c` por formato de shader, storage em vez de color target, color target falso, checagem de ROV/interlock com recuo para o software | 0,5-1 dia |
| Validar no Mac: Metal pelo HLSL contra os goldens do M8 (GPU x software ≥ 30 dB), desempenho; Vulkan via MoltenVK se der | 1 dia |
| **Parte 1 completa** | **~4,5-5,5 dias de sessão** |
| Parte 2 (Windows): build com clang-cl do C gerado de 84 MB, camada de plataforma (caminhos, threads, áudio), D3D12 no WARP em CI e numa GPU real, pacote "traga sua ISO" | **~4-6 dias**, mais o acesso a uma máquina Windows com GPU para o teste final |

Riscos: a SDL mudar o código remendado ao trocar de tag (por isso tag fixa); desempenho do ROV com o ubershader
inteiro dentro da região ordenada (dá para abrir a região só no trecho do PE: leitura do Z até a gravação da cor);
drivers que expõem o recurso com bugs.
