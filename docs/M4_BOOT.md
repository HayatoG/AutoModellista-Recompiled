# M4: até `main`

Estado: **atingido localmente** (macOS arm64 e x86_64 via Rosetta, Debug e Release). A runtime carrega o `main.dol`,
monta o estado que o IPL e o apploader deixariam, roda `__start` → `OSInit` → `main` em C recompilado e para na
primeira instrução de `main` (0x800719AC). O trace de chamadas bate com o do Dolphin 2609a **em todos os eventos
comparáveis** (1998 de 1998 no C gerado padrão; 2733 de 2733 num C regenerado com trechos menores). O limite do que é
"comparável" está em §5.2 e é a principal ressalva deste marco.

Critério de saída do plano (`PLANO_PORT_C.md` §4): "o trace de chamadas até `main` bate com o do Dolphin (§6)".
Resultado: bate, com a normalização descrita em §5.3 (sem ela não há como comparar, por motivo estrutural do gerado).

## 1. Resumo do resultado

```
$ AM_GENERATED_DIR=/Volumes/AMRecomp/recomp scripts/verify_m4.sh --c128
== estado inicial: runtime x Dolphin
OK: estado inicial igual ao do Dolphin a menos das diferenças documentadas
== trace arm64-debug x Dolphin            (idem arm64-release, x86_64-debug, x86_64-release)
  repetição diferente no evento 721: OSGetTick  800C8D78  de __OSInitAudioSystem+0xE8  x3762 | runtime x4388
IGUAIS: 1998 eventos normalizados idênticos; os dois terminam em main: True
== trace com trechos de 128 instruções
IGUAIS: 2733 eventos normalizados idênticos; os dois terminam em main: True
== golden (tests/golden/m4_trace.json)
default: igual ao golden (1998 eventos, sha256 2bdb761ae8cba763…)
c128: igual ao golden (2733 eventos, sha256 a6a30d2134f2b08a…)
```

Execução da runtime até `main` (Release arm64, ~10 ms):

```
[INFO] [OSReport] Dolphin OS
[INFO] [OSReport] Kernel built : Mar 17 2003 04:20:41
[INFO] [OSReport] Console Type : Development HW3 (10000006)
[INFO] [OSReport] Memory 24 MB
[INFO] [OSReport] Arena : 0x802e0de0 - 0x817ffcc0
[INFO] chassi: 41422 voltas, 4 interrupção(ões), 1 sc, 7490106 ciclos estimados, trace 6420 chamadas
[INFO] cache: dcbst 0, dcbf 89, dcbi 4, icbi 85 (0 sobre código intacto, 85 fora do código)
[INFO] parada em 0x800719AC (lr=0x800032AC): M4: main alcançado
```

O texto do `OSReport` sai pela UART do IPL emulado (EXI canal 0, aparelho 1), como no Dolphin.

## 2. O que foi feito

