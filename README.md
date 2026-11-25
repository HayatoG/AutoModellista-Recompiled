# Auto Modellista Recompiled

> **Estudo de caso / Case study**
>
> **PT-BR:** este projeto é apenas um estudo de caso, uma pesquisa pessoal sobre recompilação estática e engenharia reversa, e não um produto. O desenvolvimento só continuará no meu tempo disponível, quando possível: sem prazos, sem garantia de conclusão e sem suporte.
>
> **EN:** this project is only a case study, personal research into static recompilation and reverse engineering, not a product. Development will only continue in my available time, when possible: no deadlines, no guarantee of completion and no support.

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
| M8 Corrida | **partes 1 e 2b atingidas localmente** (macOS arm64 e x86_64, Release): tempo emulado igual ao do Dolphin (defasagem no q6400 de 1,37 s para 0,000 campo, com um patch no laço contado do DolRecomp), GX na GPU pela SDL3 GPU (ubershader TEV em MSL compilado offline, EFB lido e escrito pelo shader) a 31–45 dB contra o software e 35–43 dB contra o Dolphin, GX numa thread própria sem esperar no DrawDone (cópia na escrita, tempo e imagens iguais byte a byte); janela a 60 quadros/s desenhados no menu e na corrida no arm64 (x86_64: 60 desenhados, jogo a 48/s na corrida); memory card salvo e lido, `.raw` igual byte a byte ao do Dolphin; cel-shading conferido com o carro andando até o q8400 | `docs/M8_CORRIDA.md` (e `docs/M8_PARTE2B_*.md`), `scripts/verify_m8.sh`, `scripts/verify_card.sh`, `tests/golden/m8_corrida.json`, `tests/golden/m8_card.json` |
| M9–M10 port nativo em C | planejado | `docs/PLANO_PORT_C.md`, padrões em `docs/PADROES_DE_CODIGO.md` |

## Começar

```sh
scripts/fetch_tools.sh
scripts/build_dolrecomp.sh
tests/known_good_demo/run.sh
scripts/phase1_iso.sh "/Volumes/SSD/GameCube/Auto Modellista (USA).zip" /Volumes/SSD/AutoModellista-work
```

O `scripts/fetch_tools.sh` aplica as correções de `patches/dolrecomp/` sobre o DolRecomp fixado; depois de mudar um
patch, recompile o DolRecomp e regenere o C (`scripts/regen_generated.sh <saída>`, docs/M8_PARTE2B_TEMPO.md).

## Jogar

```sh
scripts/assistir.sh jogar        # janela, teclado ou gamepad, som, tempo real; GX na GPU (AM_GFX=soft: software)
```

Opções da runtime que mais importam (a lista completa está no topo de `runtime/main.c`):

| Opção | Padrão | O quê |
| --- | --- | --- |
| `--gfx gpu\|soft\|off` | `soft` (`assistir.sh`: `gpu`) | GX na GPU (Metal, GPU da Apple) ou no rasterizador em software |
| `--gx-thread on\|off` | **on** com janela e `--gfx gpu`; off no resto | GX numa thread própria, como o "dual core" do Dolphin; tempo emulado e imagens iguais aos de off |
| `--gx-depth <0..6>` | 3 | quantos eventos (DrawDone, tokens) o GX pode ficar atrás da CPU; 0 = a CPU espera o GX em cada um |
| `--memcard <arq.raw\|none>` | interativo: `MemoryCardA.raw` no diretório de dados do usuário (macOS: `~/Library/Application Support/AutoModellista/`); roteiros: sem cartão | memory card do slot A, imagem `.raw` do Dolphin (criada e formatada com 4 Mbit se não existir; gravada ao fechar) |
| `--realtime on\|off`, `--audio on\|off` | on no modo interativo (janela sem `--pad-script`) | ritmo do relógio, som |
| `--frameskip on\|off` | on | com `--realtime`, pula o desenho de quadros quando a emulação atrasa |

O `.raw` do cartão tem o save do jogo: nunca o coloque no repositório.
