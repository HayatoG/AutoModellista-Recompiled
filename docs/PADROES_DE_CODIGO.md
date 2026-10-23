# Padrões de código da runtime

Valem para tudo em `runtime/` e `tests/`. Não valem para `recomp/out/` (gerado) nem `tools/` (terceiros).
O que pode ser checado por ferramenta é checado por ferramenta: `.clang-format`, `.clang-tidy` e `.editorconfig`
na raiz. O CI falha se qualquer um deles reclamar.

## 1. Linguagem e compilação

- **C17**, sem extensões de compilador fora de `am/compiler.h` (que encapsula `__attribute__`/`__declspec`).
- Compiladores suportados: clang (macOS, Linux) e clang-cl (Windows). GCC é bem-vindo, não é bloqueante.
- Flags da runtime: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Werror`.
  O C gerado compila com warnings desligados, num alvo CMake separado (`am_recomp`).
- Debug no CI: `-fsanitize=address,undefined`.

## 2. Organização

- **Um módulo = um prefixo = um header público.** `mem_*` em `core/mem.c` e `include/am/mem.h`; `gx_fifo_*` em
  `gfx/gx_fifo.c` e `include/am/gx_fifo.h`. Funções internas são `static` e não têm prefixo obrigatório.
- Dependências só para baixo no diagrama do `PLANO_PORT_C.md` §2. `gfx/` não inclui nada de `os/`;
  `platform/` não inclui nada da runtime além de `am/types.h`.
- **Código de SO e de SDL só em `runtime/platform/`.** Fora dela, nenhum `#ifdef _WIN32`, `__APPLE__` etc.
- Estado de um módulo fica numa única `static struct` no `.c` (ex.: `static struct { … } s_vi;`). Nada de
  variáveis globais `extern`, exceto as exigidas pelo DolRecomp (`dolrecomp_call_depth`).
- Arquivos até ~800 linhas; funções até ~60 linhas. Passou disso, dividir. Tabelas de dados são exceção.

## 3. Nomes

| O quê | Estilo | Exemplo |
| --- | --- | --- |
| Funções e variáveis | `snake_case` com prefixo do módulo | `mem_read32`, `vi_schedule_retrace` |
| Tipos | `PascalCase` | `GxVertexFormat`, `OsThreadSlot` |
| Constantes, macros, valores de enum | `UPPER_SNAKE` com prefixo | `VI_REG_DISPLAY_CONFIG`, `GX_PRIM_TRIANGLES` |
| Estado `static` de arquivo | prefixo `s_` | `s_dispatch` |
| Endereço do guest | tipo `GuestAddr` (`uint32_t`), nunca `void*` | `GuestAddr fifo_base` |
| Nomes de registradores de hardware | os do YAGCD/Dolphin, para facilitar busca | `PE_ZCONF`, `CP_FIFO_BASE_LO` |

Identificadores em inglês; comentários e documentação em português (como o resto do repositório).

## 4. Tipos e memória do guest

- Tipos de tamanho fixo de `am/types.h` (`u8`…`u64`, `s8`…`s64`, `f32`, `f64`), os mesmos do DolRecomp.
- **Toda** leitura e escrita da memória do guest passa por `mem_read{8,16,32,64}` / `mem_write*` (que fazem o
  byte-swap). A assinatura é a do C gerado, `mem_read32(CPUState *cpu, GuestAddr addr)`; código do host passa `NULL`. Proibido `*(u32 *)(ram + off)`, inclusive "só para um teste rápido".
- Estruturas do guest nunca são mapeadas em `struct` de C. Use funções de acesso com o offset nomeado:

  ```c
  /* OSThread: offsets do SDK de 09/2002 */
  #define OS_THREAD_STATE 0x2C8
  static u16 os_thread_state(GuestAddr thread) { return mem_read16(thread + OS_THREAD_STATE); }
  ```

- Ponteiros de host para dentro da memória do guest (`mem_host_ptr`) só podem viver dentro de uma função e só para
  cópia em bloco já em big-endian (ex.: upload de textura para o decoder).
- Conversões numéricas sempre explícitas (`(u16)x`), já que `-Wconversion` está ligado.

## 5. Erros

- Erro de programa (invariante quebrada): `AM_ASSERT(cond, "fmt", …)`; ativo também em Release.
- Situação que o jogo não deveria provocar, mas provocou (endereço sem função, registrador MMIO desconhecido):
  `am_fatal("fmt", …)` com endereço, `pc` e `lr`. **Nunca** ignorar silenciosamente nem retornar 0 "por enquanto".
- Erro de ambiente recuperável (arquivo ausente, sem gamepad): função retorna `bool` ou um `enum` de resultado e
  quem chamou decide. Sem `errno` próprio, sem `setjmp`.
- Registrador MMIO ainda não implementado é `am_fatal`, não stub: o objetivo é descobrir o que falta.

## 6. Comentários

- Comentário explica **por que** ou cita a **fonte** (YAGCD, arquivo/função do Dolphin, endereço no jogo).
  Não repete o que o código diz.
- Toda constante de hardware tem fonte: `/* YAGCD 5.3: VI_DCR */`.
- Hipóteses ficam marcadas `/* HIPÓTESE: … */` até serem confirmadas, como em `analysis/known_addresses.md`.
- Sem código comentado, sem `TODO` sem dono: `TODO(M6): …` amarrado a um marco.

## 7. Formatação

`clang-format` decide; não se discute estilo em revisão. Resumo: 4 espaços, 120 colunas, chave na mesma linha,
`*` junto do nome (`u8 *buf`), `if` sempre com chaves.

## 8. Testes

- Todo módulo de `gfx/`, `audio/` e `hw/` tem teste unitário em `tests/unit/<modulo>_test.c`, com dados
  sintéticos. Sem framework externo: `tests/unit/am_test.h` (macros `TEST`, `EXPECT_EQ`).
- Bug corrigido = teste que falhava antes.
- Nenhum byte do jogo em `tests/` (nem ISO, nem DOL, nem textura extraída).

## 9. Git

- Commits pequenos, mensagem em português no imperativo, com o marco: `M5: implementa OSLink para RELs`.
- `recomp/out/`, `logs/`, `tools/` e qualquer arquivo do jogo continuam no `.gitignore`.
- PR só entra com CI verde nas 3 plataformas e revisão (humana ou Opus) do diff.