```
runtime/core/dol.c        DOL: cabeçalho, validação, carga na MEM1, .bss zerado sem apagar .sdata/.sdata2
runtime/core/boot.c       estado do IPL/apploader: globais em 0x80000000, bi2, FST, registradores, HID, BATs
runtime/core/timebase.c   TB e DEC derivados dos ciclos estimados do gerado (downcount); ppc_mftb próprio
runtime/core/ppc_sys.c    instruction_fallback: mfspr/mtspr, mfsr/mtsr, dcbst/dcbf/dcbi/icbi; SPRs fora do CPUState
runtime/core/trace.c      trace de chamadas em texto + tabela de trechos; teto de tamanho
runtime/core/chassis.c    laço de execução: interrupções, trace, parada, watchdog, fatias de tempo, exceções síncronas
runtime/os/exception.c    HLE dos vetores 0x500 (externa) e 0xC00 (sc), conferidos contra o modelo do DOL
runtime/hw/{pi,mi,dsp,di,si,exi,ai,vi}.c   o mínimo de MMIO que o caminho até main usa (§4)
runtime/include/am/game.h endereços do GAUE08 (OSInit, main, modelos de vetor)
runtime/main.c            CLI: --sys, --trace, --stop-at, --dump-boot-state, --console, --rtc, watchdogs
scripts/dolphin_gdb.py    cliente do protocolo remoto do GDB (stub do Dolphin)
scripts/dolphin_trace.py  oráculo: estado no entry point e trace do Dolphin até main
scripts/am_trace_lib.py   formato do trace, leitura do DOL, critério de chamada (mesmo da runtime)
scripts/compare_trace.py  comparação até a primeira divergência
scripts/compare_boot_state.py  estado inicial runtime x Dolphin (registradores + 24 MiB)
scripts/check_m4_golden.py     tests/golden/m4_trace.json (só hashes e contagens)
scripts/regen_c128.sh     regenera o C do DOL com trechos de 128 instruções em /Volumes/AMRecomp/recomp/c128
scripts/with_timeout.sh   timeout(1) para macOS; scripts/verify_m4.sh   verificação completa (substitui verify_m3.sh)
tests/unit/               dol, timebase, trace, ppc_sys, boot, exception, hw, chassis + 6 cenários fatais novos
```

Testes: `ctest` roda 26 testes (12 do M3 + 8 executáveis novos + 6 cenários `fatal_*`), todos com dados sintéticos.
Passam também com `AM_WITH_GENERATED=OFF -DAM_SANITIZE=ON` (configuração do CI).

## 3. Estado inicial

### 3.1 Fonte de cada valor

O port não roda IPL nem apploader; `boot.c` reproduz o efeito deles. Referência executável: Dolphin 2609a parado no
entry point pelo stub do GDB (`dolphin_trace.py boot-state`), cuja MEM1 inteira e registradores são comparados com os
da runtime (`--dump-boot-state`).

| Endereço / registrador | Valor | Fonte |
| --- | --- | --- |
| 0x80000000..1F | 32 primeiros bytes do disco (boot.bin) | Dolphin `DVDReadDiscID`; YAGCD 4.2.1.1 |
| 0x80000020 | 0x0D15EA5E ("booted from bootrom") | Dolphin `Boot_BS2Emu.cpp: SetupGCMemory` |
| 0x80000024 | 1 | apploader (stw em 0x81200748), conferido no dump |
| 0x80000028 | 0x01800000 (24 MiB) | `SetupGCMemory` |
| 0x8000002C | 0x10000006 (devkit HW3) | `SetupGCMemory` (`ConsoleType::LatestDevkit`); `--console retail` dá 3 |
| 0x80000030/34 | 0 / endereço do FST (arena alta) | apploader |
| 0x80000038/3C | endereço do FST / tamanho máximo (boot.bin 0x42C) | apploader |
| 0x800000CC | 0 (NTSC; 1 se bi2 0x18 = PAL) | `SetupGCMemory` ("Fake the VI Init of the IPL") |
| 0x800000D0 | 0x01000000 (ARAM) | `SetupGCMemory` |
| 0x800000EC/F0/F4 | 0x80000000 + memória simulada − monitor; memória simulada (bi2 0x04); endereço do bi2 | apploader (stw em 0x81202C58..60) |
| 0x800000F8/FC | 162 MHz / 486 MHz | `SetupGCMemory`; YAGCD 4.2.1 |
| 0x80000300/800/C00 | `rfi` | `SetupGCMemory` (tratadores padrão) |
| 0x800030D8 (u64) | rtc × 40 500 000 | `PresetTimeBaseTicks`; rtc = `--rtc` (padrão 0 = 2000-01-01) |
| FST | `(0x81800000 − FSTmax) & ~31` = 0x817FFCC0 | apploader; conferido byte a byte no dump |
| bi2 | FST − 0x2000 = 0x817FDCC0 | idem |
| r1, r2, r13 | 0x81566550, 0x81465CC0, 0x81465320 (NTSC) | Dolphin `EmulatedBS2_GC` |
| r3, pc | entry point (0x80003154) | `iAppLoaderClose` devolve o entry em r3 (`RunApploader`) |
| MSR | 0x00002032 (FP, IR, DR, RI) | `SetupMSR` |
| HID0/HID1/HID2 | 0x0011C464 / 0x80000000 / 0xE0000000 | `SetupHID` |
| IBAT0, DBAT0, DBAT1 | 0x80001FFF/2, 0x80001FFF/2, 0xC0001FFF/0x2A | `SetupBAT` |
| TB, DEC | rtc × 40,5 MHz; 0xFFFFFFFF | `SystemTimers.cpp` (`SetFakeTBStartValue`, `SetFakeDecStartValue`) |
| .bss | zerado; seções carregadas por cima | o `.bss` do cabeçalho cobre `.sdata` (0x802CD940) e `.sdata2` (0x802CE720) |

