# M9, parte 1b: SDL_shadercross funcionando no Mac

**Resultado: funciona.** O `shadercross` compila HLSL para DXIL, SPIR-V e MSL 2.3 no Mac arm64, e o teste de ROV
passa nos três alvos. Seguimento de `docs/M9_PARTE1A_INVESTIGACAO.md` (§5).

## Versões

| Componente | Versão |
| --- | --- |
| SDL_shadercross | commit `1ff05be` ("policy: Added LLM policy") |
| DXC | fork `libsdl-org/DirectXShaderCompiler`, ramo `1.8.2502-SDL`, commit `2c84a1c5` |
| SPIRV-Cross | `1a616956` (C API 0.68.0) |
| SPIRV-Tools / SPIRV-Headers | `0539c81f` / `ad9184e7` (mais os do próprio DXC: `fbe4f3ad`, `04f10f65`) |
| SDL3 | **não é submódulo**: usa o Homebrew (3.4.14, `/opt/homebrew/opt/sdl3`) |
| Clang/Metal | Apple metal 32023.921 (Xcode atual) |

## Build

`scripts/build_shadercross.sh` (zsh): clona/atualiza `/Volumes/AMRecomp/ext/shadercross-src` (semente local
`ext/SDL_shadercross`), faz checkout do commit, `submodule update --init --recursive --depth 1`, configura com
`-DSDLSHADERCROSS_VENDORED=ON -DSDLSHADERCROSS_DXC=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64`
(Ninja), compila em `ext/shadercross-build` e instala em `ext/shadercross`. Logs em `logs/shadercross_*.log`.

- **Tempo: ~4 min** (1303 alvos, `-j10` no M4; a estimativa era 40-90 min, o DXC sai bem mais rápido com Ninja).
- **Espaço:** fonte 316 MB, build 191 MB, instalação 32 MB. Nada no disco interno.
- Binário: `/Volumes/AMRecomp/ext/shadercross/bin/shadercross`; libs em `ext/shadercross/lib`
  (`libdxcompiler`, `libdxil`, `libspirv-cross-c-shared`, `libSDL3_shadercross`; rpath `@loader_path/../lib`).
- O binário liga com a `libSDL3` do Homebrew (aviso do linker "macOS-11.0 x 26.0" é inofensivo).

## Flags do shadercross

```
shadercross <entrada.hlsl> -s HLSL -d {DXIL|SPIRV|MSL} -t {vertex|fragment|compute} -e <entrada> -o <saída> [--msl-version X.Y.Z]
```

- `--msl-version 2.3.0` pede MSL 2.3 (o padrão é **1.2.0**, que não tem `raster_order_group`; com o padrão o MSL do
  teste sai sem ele). Outras opções: `-I`, `-D`, `-c` (descartar bindings sem uso), `-g`. Não há `--version`.
- Um arquivo com dois estágios precisa de `-t` e `-e` por estágio (`vs_main` e `ps_main` no teste).
- O MSL gerado é compilado com `xcrun -sdk macosx metal -std=macos-metal2.3 -c`.

## Teste

`tests/shaders/rov_min.hlsl` (vertex trivial e fragment que lê, soma 1 e grava um
`[[vk::binding(0, 2)]] RasterizerOrderedTexture2D<uint> efb : register(u0, space2)`) e `scripts/check_shadercross.sh`
(sai com 0 se tudo ok; saídas em `ext/shadercross-check/`). Resultado:

```
ok:    DXIL gerado (3320 bytes no fragment)
ok:    SPIR-V com FragmentShaderPixelInterlockEXT, PixelInterlockOrderedEXT e OpBegin/EndInvocationInterlockEXT
ok:    MSL 2.3.0 com raster_order_group
ok:    xcrun metal compilou vertex e fragment
shadercross OK
```

- SPIR-V (`spirv-dis`): `OpCapability FragmentShaderPixelInterlockEXT`, `OpExecutionMode PixelInterlockOrderedEXT`,
  `DescriptorSet 2`, `Binding 0`, `OpBegin/EndInvocationInterlockEXT` em volta do acesso.
- MSL: `fragment void ps_main(texture2d<uint, access::read_write> efb [[texture(0), raster_order_group(0)]], ...)`,
  com `efb.fence()` antes de ler e gravar.

## Problemas encontrados

- Nenhum no build. Só: a SDL3 não vem nos submódulos (usa o Homebrew); `--version` não existe no CLI; o MSL padrão
  1.2 precisa ser elevado com `--msl-version`.
- Observação para o pipeline: o DXIL é gerado, mas só roda em Windows (D3D12); o `dxil.dll` vendored do macOS é
  a biblioteca de validação/assinatura (`libdxil.dylib`).
