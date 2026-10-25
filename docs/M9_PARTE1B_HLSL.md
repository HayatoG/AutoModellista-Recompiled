# M9, parte 1b: o ubershader do GX em HLSL

**Resultado: no Mac, o ubershader em HLSL dá as mesmas capturas, byte a byte, que o MSL à mão do M8.** Isso vale nos
4 quadros do `verify_m8.sh` e nos 7 do `--cel`, em arm64 e x86_64. O EFB passou de dois color targets lidos por
*framebuffer fetch* para duas `RasterizerOrderedTexture2D<uint>`. Do mesmo fonte o SDL_shadercross gera DXIL
(D3D12), SPIR-V (Vulkan) e MSL 2.3 (Metal); os três vão embutidos no executável. No Mac o padrão continua o MSL à
mão, que é um pouco mais rápido (§5). D3D12 e Vulkan estão gerados e validados, mas não rodaram (§7).

Continua `docs/M9_PARTE1B_SDL.md` (patch da SDL) e `docs/M9_PARTE1B_SHADERCROSS.md` (o compilador).

## 1. O que mudou

| Arquivo | O quê |
| --- | --- |
| `runtime/shaders/gx_ubershader.hlsl` | o ubershader: mesma aritmética do `.metal` (TEV, amostragem s17.7, LOD, alfa, Z-textura, neblina, Z, blend/lógica/dither) |
| `runtime/shaders/gx_ubershader_hlsl.h` | gerado: DXIL e SPIR-V dos dois estágios e um metallib com os dois (MSL do HLSL compilado offline) |
| `runtime/shaders/generated/gx_{vs,fs}_hlsl.metal` | gerado: o MSL que saiu do shadercross, versionado para leitura e diff |
| `scripts/build_shaders.sh [hand\|hlsl\|all]` | gera os dois headers (`hand` é o metallib do M8) e confere o resultado (§2) |
| `runtime/platform/platform_gpu.c` | device com o recurso do patch, escolha do shader e do formato, passe com color target falso e EFB amarrado como storage gravável |
| `runtime/main.c`, `game/game_run.c` | `--gpu-shader auto\|hand\|hlsl`; sem GPU utilizável, aviso e GX em software (antes a execução parava) |
| `tests/unit/gpu_shader_test.c` (CTest `gpu_shader`) | o mesmo lote sintético nos dois shaders, EFB comparado bit a bit |
| `scripts/verify_m8.sh` | roda também o shader que o padrão não usou e compara as capturas com `cmp` (roteiro normal e `--cel`) |

`gfx/gx_gpu.c` não mudou: o uniform do desenho (624 bytes), os vértices, o buffer de texels e a leitura do EFB são os
mesmos nos dois caminhos.

## 2. Build dos shaders

`scripts/build_shaders.sh hlsl` (zsh, Mac):

1. `shadercross gx_ubershader.hlsl -s HLSL -d DXIL|SPIRV -t vertex|fragment -e gx_vs|gx_fs`;
2. MSL com `-d MSL --msl-version 2.3.0 -DAM_MSL_LAYOUT` (§3.2), compilado com `xcrun metal -std=metal3.0
   -mmacosx-version-min=13.0 -O2 -Werror -Wno-unused-const-variable` (o MSL gerado declara uma constante que o código
   otimizado não usa) e juntado num metallib com `xcrun metallib`;
3. conferências que param o script: DXIL com contêiner `DXBC`; `spirv-val --target-env vulkan1.2` nos dois SPIR-V;
   `PixelInterlockOrderedEXT` e **um único** `OpBeginInvocationInterlockEXT` no fragmento; no MSL,
   `[[texture(0), raster_order_group(0)]]` e `[[texture(1), raster_order_group(0)]]`, `DrawState [[buffer(0)]]` e
   `arena [[buffer(1)]]`;
4. tudo num só header, com um array por blob (`s_gx_hlsl_{vs,fs}_{dxil,spirv}`, `s_gx_hlsl_metallib`).

