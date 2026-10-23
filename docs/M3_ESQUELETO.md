# M3: esqueleto da runtime

Estado: **atingido localmente** (macOS arm64 e x86_64 via Rosetta). CI para Linux e Windows escrito, **ainda não executado**
(ver §7). Critério de saída do plano (`PLANO_PORT_C.md` §4): o executável liga o C gerado inteiro (DOL + 12 RELs) sem
símbolo pendente; `clang-format` e `clang-tidy` limpos.

## 1. O que existe

```
CMakeLists.txt              opções, flags estritas/de gerado, função am_apply_strict
runtime/CMakeLists.txt      alvos am_ppc, am_core, am_recomp, am_platform e o executável automodellista
runtime/cmake/              AmRecomp.cmake + 2 modelos (.in) que ligam o C gerado
runtime/include/am/         headers públicos: types, compiler, log, mem, bus, guest_cpu, dispatch, recomp, platform
runtime/core/               log (am_log/am_fatal/AM_ASSERT), mem, dispatch, guest_cpu, recomp_stub
runtime/hw/bus.c            barramento MMIO (tudo am_fatal por enquanto)
runtime/platform/           SDL3: só SDL_Init(0)/SDL_Quit
runtime/main.c              inicializa, registra o C gerado, confere entradas, encerra
tests/unit/                 am_test.h + mem, dispatch, guest_cpu + fatal_test (cenários de am_fatal)
.github/workflows/ci.yml    clang-format + matriz macOS arm64 / Linux x86_64 / Windows x86_64 (clang-cl)
scripts/                    ci_fetch_dolrecomp.sh, check_generated_symbols.sh, verify_m3.sh
```

Alvos CMake:

| Alvo | Conteúdo | Flags |
| --- | --- | --- |
| `am_ppc` | `tools/DolRecomp/src/cpu/cpu.c` (semântica PowerPC) | warnings off |
| `am_core` | `core/*.c`, `hw/bus.c` | `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Werror` |
| `am_recomp` | os 155 `chunks/*.c` + arquivos-ponte gerados no build | `-w -O1 -g0` |
| `am_platform` | `platform/platform.c` + SDL3 | estritas |
| `automodellista` | `main.c` | estritas |

Opções: `AM_WITH_GENERATED` (ON), `AM_GENERATED_DIR` (diretório que contém `dol/` e `rels/`; também lido da variável de
ambiente de mesmo nome), `AM_DOLRECOMP_DIR` (padrão `tools/DolRecomp`), `AM_SDL3_SYSTEM` (OFF: SDL3 via FetchContent na
tag fixa `release-3.4.18`), `AM_SANITIZE` (ASan + UBSan nos alvos estritos), `AM_BUILD_TESTS` (ON).

## 2. Como o C gerado se encaixa

- O gerado exige da runtime exatamente 8 funções de memória (`mem_read{8,16,32,64}` e `mem_write*`, com `CPUState *`)
  e 34 funções `ppc_*` (FP, paired singles, exceções, `rfi`, `sc`…). Lista reproduzível:
  `scripts/check_generated_symbols.sh <dol/obj> <rels/obj>`.
- **`mem_*` são nossos** (`core/mem.c`). Mantêm a assinatura com `CPUState *` porque é o que o gerado chama; a memória é
  um singleton do módulo, então código do host passa `NULL`. Desvio dos PADROES §4, que falavam em `mem_read32(addr)`.
- **`ppc_*` vêm do `cpu.c` do DolRecomp** (GPL-3.0, mesmo commit do gerador), compilado como `am_ppc`. Reescrever ~1700
  linhas de semântica de FP do Gekko (FMA, `fctiw`, paired singles, quantização) não é trabalho do M3 e já está testado
  pelo upstream. O `cpu.c` também define `mem_read*/mem_write*`; no `am_ppc` esses oito nomes são renomeados por `-D`
  (`dolrecomp_mem_read32`…), de modo que os usos internos dele (`ppc_psq_*`, `ppc_lswx`) continuam lendo `cpu->ram`,
  que aponta para a nossa MEM1. Acessos fora da RAM caem em `cpu->external_read/write`, ligados ao nosso barramento.
- `dolrecomp_call_depth` (a `extern` exigida pelo gerado) também é definida no `cpu.c`.

## 3. Decisão: DOL + RELs no mesmo executável

**Investigação** (`scripts/check_generated_symbols.sh` sobre os `.o` de `compile_generated.sh`):