### 3.2 Comparação com o Dolphin

`scripts/compare_boot_state.py` sai com OK. Diferenças restantes, todas explicadas:

| Onde | Por quê |
| --- | --- |
| r4–r7, r12, CR, SRR0, SRR1 | sobra do apploader no Dolphin; a runtime zera (`__start` não lê nenhum antes de `__init_registers`) |
| 0x80001804 "STUBHAXX" | HLE do Dolphin (`HLE::PatchFixedFunctions`, HBReload), não existe no hardware |
| 0x800030D4 | o apploader grava (stw em 0x81200C08); nenhum acesso no DOL (procurado por deslocamento 12500); não reproduzido |
| 0x800030D8 | relógio do host no Dolphin, `--rtc` na runtime |
| 0x81200000..0x81300010 | código e dados do próprio apploader e seus argumentos; viram arena |
| 0x815664E0..0x81566550 | pilha do apploader abaixo do r1 inicial |

### 3.3 Registradores no Dolphin

Lidos pelo stub do GDB: pc=0x80003154, msr=0x2032, hid0=0x0011C464, hid1=0x80000000, dec=0xFFFFFFFF, pvr=0x00083214,
tbl/tbu=0 (o Dolphin só atualiza o SPR do TB numa leitura pelo guest).

### 3.4 Código copiado em tempo de execução (vetores)

O `OSExceptionInit` (0x800C29B4) copia o modelo 0x800C2C8C..0x800C2D24 (`__OSEVStart`..`__OSEVEnd`) para cada vetor e
remenda o `li r3,0` em 0x800C2CF4 com o número da exceção; o `__OSInitSystemCall` (0x800C7A64) copia
0x800C7A44..0x800C7A60 para 0x80000C00. Esse código roda com tradução desligada e não existe no C gerado. A runtime
executa a tradução dele em C (`os/exception.c`), **depois de conferir palavra a palavra** que o vetor na MEM1 é a
cópia do modelo (a menos do número); qualquer diferença é `am_fatal`. O modelo do vetor:

```
mtsprg 0,r4 ; lwz r4,0xC0(0) ; stw r3,12(r4) ; mfsprg r3,0 ; stw r3,16(r4) ; stw r5,20(r4)
lhz r3,0x1A2(r4) ; ori r3,r3,2 ; sth r3,0x1A2(r4) ; mfcr/mflr/mfctr/mfxer/mfsrr0/mfsrr1 -> stw em 0x80..0x8C/0x198/0x19C
mr r5,r3 ; nop (__DBVECTOR) ; mfmsr r3 ; ori r3,r3,0x30 ; mtsrr1 r3 ; li r3,N ; lwz r4,0xD4(0)
rlwinm. r5,r5,0,30,30 ; bne -> (lwz r5,0x3000(N*4) ; mtsrr0 r5 ; rfi)  senão -> (srr0 = 0x800C2D28 ; rfi)
```

