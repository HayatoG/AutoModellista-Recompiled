# Relatório de recompilação estática (marco M2)

Estado: **M2 atingido para o backend C** — `main.dol` e os 12 RELs do jogo foram processados sem instruções
desconhecidas, todo o "embedded data" foi explicado, os alertas de SMC do DOL foram triados e todo o C gerado
compila para arm64. Link/execução dependem da runtime (ver `RUNTIME_EVALUATION.md`).

## 1. DolRecomp — build e testes

| Item | Resultado |
| --- | --- |
| Commit testado isoladamente | `02e27aca1c408cfcb2a4079a3a5b5ab3ae39d340` (HEAD em 2026-10-10) |
| Commit do template oficial | `1bec3554ecc4817cf78319ca3d8a0669477f29fa` (usado pela pipeline ModernGekko) |
| Build | `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release`, ~2 s no M4, 3 warnings |
| Erro inicial | `tomlc17 submodule is missing` → `git submodule update --init --recursive` |
| Testes | **20/20 passaram** (`logs/dolrecomp_ctest.log`). O teste de paired singles só entra no build LLVM |
| Backend LLVM | **não testado**: requer LLVM 19/20; só há LLVM 21 instalado |

## 2. Exemplo compatível conhecido

"Demo Quest GC" do decomp.wtf (CC0 + zlib 1.1.4), CodeWarrior GC 1.3.2 `-O4,p`, layout DOL padrão:
10.088 instruções, 0 desconhecidas, sem SMC; 5/5 unidades compilam. `tests/known_good_demo/run.sh`.

## 3. Auto Modellista — main.dol

```
dolrecomp --gamecube -j8 main.dol dol          # 0,5 s
  text[0] .init: 2416 decodificadas,    773 conhecidas, 1643 embedded data, 0 desconhecidas
  text[1] .text: 338720 decodificadas, 338712 conhecidas,    8 embedded data, 0 desconhecidas
  84 chunks, 84 MB de C
scripts/compile_generated.sh …/dol/generated …/dol/obj   # 36 s, 10 jobs
  objetos: 85  falhas: 0   (15 MB de .o arm64)
```

### Perda silenciosa de código — verificada: nenhuma

| Palavras como "embedded data" | Onde | Conclusão |
| --- | --- | --- |
| 1.621 | 0x800034AC–0x800053E0 | `gTRKInterruptVectorTable` do MetroTRK (copiada para vetores de exceção pelo depurador; não é código executado no lugar) |
| 22 | 0x80005624–0x800056BC | `_rom_copy_info` e `_bss_init_info` (tabelas do linker) |
| 2 | 0x800F5178, 0x800F517C | `.long 0` (padding entre funções) |
| 6 | 0x80150328–0x8015033C | após o fim real do `.text` (dtk: 0x14AC68); região de extab/alinhamento |

### Alertas de SMC (581) — triados

A heurística do DolRecomp (`src/analysis/smc.c`) faz uma varredura linear rastreando registradores carregados por
`lis/addi/ori`, e só esquece um valor em poucas instruções; não invalida em `lwz`, `add`, chamadas nem fronteiras de
função. `scripts/smc_triage.py` procura a definição real do registrador-base dentro da função:

| Qtde | Classificação |
| --- | --- |
| 577 | store cujo registrador-base vem de argumento/fora da função (`r3`–`r7`, `r29`, `r30`) → **falso positivo** |
| 4 | `icbi` em `__flush_cache`, `ICInvalidateRange`, `fn_800F37F4` e na área do TRK → manutenção de cache do SDK, não geração de código |

Nenhuma geração de código em tempo de execução foi encontrada no DOL. Limitação: a triagem para em `b`/`blr`
(aproximação de fronteira de função); os 4 `icbi` e `fn_800F37F4` devem ser revistos quando a runtime rodar
(`analysis/smc_triage.txt`, `analysis/smc_by_function.txt`).

## 4. Auto Modellista — RELs

```
dolrecomp --gamecube -j8 rels_in rels           # 0,3 s; rels_in = 12 RELs, sem test.rel
scripts/compile_generated.sh …/rels/generated …/rels/obj   # 32 s
  objetos: 72  falhas: 0
```

| Módulo | ID | Base virtual atribuída | Instruções | Desconhecidas | Relocações | SMC |
| --- | --- | --- | --- | --- | --- | --- |
| game | 1 | 0x80500000 | 78.921 | 0 | 11.282 | 0 |
| menu | 2 | 0x80570000 | 89.829 | 0 | 16.060 | 9 |
| menu00 | 3 | 0x80600000 | 23.354 | 0 | 3.971 | 13 |
| menu01 | 4 | 0x80630000 | 29.693 | 0 | 5.278 | 0 |
| vj | 5 | 0x807B0000 | 32.225 | 0 | 4.388 | 0 |
| msg_e…msg_s | 6–12 | 0x80660000… | 120–147 cada | 0 | ~6.400 cada | 0 |

`test.rel` (ID 3 duplicado com `menu00`, ausente de `rave.str`) foi excluído para não colidir na resolução de
imports. Os 22 alertas de SMC dos RELs (9 em `menu`, 13 em `menu00`) são todos `stb/stw/stfs` em campos de struct
via `r3` (primeiro argumento) — mesma classe de **falso positivo** do DOL.

## 5. Dependências externas

O código gerado chama a runtime para: acesso à memória emulada, dispatch indireto (`dolrecomp_indirect_dispatch`),
instruções de sistema (`mtspr`, `sc`, cache) e HLE. O DOL depende do SDK embutido nele (OS, GX, DVD, AX, CARD…) —
tudo isso é código do próprio jogo, recompilado; o hardware por trás (CP/XF/BP, DSP, DI, SI, EXI) é emulado pelo
Dolphin dentro da ModernGekko.

## 6. Lacuna específica do jogo: RELs fora do módulo nativo

- O `moderngekko-port` só passa `sys/main.dol` ao DolRecomp (e `files/_Main.rel`, padrão de alguns jogos Wii).
- O RecompCore **já sabe** mapear RELs em tempo de execução (`StaticRecompCore::RefreshRelSections` varre a RAM
  procurando cabeçalhos REL que batem com `ModernGekkoRelModule` e traduz endereço real ↔ `linked_start`).
- Mas o `module-template/module_export.c` não preenche `rel_modules`, e o DolRecomp gera DOL e RELs como saídas
  separadas, cada uma com seu `dolrecomp_call`.

Consequência: com a pipeline padrão, **~43% das instruções (todo o código de `game`, `menu`, `vj`…) rodariam no
JIT de fallback**. Para o primeiro boot isso é aceitável; para chamar de port nativo, precisamos de um módulo
combinado DOL+RELs com `rel_modules` preenchido. Tarefa registrada como prioritária após M4.

## 7. Comparação de backends

| Critério | C | LLVM |
| --- | --- | --- |
| Disponível nesta máquina | sim | não (precisa `llvm@20`) |
| Tempo (DOL) | 0,5 s geração + 36 s clang -O1 | — |
| Saída | 84 MB C → 15 MB .o | objetos nativos direto |
| Uso | primeiro boot, depuração | otimização, range builds por perfil |

## 8. Alternativa avaliada: Wiicompiled

Tradutor genérico (manifesto YAML), sem JIT, gráficos via aurora; runtime acoplada ao Mario Kart Wii, exige .NET 8.
Não migrar agora; é a referência para um port 100% nativo no futuro.
