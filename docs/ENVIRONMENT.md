# Ambiente e ferramentas (marco M0)

Verificado em 2026-10-22. Tudo abaixo foi executado de fato; o que é hipótese está marcado.

## Máquina

| Item | Valor |
| --- | --- |
| Hardware | MacBook Air, Apple M4, 16 GB RAM |
| SO | macOS 27.0.1 (26A434), Darwin 27.0.0, arm64 |
| Disco interno | 228 GiB, **8,3 GiB livres** → builds grandes (Dolphin/RecompCore, LLVM, Ghidra) devem ir para o SSD externo |
| SSD externo | `/Volumes/SSD` — **desconectado** no momento da verificação; contém a ISO e `DecompAM.sparsebundle` |

## Ferramentas instaladas

| Ferramenta | Versão | Origem / caminho | Estado |
| --- | --- | --- | --- |
| Apple clang | 21.0.0 (clang-2100.3.34.2) | Xcode CLT | OK |
| CMake | 4.4.3 | Homebrew | OK |
| Ninja | 1.13.2 | Homebrew | OK |
| pkgconf | 3.0.7 | Homebrew | OK |
| SDL2-compat / SDL3 | 2.32.70 / 3.4.14 | Homebrew | OK |
| LLVM | 21.1.8 (`llvm@21`) | Homebrew | **Incompatível**: DolRecomp exige LLVM 19 ou 20 para o backend LLVM. `llvm@20` 20.1.8 está disponível no Homebrew, não instalado |
| Python | 3.14 (sistema), 3.12.13 (venv do decomp.wtf via uv) | | OK |
| Rust/cargo | presente | `~/.cargo` | OK |
| Java | Corretto 17.0.17 | | Ghidra 11.x+ exige JDK 21 → instalar JDK 21 junto com o Ghidra |
| Ghidra | — | | **Não instalado** |
| Dolphin | — | | **Não instalado** (necessário como referência de execução/depurador) |
| .NET 8 SDK | — | | Não instalado (só seria necessário para o Wiicompiled) |
| `gh` | autenticado como HayatoG | | OK |

## Toolchains do decomp.wtf (`~/.local/share/decompnet/toolchains`)

| Ferramenta | Versão | Observação |
| --- | --- | --- |
| dtk (decomp-toolkit) | 1.8.4 (`a0c455e`) | tem `disc info/verify/extract`, `dol info/config/split` |
| objdiff-cli | 3.8.2 | |
| mwcc | GC 1.0 … 3.0a5.2, Wii, ProDG, X360 | CodeWarrior GC 1.3.2 **testado e funcionando** via wibo |
| wibo | Mach-O x86_64 | roda sob Rosetta 2 |
| gc-wii-binutils | powerpc-eabi-* | |
| m2c | venv | |
| dtk-template | | base para o projeto de matching |

## decomp.wtf

- Instalação: `~/Docs/Projetos-Pessoais/decomp.wtf`, fork `HayatoG/decomp.wtf` com upstream `i12bp8/decomp.wtf`.
- Versão: 0.10.6, commit `575b4a3` "Discontinue decomp.wtf" (15/10/2026). **Projeto descontinuado/arquivado**; o autor
  pede que não se compartilhe o código gerado e que se respeitem projetos que recusam contribuições feitas por IA.
- Plataforma oficial: Linux e Windows (WSL). **macOS não é suportado oficialmente**, mas a instalação local roda
  (toolchains via wibo/Rosetta; `iniciar.sh` sobe Ollama + `uv run decomp`).
- O projeto do Auto Modellista (`~/Decomp` → `/Volumes/DecompAM/Decomp`) mora numa imagem APFS
  `/Volumes/SSD/DecompAM.sparsebundle`. No momento da verificação, a imagem estava **anexada sem volume montado**
  (o SSD foi removido sem ejetar). Não foi tocada.
- Alternativa mantida: `n-delic/decompdecomp` (AGPL-3.0, ativo em 22/10/2026). Não instalada; avaliar só se a
  instalação local quebrar.

## Ferramentas de recompilação (clonadas em `tools/`, commits fixos em `scripts/fetch_tools.sh`)

| Repo | Commit | Data | Papel |
| --- | --- | --- | --- |
| ExpansionPak/DolRecomp | `02e27ac` | 2026-10-10 | recompilador estático (GPL-3.0) |
| ExpansionPak/ModernGekko | `a2e4e2b` | 2026-09-17 | runtime (GPL-3.0, derivada do Dolphin via RecompCore) |
| ExpansionPak/ModernGekko-Template | `c2553a9` | 2026-09-16 | template oficial atual |
| snazzmaster/ModernGekkate | `cbb2fc6` | 2026-07-26 | fork **mais antigo** do template acima; usar o oficial |
| patchzyy/Wiicompiled | `4c166a7` | 2026-10-19 | alternativa (ver RECOMPILATION_REPORT.md) |

## Reproduzir

```sh
scripts/fetch_tools.sh          # clona nos commits fixos
scripts/build_dolrecomp.sh      # build + 20 testes do DolRecomp
tests/known_good_demo/run.sh    # DOL CodeWarrior conhecido -> C -> objetos arm64
```