O de system call: `mfhid0 r9 ; ori r10,r9,8 ; mthid0 r10 ; isync ; sync ; mthid0 r9 ; rfi`. O SDK usa `sc` como
barreira no fim de `DCFlushRange`; acontece uma vez até `main`.

## 4. Hardware, SPRs, cache, tempo e interrupções

Tudo que não está nas tabelas continua `am_fatal` com endereço, pc e lr. Fonte de comportamento: Dolphin 2609a
(`Source/Core/Core/HW/...`, consultado em `/Volumes/AMRecomp/ref/dolphin`, nada copiado) e YAGCD cap. 5.

| Bloco | O que existe | Fonte |
| --- | --- | --- |
| PI 0xCC003000 | INTSR (1 apaga), INTMR, FIFO base/fim/wptr, reset code (só sem pedir reset do drive), FLIPPER_REV_C; causa inicial = botão de reset solto + VI | `ProcessorInterface.cpp` |
| MI 0xCC004000 | 0x00..0x5A como armazenamento de 16 bits, 32 bits em duas metades (proteção não aplicada, como no Dolphin) | `MemoryInterface.cpp` |
| DSP 0xCC005000 | CSR com a divisão interface/emulador (`DSP_CONTROL_MASK`), reset, halt, DSPInit → microcódigo de init (mail 0x80544348), ROM (0x8071FEED), mailbox DSP→CPU com o bit 31 apagando na leitura baixa, DMA de ARAM com 246 ciclos por 32 bytes e interrupção ARAM; mail CPU→DSP é fatal | `DSP.cpp`, `DSPHLE/DSPHLE.cpp`, `MailHandler.cpp`, `UCodes/ROM.cpp`, `INIT.cpp` |
| DI 0xCC006000 | DISR/DICVR (tampa fechada), comandos, DIMAR/DILENGTH com máscaras, DICFG=1; só Inquiry (resposta do Dolphin, fim em 300 µs → TCINT); leitura de disco é fatal | `DVD/DVDInterface.cpp` |
| SI 0xCC006400 | buffer de 128 bytes, COMCSR (TSTART termina na hora), SISR (bits RWC), NOREP; porta 1 = controle padrão (tipo 0x09000000), portas 2–4 vazias; polling e SISR.WR são fatais | `SI/SI.cpp`, `SI_DeviceGCController.cpp`, `SI_DeviceNull.cpp` |
| EXI 0xCC006800 | 3 canais (status, DMA, controle, imediato), interrupções TC/EXT/EXI; aparelho IPL no canal 0/CS2 com SRAM de 0x44 bytes (modelo `sram_dump` do Dolphin + checksum), RTC e UART (texto do OSReport); slots vazios (EXT=0, leitura imediata dá 0) | `EXI/EXI_Channel.cpp`, `EXI_DeviceIPL.cpp`, `HW/Sram.cpp` |
| AI 0xCC006C00 | AICR (máscara, apaga AIINT, SCRESET, taxas), volume, contador, IT; PSTAT (streaming) é fatal | `AudioInterface.cpp` |
| VI 0xCC002000 | só VI_CLOCK (0x6C) = 1 (NTSC 54 MHz), lido por `SISetSamplingRate` | `VideoInterface.cpp: Preset` |

**SPRs** (`ppc_sys.c`): XER/LR/CTR/DSISR/DAR/SRR0/SRR1/EAR/HID2/GQR vão aos campos do `CPUState` (o C gerado e o
`cpu.c` os leem de lá); SPRG0-3, SDR1, MMCR/PMC, IABR, DABR, ICTC, DMAU, L2CR, HID0, HID1 são armazenamento, com os
efeitos do Dolphin (`Interpreter_SystemRegisters.cpp`): HID0.ICFI se apaga, HID1 só bits 0-4, HID2 máscara
0xF0FF0000 com DMAQL somente leitura, WPAR só aceita 0x0C008000, DMAL com T=1 é fatal, PVR somente leitura, DEC que
vira negativo por escrita é fatal. **BATs**: o `__OSInitMemoryProtection` troca IBAT0/DBAT0/IBAT2/DBAT2 (configuração
de 24 MiB); aceitos porque mantêm a tradução 0x80000000|x → x; um BAT válido que traduza diferente é fatal.