| Fato | Resultado |
| --- | --- |
| Símbolos globais definidos | 84 no DOL, 71 nos RELs (todos `func_<endereço>`) |
| Duplicados entre DOL e RELs, ou dentro de cada saída | **0** |
| Por quê | cada função se chama `func_<endereço virtual>` e cada REL recebeu base virtual própria (`RECOMPILATION_REPORT` §4), então os nomes não colidem |
| Outros símbolos globais | nenhum: o resto do `generated.h` é `static inline` (uma cópia por unidade) |
| `generated.c` | só comentários e `#include`; **não é compilado nem linkado** |
| Cuidado do script antigo | `compile_generated.sh` grava `$OBJ/<basename>.o`, então os 12 `generated.c` dos RELs viram um único `generated.o` (por isso "72 objetos" = 71 trechos + 1) |

**Decisão: nenhuma renomeação de símbolos, nada de `objcopy`, nenhuma regeneração.** O C gerado atual já linka junto.
O problema real é outro: cada módulo tem o próprio `generated.h` com os mesmos nomes inline (`dolrecomp_find_original`,
`dolrecomp_call`…), cada um só conhece as funções do próprio módulo, e dois deles não podem entrar na mesma unidade C.
Chamadas entre módulos (DOL para REL e vice-versa) já saem do gerado como "devolve `pc` ao chassi", o que o dispatcher
resolve; não há chamada direta entre módulos.

Solução (`runtime/cmake/AmRecomp.cmake`): para cada módulo o CMake gera, **no diretório de build**, um arquivo-ponte
(`module_shim.c.in`) que inclui só o `generated.h` dele (`INCLUDE_DIRECTORIES` por arquivo-fonte) e expõe
`recomp_mod_<id>_register()`, que registra no dispatcher um intervalo `[início, fim)` por trecho. Os inícios vêm das
declarações `void func_XXXXXXXX(CPUState* ctx);` do `generated.h`; o fim é achado perguntando a
`dolrecomp_find_original` do próprio módulo. Nada gerado é editado e nada precisa ser regenerado, então não há script
de regeneração. Resultado em execução: 155 trechos, 13 módulos, os 13 pontos de entrada e `OSInit`/`main` resolvem no
dispatcher.

Limite conhecido: os RELs estão em bases **virtuais**. Em tempo de execução cada REL é carregado onde o `OSLink` decidir;
a tradução `pc - base_real + base_virtual` entra no M5 (`PLANO_PORT_C.md` §2.1). No M3 o dispatcher só conhece endereços
virtuais.

## 4. Runtime

- **Memória** (`mem.h`): MEM1 de 24 MiB e ARAM de 16 MiB em `calloc`, acessos big-endian por soma de bytes (sem
  dependência da ordem do host). `0x80xxxxxx` e `0xC0xxxxxx` são o mesmo bloco; o resto (MMIO `0xCC…`, cache bloqueado
  `0xE0…`, endereços baixos, acesso que cruza o fim) vai ao barramento. A ARAM só é acessível por funções próprias
  (`mem_aram_*`), como no hardware. Escrita na MEM1 cancela a reserva de `lwarx`/`stwcx` no mesmo bloco de 32 bytes.
  O diário de escritas (`ppc_set_mem_write_journal`) é da ModernGekko e não é usado.
- **Barramento** (`hw/bus.c`): `bus_read/bus_write` são `am_fatal` com o nome do bloco (CP, PE, VI, PI, MI, DSP, DI, SI,
  EXI, AI, WGPipe).
- **CPUState** (`guest_cpu.c`): liga `ram`, `external_read/write` (barramento) e os demais callbacks:

  | Callback | M3 |
  | --- | --- |
  | `external_read32/write32`, `instruction_fallback`, `spr_write` | `am_fatal` |
  | `spr_read` | SPR 287 (PVR) devolve `PPC_GEKKO_PVR`; qualquer outro é `am_fatal` |
  | `host_call` | devolve `false` ("não interceptei"); é a resposta correta sem HLE, não um stub. `am_fatal` só para a consulta de região nativa (`0xFFFFFFFC`) |
  | `cache_control` | sem efeito (host coerente, sem JIT) |
  | `external_pointer` | `NULL` (o gerado não usa) |

  `instruction_fallback` é chamado pelo gerado para `mtsprg*`, `mfdar`, `icbi` etc. na tabela de vetores do TRK
  (0x80003440…). Vai ser o primeiro `am_fatal` real do M4, se o boot passar por ali.
- **Dispatcher** (`dispatch.h`): vetor ordenado de `{start, end, fn}`, inserção por busca binária, rejeita intervalo
  vazio, desalinhado, nulo ou sobreposto; `dispatch_lookup` é busca binária; `dispatch_step` chama a função que cobre
  `cpu->pc` e é `am_fatal` se não houver. O laço de execução (`while (pc)`) é do M4.
