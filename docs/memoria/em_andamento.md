# Onde paramos (25/10/2026, à tarde)

Master limpo, com o launcher (parte 5) juntado. Nenhum agente rodando.

## Próximo: M9 parte 2, Windows (o usuário vai continuar pela máquina Windows dele, com o Claude Code lá)
1. **2a**: build Windows x86\_64. No Windows, com o clang-cl/MSVC ou o llvm-mingw; no Mac já existe o
   llvm-mingw 20261020 e a SDL remendada compila com ele (backends d3d12 e vulkan). Fazer: toolchain, dados em
   `%APPDATA%\AutoModellista` (o launcher já usa `SDL_GetPrefPath`), `--gpu-driver d3d12|vulkan` (ou o hint
   `SDL_GPU_DRIVER`), DXIL no D3D12; trocar `ddx/ddy` por `ddx_fine/ddy_fine` no LOD **se** o Mac continuar idêntico
   (`scripts/build_shaders.sh hlsl` só roda no Mac, porque o shadercross foi compilado lá; no Windows, regenerar
   exige compilar o shadercross ou usar o DXC oficial do Windows); pacote com `automodellista.exe`, `SDL3.dll`,
   `rodar.bat`, `LEIAME.txt`, `scripts/package_windows.sh`, `docs/M9_PARTE2A_WINDOWS.md`.
2. **2b**: testar de verdade: D3D12 na GPU e no WARP, Vulkan, software, áudio, gamepad, memory card, desempenho,
   e os "farelos" vistos no Linux.
3. Depois: a lentidão do Linux (ver `status.md`), CI no GitHub (o repositório ainda não tem remote; criar privado
   com o ok do usuário), parte 4 (velocidade x86\_64).
