# Plano: port nativo em C (M3–M10)

Escrito em 2026-10-23. Substitui a ModernGekko como runtime alvo; o que já foi feito em M0–M2 continua valendo
(`ENVIRONMENT.md`, `ISO_ANALYSIS.md`, `RECOMPILATION_REPORT.md`). Padrões de código: `PADROES_DE_CODIGO.md`.

## 1. Decisão

Runtime própria em **C17**, sem C++, sobre o C gerado pelo DolRecomp. Uma única dependência de plataforma:
**SDL3** (janela, entrada, áudio, threads, timers e a GPU API sobre Vulkan, Metal e D3D12). Com isso o mesmo
código compila para macOS arm64, Linux x86_64/arm64 e Windows x86_64 sem `#ifdef` de plataforma fora de
`runtime/platform/`.

Por que não a ModernGekko (`RUNTIME_EVALUATION.md`):

| Critério | ModernGekko | Runtime própria em C |
| --- | --- | --- |
| Linguagem | C++ sobre o Dolphin inteiro | C17 |
| Build | vários GB, sem MSVC | um alvo CMake, minutos |
| Código não coberto | cai no JIT do Dolphin (híbrido) | não existe fallback: tudo é recompilado ou é bug |
| RELs (~43% do código) | não suportados no módulo nativo | suportados desde o início (M5) |
| Custo | pronto para o primeiro boot | precisamos escrever GX, DSP/AX, OS, DVD |

O custo é real: a parte de vídeo sozinha é o maior bloco do projeto. Em troca, o resultado é um port nativo
de verdade, portável e legível.

Licença: o C gerado inclui `cpu/cpu.h` do DolRecomp (GPL-3.0); a runtime será **GPL-3.0**. Consultar o
código do Dolphin (GPL-2.0+) como referência é compatível; nunca copiar sem atribuição no cabeçalho do arquivo.

## 2. Arquitetura

```
              ┌──────────────────────────────────────────────────────┐
              │ recomp/out/  (gerado, nunca editado à mão)           │
              │   DOL + 12 RELs → funções C  fn_XXXXXXXX(CPUState*)  │
              └──────────┬───────────────────────────────▲───────────┘
   dolrecomp_call /      │ external_read/write,          │ chamadas de
   dispatch_replacement  │ host_call, spr, cache         │ volta (callbacks)
              ┌──────────▼───────────────────────────────┴───────────┐
              │ runtime/core   memória, CPUState, dispatcher, RELs   │
              │ runtime/os     threads, alarmes, interrupções (HLE)  │
              │ runtime/hw     barramento MMIO: PI VI CP PE DSP DI   │
              │                SI EXI AI + WGPipe 0xCC008000         │
              │ runtime/gfx    FIFO → XF/BP → vertex loader →        │
              │                texturas → TEV ubershader → EFB/XFB   │
              │ runtime/audio  AX HLE (mailbox do DSP) + mixer       │
              │ runtime/game   patches do Auto Modellista            │
              └──────────┬───────────────────────────────────────────┘
              ┌──────────▼───────────────────────────────────────────┐
              │ runtime/platform  SDL3: janela, GPU, áudio, input,   │
              │                   arquivos, config  (único lugar     │
              │                   com código de SO)                  │
              └──────────────────────────────────────────────────────┘
```

Princípios:

1. **O SDK do jogo é código do jogo.** OS, GX, DVD, AX, CARD e as bibliotecas CRI estão dentro do DOL e já foram
   recompilados. Emulamos o *hardware* por trás deles (registradores MMIO), não as funções. Isso cobre GX inline
   (macros `GXPosition3f32` escrevendo direto no WGPipe), que uma HLE por função perderia.
2. **HLE por função só onde o hardware é caro ou não faz sentido no PC**, via `dolrecomp_dispatch_replacement`:
   troca de contexto de thread, `OSLink`, leitura de disco, memory card e microcódigo do DSP.
3. **Memória do guest continua big-endian.** Toda leitura e escrita passa por `mem_read*/mem_write*`; nenhum
   ponteiro de host é guardado em struct do guest. Isso elimina o problema de ponteiros de 32 vs 64 bits.
