# Auto Modellista Recompiled

Port por recompilação estática de **Auto Modellista** (Capcom, GameCube) para PC, com runtime própria em C17 sobre SDL3
(macOS, Linux e Windows).

Este repositório não contém nem vai conter nada do jogo. Traga sua própria cópia; ISO, DOL, RELs, assets e código
gerado a partir deles ficam fora do git (`.gitignore`).

## Estrutura

| Pasta | Conteúdo | Versionado |
| --- | --- | --- |
| `docs/` | relatórios por fase | sim |
| `scripts/` | automação escrita à mão | sim |
| `analysis/` | metadados da análise (hashes, mapas, JSON) — sem bytes do jogo | sim (exceto `files/`, `ghidra/`) |
| `recomp/` | configuração do DolRecomp; `out/` é gerado | só config |
| `runtime/` | código da runtime específico do jogo | sim |
| `patches/` | patches de função para o DolRecomp (`--config`) | sim |
| `decomp/` | projeto de matching decompilation (dtk-template), isolado do recomp | só código escrito à mão |
| `tests/` | testes reproduzíveis | sim |
| `logs/` | logs locais | não |
| `tools/` | ferramentas de terceiros em commits fixos | não (ver `scripts/fetch_tools.sh`) |

## Marcos

| Marco | Estado | Evidência |
| --- | --- | --- |
| M0 ferramentas | **parcial**: DolRecomp (C) ok, mwcc/dtk/objdiff ok, Dolphin 2609a (oráculo do M4); faltam LLVM 20, Ghidra (ModernGekko não é mais necessária) | `docs/ENVIRONMENT.md`, `tests/known_good_demo/run.sh` |
| M1 ISO analisada | **atingido** | `docs/ISO_ANALYSIS.md`, `analysis/iso/` |
| M2 recompilação (backend C) | **atingido** | `docs/RECOMPILATION_REPORT.md` |
| M3 esqueleto da runtime | **atingido localmente** (macOS arm64 e x86_64): CMake, `runtime/core`, DOL + 12 RELs linkados (155 trechos), 12 testes; CI Linux/Windows escrito, ainda não executado | `docs/M3_ESQUELETO.md`, `scripts/verify_m3.sh` |
| M4 até `main` | **atingido localmente** (macOS arm64 e x86_64): DOL carregado, estado do IPL/apploader conferido contra o Dolphin, `__start` → `OSInit` → `main` em C recompilado; trace de chamadas igual ao do Dolphin 2609a em todos os eventos comparáveis (1998/1998; 2733/2733 com trechos de 128 instruções; chamadas dentro do mesmo trecho não são visíveis, ver §5.2) | `docs/M4_BOOT.md`, `scripts/verify_m4.sh`, `tests/golden/m4_trace.json` |
| M5–M10 port nativo em C | planejado | `docs/PLANO_PORT_C.md`, padrões em `docs/PADROES_DE_CODIGO.md` |

## Começar

```sh
scripts/fetch_tools.sh
scripts/build_dolrecomp.sh
tests/known_good_demo/run.sh
scripts/phase1_iso.sh "/Volumes/SSD/GameCube/Auto Modellista (USA).zip" /Volumes/SSD/AutoModellista-work
```