**Contrato do fallback (achado do M4).** O DolRecomp emite TODA instrução de sistema deste jogo (310 sítios: mfspr,
mtspr, cache) como `ppc_fallback_instruction(ctx, palavra, cia); return;` sem avançar o pc. Quem trata tem de pôr
`pc = cia + 4`. O laço sem fim do primeiro teste (repetindo `ICFlashInvalidate` a partir de `__OSPSInit`) era isso:
o M3 não avançava o pc, e o chassi executava o mesmo `mfhid0` para sempre.

**Hipótese do M3 sobre `cache_control` (confirmada até main, com ressalva).** O callback `cache_control` nunca é
chamado: as 4 operações de cache passam pelo fallback. Até `main` há 85 `icbi`, todos em 0x80000060..0x800017xx
(vetores copiados por `OSExceptionInit`/`__OSInitSystemCall`), nenhum sobre o texto do DOL; 89 `dcbf`, 4 `dcbi`, 0
`dcbst`. Como o código dos vetores é executado pelo HLE conferido de §3.4, tratar as operações de cache como sem
efeito está correto até aqui. Em vez de confiar na hipótese daqui para frente, a runtime protege as seções de texto
do DOL: um `icbi` sobre um bloco cujo conteúdo na MEM1 difere do DOL é `am_fatal` (código gerado em tempo de execução).
Os outros 3 `icbi` do `RECOMPILATION_REPORT` §3 (TRK, `fn_800F37F4`) não rodam até `main`.

**Tempo** (`timebase.c`). O TB avança 1 tique a cada 12 ciclos de CPU (Dolphin `TIMER_RATIO`), e os ciclos são os que
o C gerado desconta de `downcount`. O chassi zera `downcount` antes de cada chamada e consolida depois; dispositivos
leem o tempo da fatia em andamento. Totalmente determinístico. `ppc_mftb` do `cpu.c` é renomeado no alvo `am_ppc`
(lia `cpu->timebase`, que nada avança).

**Interrupções.** Entregues pelo chassi quando `MSR[EE]` e `(causa & máscara) != 0` no PI, nos pontos em que o C
gerado devolve o controle (bl/bctrl/rfi e estouro do orçamento de ciclos). Até `main` acontecem 4 (SI, uma por
`SIGetType`), logo depois do `OSEnableInterrupts`, como no Dolphin. Decrementer expirado com EE=1 é fatal (M5).

**Modo real.** `RealMode` (0x800C629C) salta com `rfi` e `MSR[IR]=0` para o endereço físico da função que troca os
BATs; o chassi traduz pc físico x < 24 MiB para 0x80000000|x (mesmos bytes pelo mapeamento do BS2). Acesso a dados
em modo real continuaria indo ao barramento e seria fatal; não acontece até `main`.

## 5. Trace e oráculo

### 5.1 Formato e método

Uma linha por evento, texto: `# am-trace v1 k=v...` e depois `<pc> <lr> <ctr>` em hex. A runtime grava quando o
chassi está para despachar um pc e a instrução em LR−4 é um desvio com link para esse pc (`bl/bla/bcl` com alvo
conferido; `bcctrl` conferido pelo CTR; `bclrl` aceito se pc ≠ LR). O trace tem teto (`--trace-max-mb`, padrão
256 MiB, depois `am_fatal`) e vai para `$AM_LOG_DIR` (scripts: `/Volumes/AMRecomp/logs/m4`), nunca para o repositório.