4. **Nada de JIT nem interpretador.** Código não encontrado no dispatcher é erro fatal com endereço e pilha de
   chamadas, não um caminho lento silencioso.

### 2.1 Decisões por subsistema

| Subsistema | Decisão | Motivo |
| --- | --- | --- |
| Memória | MEM1 24 MiB + ARAM 16 MiB em arrays de host; espelhos 0x80/0xC0 resolvidos no acesso | layout original preservado |
| Dispatcher | tabela ordenada endereço → função, busca binária; cache de 1 entrada por call site | sem dependência de hash externo |
| RELs | HLE de `OSLink`/`OSUnlink` registra o endereço real de cada módulo; o dispatcher traduz `pc - base_real + base_virtual` (bases de `RECOMPILATION_REPORT.md` §4) | a relocação de dados continua sendo feita pelo próprio jogo |
| Threads | cada `OSThread` vira uma thread de host; só uma roda por vez (bastão com mutex + condvar) | portável, sem `ucontext`/fibers por SO |
| Interrupções | entregues em pontos seguros (retorno ao dispatcher e `VIWaitForRetrace`); VI a 59,94 Hz por timer do host | determinismo suficiente sem emular ciclos |
| Disco | lê os arquivos extraídos pelo `dtk disc extract`; `DVDRead*` respondem do sistema de arquivos | evita implementar RVZ; ISO nunca entra no repo |
| Memory card | um arquivo `.gci` por save, em `platform_user_dir()` | compatível com Dolphin |
| Vídeo | decodificador de FIFO (CP) + estado XF/BP próprio; **um ubershader TEV** em HLSL, compilado offline para SPIR-V/MSL/DXIL com SDL_shadercross | nenhuma compilação de shader em tempo de execução, sem travadas |
| Texturas | decoders de I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, CI4/8/14 e CMPR para RGBA8; cache por endereço + hash do conteúdo | formatos documentados, testáveis isoladamente |
| EFB/XFB | EFB = render target 640×528 (escalável); cópias EFB → textura na GPU; XFB apresentado no `VISetNextFrameBuffer` | o cel-shading do jogo depende de cópias de EFB |
| Áudio | HLE do microcódigo AX na interface de mailbox do DSP; mixer próprio em `float`, saída por `SDL_AudioStream` | ADX e Sofdec (CRI) continuam como código do jogo |
| Entrada | SI/PAD alimentado por `SDL_Gamepad`; mapeamento em arquivo de config | |
| Rede (BBA) | não suportada; EXI do adaptador responde "ausente" | hipótese de LAN ainda não confirmada |

## 3. Estrutura de pastas

```
runtime/
  CMakeLists.txt
  include/am/          headers públicos, um por módulo (am/mem.h, am/gx_fifo.h…)
  core/  os/  hw/  gfx/  audio/  game/  platform/
  shaders/             HLSL do ubershader + SPIR-V/MSL/DXIL gerados (versionados)
  third_party/         SDL3 via FetchContent, versão fixa
tests/
  unit/                testes por módulo, dados sintéticos (nunca bytes do jogo)
  golden/              comparação com Dolphin (ver §6), só hashes e metadados
```

## 4. Marcos

Cada marco tem critério de saída verificável. Nenhum marco começa com o anterior "quase pronto".

| Marco | Entrega | Critério de saída |
| --- | --- | --- |
| **M3 Esqueleto** | CMake, `runtime/core` com stubs, link de todo o C gerado (DOL + RELs) num executável; CI em macOS arm64, Linux x86_64 e Windows x86_64 (clang-cl) | o executável linka nas 3 plataformas sem símbolo pendente; `clang-format`/`clang-tidy` limpos |
| **M4 Até `main`** | memória, dispatcher, SPRs, cache ops, `__start` → `OSInit` (0x800C25DC) → `main` (0x800719AC); log de trace | o trace de chamadas até `main` bate com o do Dolphin (§6) |
| **M5 OS e disco** | threads, alarmes, interrupções, `OSLink` dos RELs, DI/DVD, ARAM DMA, VI básico | o jogo carrega `game.rel` e `menu.rel` e o laço de frame (fn_80051034) roda 600 frames sem erro |
| **M6 Primeiro frame** | FIFO, XF/BP, vertex loader, decoders de textura, ubershader com TEV simples, XFB na tela | logo da Capcom visível; screenshot comparado ao Dolphin (§6) |
| **M7 Menus** | TEV completo, cópias de EFB, PAD, AX HLE + mixer, Sofdec (FMV) | navegar todos os menus com controle; áudio e vídeos de abertura tocando |
| **M8 Corrida** | cel-shading correto, z-texture/fog, CARD (saves), desempenho | uma corrida completa a 60 fps estáveis no M4; save gravado e lido |
| **M9 Portabilidade** | builds Release nas 3 plataformas, pacote com instruções de "traga sua ISO" | corrida completa em Windows x86_64 e Linux x86_64 além do macOS |
| **M10 Melhorias** | widescreen, resolução interna, 60 fps onde o jogo roda a 30, troca gradual de funções recompiladas por decompiladas | cada melhoria atrás de opção, desligada = comportamento original |