- **Erros** (`log.h`): `am_fatal` imprime mensagem, `pc`, `lr` e `ctr` da CPU registrada e sai com código 70
  (`AM_EXIT_FATAL`); `AM_ASSERT` idem, ativo em Release.
- **Plataforma**: `platform_init()` faz `SDL_Init(0)`, sem janela.

## 5. Testes

`ctest` roda 12 testes: `mem` (endianness, espelhos, limites de MEM1, reserva, ARAM), `dispatch` (intervalos
semiabertos, ordem, sobreposição, crescimento, `dispatch_step`), `guest_cpu` (callbacks, PVR, `ppc_lswx` lendo a nossa
memória) e 9 cenários `fatal_*` que confirmam, pela mensagem, que cada stub para com `am_fatal` (MMIO de leitura e
escrita, endereço sem mapeamento, SPR de leitura e escrita, instrução sem recompilação, pc sem função, ARAM fora do
limite, `AM_ASSERT`). `tests/unit/am_test.h` traz `TEST`, `RUN_TEST`, `EXPECT_EQ/NE/TRUE/NULL/STREQ`. Nenhum byte do jogo.

## 6. Reproduzir

```sh
scripts/fetch_tools.sh                      # DolRecomp no commit fixo (só precisa de src/cpu/)
export AM_GENERATED_DIR=/Volumes/AMRecomp/recomp   # contém dol/generated e rels/generated/rels/*

cmake -S . -B /Volumes/AMRecomp/build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /Volumes/AMRecomp/build-debug
ctest --test-dir /Volumes/AMRecomp/build-debug --output-on-failure
/Volumes/AMRecomp/build-debug/runtime/automodellista

# x86_64 (Rosetta): acrescente -DCMAKE_OSX_ARCHITECTURES=x86_64
# sem o jogo (como no CI):        acrescente -DAM_WITH_GENERATED=OFF
# tudo de uma vez (format, tidy, 4 builds, ctest):   scripts/verify_m3.sh
```

Saída esperada com o gerado ligado: `C gerado: ligado; 155 trecho(s) no dispatcher`, uma linha por módulo e
`M3: esqueleto OK`, código de saída 0. Compilar os 155 arquivos (146 MB de C) leva ~30 s em 10 núcleos.

### CI e `AM_WITH_GENERATED=OFF`

O C gerado é material do jogo e não pode estar no repositório, então o CI nunca o vê. Com `AM_WITH_GENERATED=OFF`:
`am_recomp` vira um alvo `INTERFACE` com `core/recomp_stub.c` (`recomp_is_generated() == false`, zero módulos), o
executável linka e roda dizendo "C gerado: ausente", e os testes usam funções sintéticas. A única dependência de
terceiros que o CI busca é `cpu.h`/`cpu.c` do DolRecomp, por `scripts/ci_fetch_dolrecomp.sh` (lê o pin de
`scripts/fetch_tools.sh`). O demo `tests/known_good_demo` não serve ao CI: é derivado de um DOL e fica fora do git.

## 7. Pendências e riscos

- **CI Linux e Windows não foi executado.** Só a lógica foi ensaiada no macOS (checkout limpo do DolRecomp + SDL3 via
  FetchContent + `AM_WITH_GENERATED=OFF`). Pontos a observar: pacotes `apt` da SDL3, flags `/clang:` do clang-cl, Ninja
  no runner Windows, `abort`/`exit` sem diálogo no Windows, clang-tidy 21.1.6 do PyPI (local: 21.1.8).
- Windows com o C gerado (clang-cl, 146 MB de C) não foi tentado; plano §7 já lista o risco.
- Cada `mem_read*` é uma chamada de função entre bibliotecas. Funcional agora; no M4/M5 medir e considerar um caminho
  rápido inline ou LTO.
- `ppc_rfi`, `ppc_system_call_exception`, `ppc_program_exception` e `ppc_dcbz_l` já estão ligados ao `cpu.c`, mas dependem
  do boot/exceções do M4.
- `cache_control` sem efeito é hipótese: confirmar no M4 que os 4 `icbi` do DOL (`RECOMPILATION_REPORT` §3) não geram
  código.
- Os 62 símbolos `ppc_*` do `am_ppc` incluem funções que o gerado não usa; não há custo além do tamanho da biblioteca.
- `.clang-tidy`: desligado `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` (exige `fprintf_s`,
  Anexo K do C11, inexistente em glibc/macOS/clang-cl). Única mudança nos arquivos de configuração.
