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
| M5 OS e disco | **atingido localmente** (macOS arm64 e x86_64, Release): RELs ligados pelo `OSLink` do SDK (menus e, na largada, o `game.rel`), DVD com o tempo do drive do Dolphin, laço de quadros com roteiro de controle até a corrida; `game.rel` ligado no quadro 5731 e 669 quadros depois sem erro; 12 eventos de módulo nos mesmos quadros do Dolphin 2609a e 91 leituras de disco idênticas, música ADX em fluxo com a mesma contagem por trecho (§5.3) | `docs/M5_OS_DISCO.md`, `scripts/verify_m5.sh`, `tests/golden/m5_events.json` |
| M6 Primeiro quadro | **atingido localmente** (macOS arm64 e x86_64, Release): GX em software (FIFO, vertex loader, XF, BP, texturas, TEV, rasterizador, EFB, cópia EFB -> XFB na RAM) e janela SDL3; logo da Capcom no mesmo quadro que no Dolphin 2609a, captura a 46,4 dB (logo) e 49,2 dB (fade) sem pixels com diferença visível, iguais bit a bit nos dois builds | `docs/M6_PRIMEIRO_QUADRO.md`, `scripts/verify_m6.sh`, `tests/golden/m6_frame.json` |
| M7 Menus | **atingido localmente, com ressalva de desempenho** (macOS arm64 e x86_64, Release): teclado e gamepad pelo SDL3 (mapeamento padrão do Dolphin), HLE completo do AX com som pelo SDL3 e `--audio-dump` igual ao `dspdump.wav` do Dolphin amostra por amostra (correlação 1,0 nas janelas de uma fonte, nível a 0,0 dB), filme da abertura (Sofdec, decodificado pelo código do jogo) e menus a 31–41 dB contra o Dolphin; rasterizador em faixas paralelas e cache de texturas com a imagem idêntica byte a byte; menus a 18–22 quadros/s desenhados no arm64, com a janela em tempo real pulando quadros para manter o jogo a 60 | `docs/M7_MENUS.md`, `scripts/verify_m7.sh`, `tests/golden/m7_menus.json` |
| M8 Corrida | **parte 1 atingida localmente, com ressalva de desempenho na corrida** (macOS arm64 e x86_64, Release): tempo emulado igual ao do Dolphin (defasagem no q6400 de 1,37 s para 0,035 s; câmera da largada de 20,9 para 29,4 dB), GX na GPU pela SDL3 GPU (ubershader TEV em MSL compilado offline, EFB lido e escrito pelo shader, cópias EFB -> XFB/textura na CPU) a 31–45 dB contra o software e 35–43 dB contra o Dolphin; janela a 60 quadros/s desenhados nos menus e 31 na corrida (arm64). Parte 2: cel-shading, memory card, 60 na corrida | `docs/M8_CORRIDA.md`, `scripts/verify_m8.sh`, `tests/golden/m8_corrida.json` |
| M9–M10 port nativo em C | planejado | `docs/PLANO_PORT_C.md`, padrões em `docs/PADROES_DE_CODIGO.md` |

## Começar

```sh
scripts/fetch_tools.sh
scripts/build_dolrecomp.sh
tests/known_good_demo/run.sh
scripts/phase1_iso.sh "/Volumes/SSD/GameCube/Auto Modellista (USA).zip" /Volumes/SSD/AutoModellista-work
```