Tamanhos: DXIL 6 516 + 28 356 bytes, SPIR-V 5 044 + 58 176, metallib 24 835; o header tem 769 KB. O resultado é
reprodutível: rodar de novo dá o mesmo header (md5 igual). O build normal (CMake) só inclui os headers e não precisa
do shadercross nem do Xcode. Para regenerar depois de mexer no `.hlsl`: `scripts/build_shadercross.sh` (uma vez,
~4 min) e `scripts/build_shaders.sh hlsl`; versionar `gx_ubershader_hlsl.h` e `generated/`. O DXIL sai assinado (a
parte `HASH` do contêiner não é zero), porque o `libdxil.dylib` vendored valida e assina.

## 3. Mapeamento MSL → HLSL dos pontos delicados

### 3.1 Tabela

| Ponto | MSL à mão (M8) | HLSL (M9) | Por quê |
| --- | --- | --- | --- |
| EFB | `uint dst_c [[color(0)]]`, `uint dst_z [[color(1)]]` como entrada e saída do fragmento | `RasterizerOrderedTexture2D<uint>` com `[[vk::image_format("r32ui")]]` e `[[vk::binding]]` explícito, `register(u0/u1, space2)` | o HLSL não tem framebuffer fetch; o formato explícito evita `StorageImageReadWithoutFormat` no SPIR-V |
| Saída do fragmento | `FOut` com duas cores | `void`, um color target falso `R8_UNORM` com máscara de escrita 0 e `DONT_CARE` | a SDL e o Metal pedem pelo menos um attachment no passe |
| Uniform | `constant DrawState&`, arrays de `uint` | `cbuffer DrawState : register(b0, space3)` com os arrays como `uint4 x[n/4]`, lidos em `x[i >> 2][i & 3]` | num cbuffer cada elemento de array ocupa 16 bytes; `uint4[]` tem o layout de `u32[]` do C em DXIL e SPIR-V (offsets conferidos no `spirv-dis`: o último membro em 592, total 624) |
| Texels | `device const uint *arena [[buffer(1)]]` | `ByteAddressBuffer arena : register(t0, space2)`, `arena.Load(i * 4)` | a SDL D3D12 cria a SRV do storage buffer como *raw* (`D3D12_BUFFER_SRV_FLAG_RAW`), que é a view do `ByteAddressBuffer` |
| Interpolação | `[[center_no_perspective]]` em z e cores | `noperspective` | sai `center_no_perspective` no MSL gerado |
| Vértices | `[[attribute(n)]]` | semântica `TEXCOORDn` = location n | convenção da SDL/shadercross |
| Derivadas | `dfdx`/`dfdy` | `ddx`/`ddy` | viram `dfdx`/`dfdy` no MSL; no D3D12 `ddx` é *coarse* (§7) |
| Reinterpretar bits | `as_type<uint>(f)`, `int(v << 21) >> 21` | `asuint(f)`, `asint(v << 21) >> 21` | |
| Subtração do blend | `select(uint4(0), dst - src, dst > src)` | laço por canal com `?:` | o DXC usa HLSL 2021, que não aceita `?:` com condição vetorial |
| Palavra reservada | `bool linear` | `bool use_bilinear` | `linear` é modificador de interpolação no HLSL |
| Tabelas | `constant int X[8]` | `static const int X[8]` | |

### 3.2 Registros do MSL gerado

O shadercross numera o `[[texture]]` do MSL pelo binding do SPIR-V e o `[[buffer]]` dos storage buffers como
`uniforms + (binding - texturas)`, supondo as texturas antes dos buffers no set 2. O patch da SDL põe as graváveis
**depois** dos storage buffers (para não mudar o índice de nada que já existia). Com o layout do SPIR-V/Vulkan (texels
no binding 0, EFB em 1 e 2), o MSL sairia com o EFB em `texture(1)`/`texture(2)` e o buffer de texels em `buffer(-1)`.