**Oráculo**: Dolphin 2609a (`brew install --cask dolphin`), modo batch, vídeo Null, sem áudio, JIT ARM64 com
`Interface.DebugModeEnabled=True` (o JIT só confere breakpoints assim; `JitArm64/Jit.cpp`), stub do GDB na porta 2345
(`General.GDBPort`; `Core.cpp` deixa a CPU parada no entry point até o cliente conectar). `dolphin_trace.py` põe 3886
breakpoints de execução (todo início de função de `analysis/call_graph.json` + todo alvo de bl/bcl direto do DOL) e
a cada parada lê pc, LR e CTR. Até `main`: 6617 paradas em ~2 s, reproduzível (duas execuções deram o mesmo hash).
Dispositivos iguais aos da runtime: nenhum aparelho EXI (o padrão do Dolphin tem memory card no slot A) e controle
só na porta 1. Escolhi o stub do GDB em vez de breakpoints com log porque dá controle do início (entry point),
leitura de registradores e da memória inteira (usada em §3), sem depender de GUI nem de arquivos de configuração
de breakpoints. Disco boot pelo RVZ original, sem mapa de símbolos (logo sem HLE de funções do Dolphin).

### 5.2 Limite estrutural: chamadas dentro do mesmo trecho

O DolRecomp gera um `func_<início>` por trecho de 4096 instruções (16 KiB), não por função. Um `bl` para dentro do
mesmo trecho vira `goto label_X` com `ctx->lr` ajustado (`emitter.c: emit_direct_branch`), e nunca passa pelo
chassi. A runtime **não vê** essas chamadas, e não há opção do DolRecomp para mudar isso no backend C (o tamanho
mínimo é 128 instruções, `DOLRECOMP_C_CHUNK_INSTRUCTIONS`). Por isso a comparação filtra os dois lados pelo mesmo
critério estático: **só chamadas cujo chamador (LR−4) e chamado estão em trechos diferentes** (tabela gravada pela
runtime em `<trace>.chunks`). Números, de 2797 eventos normalizados do Dolphin:

| C gerado | Trechos | Eventos comparáveis | Resultado |
| --- | --- | --- | --- |
| padrão (`/Volumes/AMRecomp/recomp`, 4096 instr.) | 155 | 1998 (71%) | **iguais** |
| `scripts/regen_c128.sh` (128 instr.) | 2737 | 2733 (98%) | **iguais** |

O build com trechos de 128 instruções existe só para tornar o teste mais forte; o executável normal continua usando o
gerado padrão. Os 64 eventos que nem o c128 vê são chamadas a funções a menos de 512 bytes do chamador.

### 5.3 Normalização

1. só eventos que são chamada (mesmo critério nos dois lados);
2. só alvos com breakpoint no Dolphin;
3. só chamadas entre trechos (§5.2);
4. repetições consecutivas do mesmo (pc, lr) viram um evento. Único caso até `main`: `OSGetTick` no laço de espera de
   54 µs do `__OSInitAudioSystem` (Dolphin 3762 voltas, runtime 4388/4387): o número de voltas depende de quantos
   ciclos cada volta custa no emulador. As contagens aparecem no relatório.

### 5.4 Golden

`tests/golden/m4_trace.json`: contagens e SHA-256 das sequências normalizadas (pc, lr) dos dois lados, por variante.
Nenhum trace bruto, dump ou C gerado no repositório.

## 6. Reproduzir

