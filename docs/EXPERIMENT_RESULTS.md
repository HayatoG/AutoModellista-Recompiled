# Resultados dos experimentos de FPS (M9 parte 6)

Uma mudança por experimento. Corrida automática de 8.400 quadros (`work/run-perf/corrida.sh`), média e pior janela de
60 quadros no trecho 6.900–8.400, em quadros/s. "Sem desenho" = `--gfx off` (só a CPU emulada); "GPU" =
`--gfx gpu --gx-thread on`, sem janela. A máquina tem ruído de fundo (±30% entre execuções do mesmo exe): os
resultados que contam são o melhor de 2–3 execuções intercaladas (`work/run-perf/bench.sh`), e as medições entre
25/10 23:59 e 26/10 00:30 foram contaminadas por outro processo pesado e repetidas depois.

## CPU emulada (sem desenho)

| # | Mudança | Antes | Depois | Saída igual? | Ficou? |
| --- | --- | --- | --- | --- | --- |
| 1 | Pulo do laço de espera (idle skip) | 48 / 34 | 72 / 51 | timebase e áudio idênticos | sim (`b26cf5f`) |
| 2 | `bus_update` só com evento vencido, cache no despachante | 72 | 74 | idêntica | sim (`b26cf5f`) |
| 3 | Memória forçada em linha pelo ThinLTO (`-inline-cold-callsite-threshold=250`) | 80 | 75 | idêntica | não (exe 42 → 55 MB) |
| 4 | `-march=native -ffp-contract=off` | 80 | 81 | idêntica | só no build local |
| 5 | `dcbf`/`dcbst`/`dcbi` sem voltar ao chassi | 81 / 58 | 86 / 62 | áudio igual, timebase muda pouco | sim (`61f40ff`) |
| 6 | `cpu.c` em `-O2` | 82 | 77 | idêntica | não |
| 7 | Chamadas diretas entre trechos do C gerado | 86 / 62 | 90 / 65 | áudio igual | sim (`61f40ff`) |
| 8 | Orçamento do laço contado 256 → 1024 ciclos | 90 | 91 | áudio diferente | não |
| 9 | Ponto flutuante em linha (sem o modo NI) | 92 | 90 | idêntica | — |
| 10 | Ponto flutuante em linha com NI + gather pipe direto | 91 / 65 | 99 / 73 | idêntica | sim (`4a7df06`) |
| 11 | `mem_write` sem salvar registradores | 98 | 102 / 73 | idêntica | sim (`4a7df06`) |

## Com a GPU

| # | Mudança | Antes | Depois | Imagem igual? | Ficou? |
| --- | --- | --- | --- | --- | --- |
| 12 | Desenhos de mesmo estado juntados num comando | 49,6 | 49,4 | idêntica (quadros 7000, 7600, 8300) | sim, sem ganho (`fa684e5`) |
| 13 | `--gx-depth` 3 → 6 | 49,5 / 35 | 55,9 / 36 | — | sim, no `jogar.bat` |
| 14 | `--gx-depth` 3 → 1 | 49,5 | 24,4 | — | não |
| 15 | Um lógico por núcleo físico (afinidade 0x5555), duas rodadas intercaladas | 59,0 / 38 e 59,7 / 38 | 72,3 / 48 e 73,1 / 48 | — | sim, no runtime (`fa684e5`) |
| 16 | Cache de estado por desenho (gerações de BP e XF) | 58,8 / 38 | 62,6 / 44 | idêntica (3 quadros) | sim (`fa684e5`) |
| 17 | Tudo (#15 + #16 + profundidade 6) | — | 74,2 / 49 | — | — |

## Na janela (partidas do usuário)

| Build | Lógica | Desenhados de 60 | Observação |
| --- | --- | --- | --- |
| Software, original | 20–26 | 15–17 | só software no launcher |
| D3D12, original | 30–51 | — | música engasga com carros à frente |
| `dcbf` | 47–55 | — | — |
| Chamadas diretas | 57 (mín. 45) | 51 | 27 atrasos de áudio |
| Tudo até o FP em linha, profundidade 3 | 59–60 | 40–41 | o overlay mostrava 60 |