Solução: o MSL é gerado com `-DAM_MSL_LAYOUT`, que só troca os bindings do SPIR-V intermediário (EFB em 0 e 1, texels
em 2). Aí a conta do shadercross dá exatamente o que a SDL Metal espera: EFB em `texture(0)` e `texture(1)`, uniform
em `buffer(0)`, texels em `buffer(1)`. O DXIL e o SPIR-V embutidos usam o layout normal. O `build_shaders.sh` confere
os quatro índices no MSL.

### 3.3 Uma só região ordenada (bug do SPIRV-Cross)

A primeira tradução lia e gravava o EFB onde o `.metal` lia, dentro dos desvios (teste de Z antes do TEV, saídas
antecipadas). Os três formatos saíram e passaram no `spirv-val`, mas **o MSL veio sem `raster_order_group`**, sem
erro nem aviso. Os motivos:

- com acessos ao ROV em vários caminhos, o DXC (passe de colocação do interlock do SPIRV-Tools) gera vários pares
  `OpBegin/EndInvocationInterlockEXT` dentro de desvios;
- o SPIRV-Cross (`1a616956`, `Compiler::analyze_interlocked_resource_usage`) marca esse caso como
  `control_flow_interlock` e só conta os recursos acessados quando `call_stack_is_interlocked`. Essa flag só liga ao
  entrar por `OpFunctionCall` na função do interlock. Quando o interlock está na própria função de entrada (o caso
  depois do inlining), ela nunca liga: nenhuma textura é marcada e o `raster_order_group` some.

No Metal isso teria rodado sem ordem entre fragmentos sobrepostos. O teste do patch mostrou o efeito: 4096 de 4096
pixels errados sem o atributo.

Correção no shader, sem mexer no SPIRV-Cross: tudo o que não depende do EFB (TEV, teste de alfa, textura de Z,
neblina) roda antes, numa função `shade`. Depois vem uma região única fora de qualquer desvio: lê as duas texturas,
resolve o PE numa função sem acesso a textura (`pe_resolve` / `clear_resolve`) e grava as duas de volta (o mesmo valor
quando nada muda, como o shader do framebuffer fetch). O SPIR-V passa a ter um só par Begin/End, e o MSL sai com
`raster_order_group(0)` nas duas texturas. O `build_shaders.sh` recusa SPIR-V com mais de um Begin.

Efeitos colaterais, sem mudar a imagem: o TEV roda também nos fragmentos que o Z antes do TEV descartaria (no `.metal`
eles saíam cedo), e a região ordenada fica menor (só o PE, não o TEV inteiro, `docs/M9_PARTE1A_INVESTIGACAO.md` §9).

## 4. Runtime

- `platform_gpu_init(PlatformGpuShader)`. O device é criado com `SDL_CreateGPUDeviceWithProperties`: metallib sempre
  e, com a SDL remendada, também SPIR-V, DXIL e `..._FEATURE_GRAPHICS_STORAGE_READ_WRITE_BOOLEAN`. O modo debug fica
  desligado, como antes.
- `--gpu-shader auto` (padrão): no Metal, `hand`; nos outros backends, `hlsl`. `hand` exige metallib. `hlsl` exige
  `SDL_PROP_GPU_DEVICE_RASTER_ORDERED_STORAGE_BOOLEAN` e escolhe o formato por `SDL_GetGPUShaderFormats`: DXIL, senão
  SPIR-V, senão o metallib do HLSL.
- No caminho HLSL as texturas do EFB têm `SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE` (upload e download pelo
  passe de cópia, como antes). O fragment shader declara 2 graváveis
  (`SDL_PROP_GPU_SHADER_CREATE_NUM_READWRITE_STORAGE_TEXTURES_NUMBER`), e o passe amarra o EFB com
  `SDL_BindGPUFragmentReadWriteStorageTextures`.
- Sem o recurso, sem o patch (`AM_SDL3_SYSTEM=ON`, tudo atrás de `#ifdef SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_WRITE`)
  ou com qualquer erro na criação, o device é liberado e o jogo segue em software:
  `[WARN] --gfx gpu: sem GPU utilizável (<motivo>); o GX desenha em software`. Com isso a thread do GX volta à
  profundidade 0 e o pulo de quadros usa o limite do software.
