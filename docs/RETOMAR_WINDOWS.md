# Retomar no Windows

Guia para continuar o M9 numa máquina Windows com o Claude Code. Leia antes `CLAUDE.md`, `docs/memoria/README.md`,
`docs/memoria/status.md` e `docs/memoria/em_andamento.md`.

## 1. O que vem do Mac e o que não vem
- **Vem pelo Git** (repositório privado ou `git bundle`): runtime, shaders (inclusive os headers gerados do HLSL:
  DXIL e SPIR-V já embutidos), patches da SDL e do DolRecomp, scripts, testes, goldens, docs e esta memória.
- **Não vem pelo Git**: a ISO e o **C gerado do jogo** (derivado do executável do jogo; nunca no GitHub). Ou ele é
  gerado de novo aqui a partir da sua ISO (passo 3), ou copiado à parte do Mac (pasta `/Volumes/AMRecomp/recomp/m5`,
  com `dol/` e `rels/`) por pendrive ou rede.

## 2. Ferramentas no Windows
- Git, CMake ≥ 3.28, Ninja, Python 3, e um compilador: **LLVM/clang-cl** (com o Visual Studio Build Tools, para o SDK
  do Windows) ou o **llvm-mingw** (o mesmo usado no Mac, release 20261020, alvo `x86_64-w64-mingw32`).
- O dtk (decomp-toolkit, `dtk-windows-x86_64.exe`, 1.8.4+) para extrair a ISO/RVZ.
- Drivers de vídeo atualizados (D3D12 com ROV; Vulkan com `VK_EXT_fragment_shader_interlock` é opcional).

## 3. Arquivos do jogo e C gerado
```
dtk-windows-x86_64.exe disc extract "Auto Modellista (USA).rvz" C:\jogos\automodellista
certutil -hashfile C:\jogos\automodellista\sys\main.dol SHA256
```
O SHA-256 tem de ser `75b22d528ebf702972931f901f48ce0e81cbf2b98567b3471d75182c1bdee71c` (GAUE08 rev 0).
Para gerar o C: `scripts/fetch_tools.sh` (baixa o DolRecomp no commit fixo e aplica `patches/dolrecomp/`),
`scripts/build_dolrecomp.sh` e `scripts/regen_generated.sh <saída>`. Os scripts foram escritos para macOS/Linux (zsh,
bash): no Windows, use o Git Bash/MSYS2 ou WSL para eles, ou adapte. Se for mais fácil, copie o C gerado do Mac.

## 4. Build e teste
- Configure com `-DAM_GENERATED_DIR=<pasta com dol/ e rels/>` (veja `scripts/package_linux.sh` como modelo).
- Rode: `automodellista.exe --sys C:\jogos\automodellista\sys` (abre a tela inicial) ou com `--no-launcher --window 2
  --gfx gpu`. `--gfx soft` para comparar. O fim do log traz velocidade, esperas do GX e áudio.
- Siga a lista do `em_andamento.md` (parte 2a e 2b).

## 5. Ao terminar cada etapa
Atualizar os roteiros (`docs/ROTEIRO.md`, `docs/ROTEIRO_VIDEO.md` e os docs do Claude, ver `docs/memoria/roteiros.md`)
e esta memória (`docs/memoria/*.md`), e commitar. Sem push sem o ok do usuário.