```sh
# uma vez
brew install --cask dolphin
mkdir -p /Volumes/AMRecomp/work/iso && unzip "/Volumes/SSD/GameCube/Auto Modellista (USA).zip" -d /Volumes/AMRecomp/work/iso
# sys/ extraído (dtk disc extract) em /Volumes/AMRecomp/work/extracted/sys (boot.bin, bi2.bin, fst.bin, main.dol)

export AM_GENERATED_DIR=/Volumes/AMRecomp/recomp AM_LOG_DIR=/Volumes/AMRecomp/logs/m4
scripts/verify_m4.sh --oracle --c128      # format, tidy, 5 builds, ctest, boot, oráculo, comparações, golden

# passos isolados
python3 -I scripts/dolphin_trace.py --iso "/Volumes/AMRecomp/work/iso/Auto Modellista (USA).rvz" \
    trace --dol /Volumes/AMRecomp/work/extracted/sys/main.dol --out $AM_LOG_DIR/dolphin_trace_raw.txt
scripts/with_timeout.sh 120 /Volumes/AMRecomp/build-arm64-debug/runtime/automodellista \
    --sys /Volumes/AMRecomp/work/extracted/sys --trace runtime_trace.txt
python3 -I scripts/compare_trace.py --dol /Volumes/AMRecomp/work/extracted/sys/main.dol \
    --dolphin $AM_LOG_DIR/dolphin_trace_raw.txt --runtime $AM_LOG_DIR/runtime_trace.txt
python3 -I scripts/compare_trace.py ... --all-calls     # sem o filtro de §5.2: mostra o que a runtime não vê
```

Salvaguardas (depois de um trace de 20,7 GB ter enchido o disco interno numa primeira execução com o laço do
fallback): trace com teto; watchdog no chassi (mesmo pc despachado 2 000 000 vezes seguidas ou `--max-seconds`,
padrão 120, sem chegar à parada → `am_fatal` com o pc); toda execução nos scripts passa por `with_timeout.sh`;
`/logs/` no `.gitignore`; saída relativa vai para `$AM_LOG_DIR`.

## 7. Pendências para o M5 e o que não foi verificado

- **Chamadas dentro do trecho** continuam invisíveis ao trace da runtime (§5.2). Se o M5 precisar de trace completo,
  as opções são um patch no emissor do DolRecomp (fora de `tools/`, como fork) ou o trace c128.
- Interrupções só nos pontos em que o gerado devolve ao chassi; laços internos de um trecho só devolvem ao estourar o
  orçamento de 256 ciclos. Bastou até `main`; o VI a 60 Hz (M5) vai exigir conferir a latência.
- Exceções síncronas além de `sc` (DSI, programa, FP indisponível, alinhamento), decrementer, e o tratador padrão
  (MSR[RI]=0) não foram exercitados: são `am_fatal`.
- DI só responde Inquiry; a interrupção do Inquiry (300 µs) não chega antes de `main` em nenhum dos dois lados.
- DSP: mail CPU→DSP (AX) é fatal; EXI: ROM/fontes do IPL são fatais; SI: polling e WR fatais; VI só o clock.
- Console devkit (0x10000006) por paridade com o Dolphin: faz o `OSReport` sair pela UART e o `OSInit` chamar
  `DVDInquiryAsync`. Um console de varejo (`--console retail`) muda o caminho e não foi comparado.
- O controle da porta 1 responde ao `SIGetType` como conectado; no Dolphin isso depende de haver um aparelho de
  entrada padrão. O trace bateu, mas a escolha não foi isolada por teste no Dolphin com o controle desconectado.
- CI Linux/Windows continua sem execução (herdado do M3). O build `AM_WITH_GENERATED=OFF` com ASan+UBSan foi rodado
  localmente no macOS arm64.
- Os RELs não carregam (M5); `scripts/regen_c128.sh` só regenera o DOL e aponta `rels` para o gerado padrão.

## 8. Desvios do plano

- **Critério de saída** cumprido com a normalização de §5.3; sem o filtro de trechos não há comparação possível com o
  C gerado como está (§5.2). Mantive o executável no gerado padrão e uso o c128 só como teste mais forte.
- **HLE dos vetores 0x500 e 0xC00** (§3.4): o plano previa HLE só para troca de contexto, `OSLink`, disco, memory card e
  DSP. Os vetores são cópias feitas em tempo de execução que nunca vão existir no C gerado; a alternativa (mapear
  endereços físicos e recompilar os vetores) não existe sem um interpretador.
- **hw/ depende de core** (`mem`, `log`, `timebase`): DMA e tempo dos dispositivos precisam disso. O diagrama do plano
  põe hw abaixo de core; registro aqui em vez de criar uma camada só para isso.