- O log diz o caminho: `GX: desenho na GPU (driver metal, HLSL (MSL 2.3, metallib), raster order groups, ubershader TEV)`.

## 5. Resultados no Mac (Apple M4)

Builds Release em `/Volumes/AMRecomp/wt-builds/m9-hlsl`, logs em `/Volumes/AMRecomp/logs/m9/p1b-hlsl`, oráculo do
Dolphin copiado de `logs/m8/integracao-2b` (o Dolphin não foi aberto).

### 5.1 Capturas

| Comparação | Resultado |
| --- | --- |
| HLSL x MSL à mão, q1600 / q1800 / q2300 / q6400, arm64 | **idênticas byte a byte** (`cmp`) |
| HLSL arm64 x GPU do M8 (`logs/m8/integracao-2b/gpu_arm64_f*.png`) | idênticas byte a byte nos 4 |
| HLSL x MSL à mão, x86_64 (Rosetta) | idênticas byte a byte nos 4 |
| `--cel`, 7 quadros (q7400 a q8400), arm64 | **idênticas byte a byte** nos 7; GPU x Dolphin 35,33–37,59 dB e software x Dolphin 31,06–32,77 dB, os mesmos do M8 |
| `gpu_shader` (320 triângulos sobrepostos em 8 desenhos, mais uma limpeza; blend com alfa, soma, subtração, XOR, `~s \| d`, alfa do destino, RGBA6 com dither, Z antes e depois) | cor 0 e Z 0 de 337 920 pixels diferentes |

Como as capturas são idênticas, não há PSNR para reportar. Os números contra o software e o Dolphin são os do M8:
GPU x soft 36,91 / 44,56 / 39,21 / 30,65 dB, GPU x Dolphin 39,44 / 43,34 / 39,95 / 34,92 dB.

### 5.2 Verificação

`BUILD_ROOT=/Volumes/AMRecomp/wt-builds/m9-hlsl AM_LOG_DIR=/Volumes/AMRecomp/logs/m9/p1b-hlsl
scripts/verify_m8.sh --no-window`: **OK** (`logs/m9/p1b-hlsl/verify_no_window.txt`).

| Item | arm64 | x86_64 |
| --- | --- | --- |
| ctest | 52/52 (com `gpu_shader` e `sdl_rov`) | 52/52 |
| `--gfx soft` / `--gfx gpu` até o q6400 | 193 s / 87 s | 303 s / 121 s |
| `--gx-thread on` | capturas e timebase idênticos | idem |
| `--gpu-shader hlsl` contra o padrão (`hand`) | 4 capturas idênticas | 4 capturas idênticas |
| tempo emulado | defasagem 0,000 campo no q6400, idêntico nos dois builds | idem |
| PSNR | os mesmos do M8 (§5.1) | idem |
| golden `tests/golden/m8_corrida.json` | igual, sem regravar | |

`verify_m8.sh --cel`: **OK** (`logs/m9/p1b-hlsl/verify_cel.txt`), com a conferência do HLSL nos 7 quadros.

### 5.3 Desempenho (arm64, máquina livre, cada shader duas vezes, alternados)

| Medida | MSL à mão | HLSL (MSL 2.3) |
| --- | --- | --- |
| Janela, menu principal (q2160–2400), quadros/s do jogo / desenhados | 61,0 / 60,0 e 61,0 / 60,0 | 61,0 / 60,0 e 61,0 / 60,0 |
| Janela, corrida (q6240–6600) | 60,6 / 60,0 e 60,6 / 60,0 | 60,6 / 60,0 e 60,6 / 60,0 |
| Janela, espera média pela GPU por leitura do EFB | 4 785 e 4 624 µs | 5 098 e 5 094 µs |
| Sem janela, até o q6400 (`--gfx gpu`, sem a thread do GX) | 87 s, espera média 2 370 µs | 90 s, espera média 2 662 µs |