Ordem dentro de cada marco: primeiro o teste (ou o trace de referência), depois o código.

## 5. Integração com o decomp

O decomp (decomp.wtf / dtk-template em `decomp/`) não é pré-requisito do port. Ele entra assim:

1. **Nomes**: símbolos casados alimentam o MAP do DolRecomp (`--map`), deixando o C gerado e os logs legíveis.
2. **Substituição**: uma função decompilada e casada pode substituir a recompilada via `patches/`; o
   comportamento é idêntico por construção, mas o código passa a ser legível e modificável.
3. **M10**: melhorias (widescreen, 60 fps) são escritas sobre funções já decompiladas, nunca sobre C gerado.

## 6. Verificação

- **Dolphin como oráculo.** Script que roda o jogo no Dolphin com logging de chamadas em pontos fixos (entrada de
  função por endereço, `VISetNextFrameBuffer`) e produz um trace; a runtime gera o mesmo formato. O CI compara
  os dois até o primeiro ponto de divergência. Traces e screenshots ficam em `logs/` (fora do git); só os hashes
  vão para `tests/golden/`.
- **Testes unitários** por módulo com dados sintéticos: decoder de textura (blocos montados à mão), parser de
  FIFO, conversões de ponto flutuante, mixer.
- **Sanitizers**: builds Debug com ASan + UBSan no CI de Linux e macOS.
- **Nada de bytes do jogo no repositório**, nem em testes.

## 7. Riscos

| Risco | Impacto | Mitigação |
| --- | --- | --- |
| TEV/EFB do cel-shading mais complexo que o previsto | atrasa M7/M8 | começar o estudo das cópias de EFB do jogo já em M6, usando o FIFO log do Dolphin |
| Threads de host + bastão com deadlock em casos de borda (`OSSuspendThread` em outra thread) | travamentos intermitentes | testes de estresse do escalonador isolados do jogo; log de toda troca de contexto |
| Recursão profunda estoura a pilha do host | crash | `DOLRECOMP_C_MAX_CALL_DEPTH` já devolve ao dispatcher; pilha de 8 MiB nas threads guest |
| C gerado de 84 MB lento para compilar no MSVC | Windows inviável | usar clang-cl no Windows; `cl.exe` não é alvo |
| Microcódigo de DSP diferente do AX padrão | áudio mudo | identificar o hash do ucode no M5 (antes de escrever o mixer) |
| Streaming de áudio do disco (byte 0x08) em uso | música ausente | confirmar no M5 via log de comandos DI |
| DolRecomp (projeto de terceiro) muda a interface | quebra a build | commit fixo em `scripts/fetch_tools.sh`; atualizar só de propósito |

## 8. Divisão de trabalho com modelos

| Tipo de tarefa | Modelo | Por quê |
| --- | --- | --- |
| Arquitetura, interfaces entre módulos, revisão de PR, depuração de divergência com o Dolphin | Opus | raciocínio longo, pouca especificação prévia |
| Implementação de módulos com especificação clara (decoders, FIFO, MMIO, mixer) | Sonnet | bom custo para código grande com testes |
| Tarefas mecânicas com verificador automático: matching de funções no decomp (objdiff dá o veredito byte a byte), testes unitários a partir de tabela, renomeações | Haiku | barato e rápido; o verificador pega os erros |