Na janela os dois ficam no teto de 60 quadros/s. O custo na GPU do HLSL é **~7–12% maior** (espera por leitura do
EFB) e o tempo sem janela é ~3% maior. Prováveis causas: storage em vez da memória de tile, o TEV dos fragmentos que o
Z antes descartaria e as duas gravações incondicionais. **Padrão escolhido no Metal: o MSL à mão** (`--gpu-shader
auto` = `hand`). Os dois dão a mesma imagem byte a byte, e o HLSL fica para os outros backends e para conferência
(`--gpu-shader hlsl`).

## 6. Decisões

- **HLSL único, MSL à mão mantido no Mac.** Era a opção (1) com recuo para (2) de `M9_PARTE1A_INVESTIGACAO.md` §8. A
  queda é pequena, mas existe, e o à mão já está provado. Os dois fontes ficam presos um ao outro: o verify e o
  `gpu_shader` comparam a imagem byte a byte.
- **Metallib, não MSL em texto, para o Metal.** A runtime continua sem compilar shader em tempo de execução. O MSL
  gerado fica versionado só para leitura.
- **`-DAM_MSL_LAYOUT` em vez de remendar o shadercross ou editar o MSL gerado.** É uma troca de números de binding no
  próprio fonte, conferida pelo script.
- **Região ordenada única** em vez de remendar o SPIRV-Cross (§3.3). O mesmo formato serve aos três backends, e a
  região menor deve ajudar o ROV no D3D12.
- **Recuo para o software com aviso** em vez de parar. Isso cobre GPU sem ROV/interlock (AMD GCN até Polaris no
  D3D12, AMD no Vulkan do Windows), a SDL sem o patch e Mac Intel sem framebuffer fetch.

## 7. O que falta testar fora do Mac

Nada disto rodou: não há máquina Windows ou Linux aqui.

| Caso | O que conferir |
| --- | --- |
| D3D12 (WARP e GPU real) | o PSO aceita o DXIL (root signature da SDL com `u0/u1, space2` e o `ByteAddressBuffer` em `t0, space2`); log com `HLSL (DXIL), ROV`; `gpu_shader` adaptado para comparar com o software em vez do MSL à mão (§8); capturas do verify contra as do Mac (devem ser iguais byte a byte se `ddx` *coarse* der o mesmo LOD; senão medir PSNR) |
| D3D12 sem ROV (AMD GCN até Polaris) | aviso e recuo para o software |
| Vulkan (Nvidia, Intel, RADV) | `spirv-val` passou aqui; no device: extensão de interlock ligada, camada de validação sem erros, o viewport da SDL invertendo o Y como no Metal (a mesma conta de `gx_vs`), capturas como acima |
| Vulkan na AMD do Windows (sem interlock) | `RASTER_ORDERED` = `false` e recuo para o software |
| Desempenho | ubershader inteiro com a região ordenada só no PE, nas três marcas |

## 8. Pendências

- O `gpu_shader` precisa dos dois shaders, então fora do Mac sai como pulado. Uma versão que compare o HLSL com o
  `gx_efb.c`/`gx_tev.c` do software (a mesma conta) serviria em qualquer plataforma.
- O DXIL de `ddx` é *coarse* e o Metal não tem essa distinção. Se o D3D12 der LOD diferente nas bordas dos
  triângulos, a troca é `ddx_fine` (que vira `OpDPdxFine` no SPIR-V e `dfdx` no MSL, sem mudar o Mac).
- Vale relatar o bug do SPIRV-Cross (§3.3) ao projeto, com o SPIR-V mínimo de dois Begin/End na função de entrada.

## 9. Lições

- **Um atributo que some sem aviso só aparece quando se procura por ele.** O shader com interlock em desvios passou
  no DXC, no `spirv-val`, no SPIRV-Cross e no compilador Metal; só o `grep raster_order_group` no MSL mostrou o
  problema. Agora o script de build faz esse grep.
- **Igualdade byte a byte na primeira execução não é sorte.** A aritmética é inteira e as poucas contas em ponto
  flutuante (vértice, LOD, neblina) passam pelo mesmo compilador Metal com o mesmo modo de matemática, então o
  caminho HLSL → SPIR-V → MSL não mudou nenhum bit.
