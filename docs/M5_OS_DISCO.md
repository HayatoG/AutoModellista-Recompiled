# M5: OS e disco

Estado: **atingido localmente** (macOS arm64 e x86_64 via Rosetta, Release). A runtime passa de `main`, carrega os
RELs pelo `OSLink` do SDK (menus, textos e, ao começar uma corrida, o `game.rel`), lê o disco com o tempo do drive do
Dolphin e roda o laço de quadros `fn_80051034`. Com um roteiro de controle que atravessa os menus até a largada, o
`game.rel` é ligado no quadro 5731 e a execução segue 669 quadros depois disso sem erro. Os 12 eventos de módulo
(OSLink/OSUnlink) caem nos mesmos quadros que no Dolphin 2609a, e as 91 leituras de disco fora de fluxo são idênticas
e na mesma ordem. As leituras de música em fluxo (ADX) batem em número por trecho, não em tamanho (§5.3).

Critério de saída do plano (`PLANO_PORT_C.md` §4): "o jogo carrega `game.rel` e `menu.rel` e o laço de frame
(fn_80051034) roda 600 frames sem erro". Resultado: cumprido, com a comparação de eventos de §5 contra o Dolphin.

## 1. Resumo do resultado

```
$ scripts/verify_m5.sh --oracle
== oráculo: Dolphin com o roteiro (6400 quadros)
551 eventos, 6400 quadros em 165.4s; fim: frames
== arm64 Release (/Volumes/AMRecomp/build-arm64-release)          (idem x86_64 Release, Rosetta)
100% tests passed out of 39
parada em 0x80051034: M5: 6400 quadros completos do laço fn_80051034 em 57 s   (x86_64 via Rosetta: 81 s)
Dolphin : 551 eventos, 6400 quadros; OSLink do módulo 1 no quadro 5731, 669 quadros depois -> OK
runtime : 551 eventos, 6400 quadros; OSLink do módulo 1 no quadro 5731, 669 quadros depois -> OK
ESTRITO IGUAL: 103 eventos (módulos + leituras fora de fluxo) idênticos e na mesma ordem
FLUXO IGUAL: 441 leituras de 10 arquivo(s) ADX, mesma contagem por trecho; 231 delas idênticas (...)
eventos de módulo: 12; quadros iguais nos dois lados: True
OK
== golden (tests/golden/m5_events.json)
igual ao golden (2 build(s)): 103 eventos estritos, 441 leituras de ADX; sha256 5ebc86e48180950…
```

`AM_GENERATED_DIR=/Volumes/AMRecomp/recomp/m5 scripts/verify_m4.sh` continua passando com os mesmos builds (5 builds,
39/39 testes em cada, clang-tidy limpo, trace até `main` com 1998 eventos iguais e o golden do M4 inalterado).

Fim da execução da runtime (Release arm64, 6400 quadros):

```
[INFO]   OSLink menu    id  2 real 0x816D27E0 virtual 0x81870000
[INFO]   OSLink menu00  id  3 real 0x81736EE0 virtual 0x81900000
[INFO]   OSLink msg_e   id  6 real 0x8176A7E0 virtual 0x81960000
[INFO]   OSUnlink menu00  id  3 ...   OSLink menu01  id  4 real 0x81736EE0 virtual 0x81930000
[INFO]   OSUnlink menu01  id  4 ...   OSLink menu00  id  3 real 0x81736EE0 virtual 0x81900000
[INFO]   OSUnlink menu    id  2 ...   OSUnlink menu00  id  3
[INFO]   OSLink game    id  1 real 0x816D27E0 virtual 0x81800000
[INFO]   OSLink menu00  id  3 real 0x81736EE0 virtual 0x81900000
[INFO] DVD: 540 comandos, 539 leituras (27208832 bytes, 0 de enchimento), 0 AudioBufferConfig
[INFO] VI: 6604 campos, XFB 0x005067A0; SI: 13205 leituras do controle; AI: streaming ligado 0 vez(es)
[INFO] DSP: microcódigo 0x07F88145 (7968 bytes de 0x80180E40), 22046 listas do AX, 22046 interrupções de DMA de áudio
[INFO] GX: 455644533 bytes no gather pipe, 36192397 comandos, 4704330 desenhos (21722696 vértices), ... descartado
[INFO] exceções: externa 91325, decrementer 7566, FP indisponível 9098
[INFO] parada em 0x80051034: M5: 6400 quadros completos do laço fn_80051034
```

O `menu.rel` e o `game.rel` usam o mesmo endereço real (0x816D27E0), um depois do outro; `menu00`/`menu01` revezam
0x81736EE0. Os riscos do plano ficam respondidos: o microcódigo do DSP é o AX padrão (hash Ector 0x07F88145) e o jogo
não liga o streaming de áudio do disco (DTK: nenhum `AudioBufferConfig`, AI sem streaming); a música vem de ADX
lido do disco e tocado pelo AX.

## 2. O que foi feito

```
scripts/regen_rels_m5.sh   C dos 12 RELs com bases virtuais a partir de 0x81800000 (fora dos 24 MiB) em recomp/m5
runtime/core/disc.c        imagem do disco a partir da árvore extraída (sys/ + files/ pela FST)
runtime/core/rel_map.c     janela virtual dos RELs (físico 0x018xxxxx) -> endereço real; real -> virtual para o pc
runtime/core/hook.c        ganchos por endereço no chassi (OSLink, OSUnlink, quadro)
runtime/os/os_link.c       HLE de OSLink/OSLinkFixed/OSUnlink: confere o módulo contra o .rel do disco, liga a janela
                           e chama o Link do SDK com a base virtual do C gerado
runtime/os/os_threads.c    leitura das OSThread do jogo para o detector de laço parado (só leitura)
runtime/os/exception.c     + decrementer (0x900) e FP indisponível (0x800) pelo vetor copiado
runtime/hw/{vi,si,ai,di,dsp,cp,pe,gather_pipe}.c   VI com meias-linhas e interrupções; SI com polling e controle;
                           AI; DI lendo o disco; DSP com DMA de áudio e ARAM; CP/PE/gather pipe gravando a FIFO
runtime/hw/dvd_timing.c    tempo do drive como o do Dolphin (§3)
runtime/audio/dsp_hle.c    protocolo dos microcódigos (ROM, INIT, AX por hash) e lista de comandos do AX
runtime/audio/ax_voice.c   vozes do AX andando sem mixagem: endereço, laço, fim de amostra, conversor de taxa
runtime/gfx/gx_fifo.c      leitor mínimo do fluxo do GX: só PE_DONE e tokens (desenho é M6)
runtime/core/pad_script.c  roteiro de botões por quadro (--pad-script), o mesmo formato do oráculo
runtime/game/game_run.c    disco, ganchos, contagem de quadros, log de eventos (--events), detector de laço parado
scripts/dolphin_trace.py   + events (main, OSLink/OSUnlink, leituras, quadros), --pad-script, --shots, --rtc
scripts/compare_events.py  comparação de eventos em dois níveis (§5)
scripts/check_m5_golden.py tests/golden/m5_events.json (só roteiro, contagens e hashes)
scripts/verify_m5.sh       verificação completa do marco
```

Testes: `ctest` roda 39 testes (entre eles vi, gx_fifo, dsp_hle, ax_voice, pad_script, rel_map, disc, sched e
dvd_timing, novos no M5), todos com dados sintéticos; passam nos builds Release arm64 e x86_64.

## 3. Tempo do disco

O jogo tem várias threads lendo o disco ao mesmo tempo (música em fluxo, cenário, menus). A ordem das leituras e o
tamanho de cada pedido dependem de quando cada leitura termina. O primeiro modelo da runtime (300 µs + 3 MB/s fixos)
reproduzia a ordem até o menu principal e divergia logo depois: no evento 71 (quadro 2092 no Dolphin, 2053 na
runtime) uma leitura de `afs01_gu.afs` passava na frente de uma de música (ADX), e daí em diante os quadros e a ordem
se afastavam (14 leituras a mais na runtime no menu).

`hw/dvd_timing.c` refaz o `ScheduleReads` do `DVDInterface.cpp` e o `DVDMath.cpp` do Dolphin 2609a, inclusive as
conversões entre inteiro e double (o tempo depois da busca vai em segundos inteiros, como lá): 600 µs de comando;
busca curta (< 1 mm: 35 ms + 50 s/m) ou longa (75 ms + 4,5 s/m) entre os raios da espiral; latência de rotação a
28,5 voltas/s; leitura de bloco ECC (32 KiB) a 2,1–3,325 MiB/s conforme o raio (drive CAV); e o buffer de 1 MiB que o
drive continua enchendo depois de cada leitura, servido a 32 MiB/s. Os comandos sem leitura continuam com 300 µs.
Depois disso as leituras ficaram na mesma ordem e no mesmo quadro até o quadro 2121, e todos os eventos de módulo
caem no mesmo quadro que no Dolphin.

## 4. Caminho até a corrida e oráculo

O `game.rel` só é ligado quando uma corrida começa (REA/Ghidra no `main.elf`: `fn_8007EEA0`/`fn_8007EFB4` trocam o
módulo do espaço 1, onde entra o `game.rel`; `fn_80071658` chama o `OSLink`). Sem controle o jogo para em "No Memory
Card... start without saving?". O caminho foi achado no Dolphin com capturas da tela (`--shots`, o XFB lido da RAM
pelo stub do GDB) e virou um roteiro por quadro, o mesmo nos dois lados:

| Quadro | Botão | Tela |
| --- | --- | --- |
| 320, 360 | D_LEFT, A | "No Memory Card": escolhe "Yes" |
| 1700, 2000 | START, START | pula logotipos e filme; "PRESS START" |
| 2600, 2900 | A, A | menu principal: arcade, single race |
| 3300 … 5700, a cada 300 | A | pista (Suzuka), música, carro, estilo, ajuste, regras, "go race" |
| 5731 | — | "now loading": `OSUnlink` de menu e menu00, `OSLink` do game.rel e do menu00 |

Cada passo segura o botão 8 quadros. Como os quadros dos eventos de módulo caem iguais nos dois lados (§3), não foi
preciso margem nem repetição: o roteiro por quadro fixo funciona igual no Dolphin e na runtime. Na largada a captura
do Dolphin mostra o Viper em 8º, volta 1/3, 9 s de corrida no quadro 7600.

**Relógio:** o Dolphin usa o relógio do host por padrão; a runtime começa em 2000-01-01 (`--rtc 0`). Com o relógio do
host, as 20 leituras grandes da tela "now loading" eram de outras entradas do `.afs` que as da runtime (HIPÓTESE: o
jogo sorteia algo da corrida, como os adversários, a partir do tempo). `dolphin_trace.py` agora liga
`EnableCustomRTC` com o mesmo instante (`--rtc`, padrão 0) e as leituras passaram a ser as mesmas, na mesma ordem.
Com isso o Dolphin é reproduzível: duas execuções de 6400 quadros deram o mesmo log, evento a evento.

`dolphin_trace.py events` põe breakpoints só em `main`, `OSLink`/`OSLinkFixed`/`OSUnlink`, na função interna `Read`
do DVD (0x800CDC70, que programa o comando 0xA8) e na entrada do quadro (fn_80051034 chamada de 0x8004839C), e grava
uma linha por evento no mesmo formato que a runtime grava com `--events`. 6400 quadros levam ~165 s no Dolphin e ~58 s
na runtime (Release arm64).

## 5. Comparação de eventos

### 5.1 Formato

Texto, uma linha por evento: `main`; `oslink|osunlink frame=N id=<módulo> real=<endereço>`;
`di_read frame=N offset=<disco> length=<bytes> dst=<físico>`. O Dolphin escreve o número de quadros no cabeçalho, a
runtime num rodapé (`# fim frames=N end=frames`); sem rodapé a execução não terminou (um `am_fatal` não chega lá).

### 5.2 Dois níveis

`compare_events.py` ignora o número do quadro na igualdade (relata à parte) e compara:

1. **Estrito, em ordem:** `main`, `OSLink`/`OSUnlink` e toda leitura que não é de áudio ADX, com todos os campos.
2. **Fluxo:** para cada trecho entre dois eventos de módulo, o mesmo número de leituras de cada arquivo ADX nos dois
   lados. Bytes lidos e leituras idênticas são relatados.

Arquivo ADX = entrada de um `.afs` (tabela do próprio `.afs`) cujo cabeçalho começa com 0x80 0x00 e traz "(c)CRI"
antes dos dados; o script lê só o cabeçalho para classificar. Critério: (1) igual, (2) mesma contagem, e os dois
lados com o `OSLink` do `game.rel` seguido de 600 quadros ou mais.

### 5.3 Por que a música não bate byte a byte

A CRI toca música lendo pedaços do ADX para um buffer circular. Pelo que os logs mostram, cada pedido repõe o que já
foi tocado, em setores de 2 KiB (HIPÓTESE: medido pela posição da voz do AX no instante em que a thread de leitura
olha). Isso depende da fração de quadro em que a CPU chega ali, e a runtime não tem igualdade de ciclo de CPU com o
JIT do Dolphin (os ciclos do C gerado são estimados por bloco). O conversor de taxa e o tempo da DMA de áudio já
seguem o Dolphin (`ax_voice.c`, `dsp.c`). No menu principal as três trilhas (afs00 #17, #18, #19) fazem 417 leituras
nos dois lados; a primeira diferença é no quadro 2121 (0xB800 contra 0xA800 bytes) e o total difere em 0x2800 bytes
de 7,1 MB. Na seleção de pista, a música da pista (#8) faz 8 leituras nos dois lados. 231 das 441 leituras de ADX são
idênticas.

### 5.4 Golden

`tests/golden/m5_events.json`: o roteiro de controle, o número de quadros e o resumo de `compare_events.py` (contagens,
quadros do `OSLink` do `game.rel`, SHA-256 da sequência estrita). Os dois builds (arm64, x86_64) têm de dar o mesmo
resumo. Nenhum log de eventos, dump ou C gerado entra no repositório.

## 6. Reproduzir

```sh
# uma vez: ISO e árvore extraída como no M4 (docs/M4_BOOT.md §6); C dos RELs fora da MEM1
scripts/regen_rels_m5.sh                     # -> /Volumes/AMRecomp/recomp/m5 (dol/ é link para o gerado padrão)

scripts/verify_m5.sh --oracle                # format, builds Release arm64/x86_64, ctest, jogo, Dolphin, golden
AM_GENERATED_DIR=/Volumes/AMRecomp/recomp/m5 scripts/verify_m4.sh   # o M4 continua passando com os mesmos builds

# passos isolados
scripts/m5_run.sh --frames 6400 --pad-script "<roteiro>" --events eventos.txt   # build de desenvolvimento
python3 -I scripts/dolphin_trace.py --iso "/Volumes/AMRecomp/work/iso/Auto Modellista (USA).rvz" --video Metal \
    events --pad-script "<roteiro>" --frames 6400 --shots 5700,6400 --out /Volumes/AMRecomp/logs/m5/d.txt
python3 -I scripts/compare_events.py --dolphin d.txt --runtime eventos.txt
```

O roteiro completo está em `scripts/verify_m5.sh` e no golden. Salvaguardas: toda execução por `with_timeout.sh`; o
Dolphin roda no grupo de processos do script e é morto em qualquer saída; detector de laço parado na runtime (1200
campos de VI sem quadro → `am_fatal` com as threads do SO, a causa do PI e o VI); `ls /Volumes/AMRecomp` antes de
execuções longas (o volume do SSD desmontou três vezes durante o marco).

## 7. Bugs que valem registro

- **Janela de REL descarregado:** `menu.rel` e `game.rel` (e `menu00`/`menu01`) ocupam o mesmo endereço real um depois
  do outro. A tradução real → virtual achava o primeiro módulo que cobria o endereço, inclusive um já descarregado: o
  `OSUnlink` do `menu01` recebia a base virtual do `menu00` (o log mostrava "OSUnlink menu01 … virtual 0x81900000") e
  um salto para o `game.rel` pelo endereço real cairia no código do menu. O jogo seguia mesmo assim (não conferi o
  que o `Unlink` do SDK fez com a base errada). Corrigido com `rel_map_retire` no `OSUnlink` + teste de regressão
  (`c2d96c8`).
- **Relógio do oráculo** (§4) e **tempo do disco** (§3): duas fontes de diferença que não eram bug da runtime, mas da
  comparação.

## 8. Pendências e o que não foi verificado

- **Igualdade de ciclo de CPU:** os ciclos do C gerado são estimados; basta para quadros e eventos, não para o tamanho
  dos pedidos de música (§5.3). Vai importar de novo se algum caminho do jogo depender de corrida entre threads.
- **Builds Debug** do M5 não rodaram os 6400 quadros (só ctest e o boot até `main` pelo `verify_m4.sh`); o critério
  foi verificado em Release arm64 e x86_64.
- **Mixagem do AX, desenho do GX e memory card** continuam fora (M6/M7): o GX só responde PE_DONE e tokens, o áudio
  não sai, e o jogo roda sem memory card (escolha "Yes" no aviso).
- **A corrida sem acelerador:** o roteiro não pilota; 669 quadros depois do `OSLink` são a contagem regressiva e o
  começo da prova parado. Pilotar entra quando houver vídeo (M6).
- CI Linux/Windows continua sem execução (herdado do M3).

## 9. Desvios do plano

- **Comparação em dois níveis** (§5.2) em vez de igualdade total das leituras: a única diferença restante é o tamanho
  dos pedidos de música em fluxo, que depende de igualdade de ciclo de CPU.
- **Tempo do drive copiado do Dolphin** em vez de um modelo próprio: o oráculo é o Dolphin, e a ordem das leituras
  só bate com o mesmo modelo.
- **Roteiro de controle** dentro da runtime (`--pad-script`): o plano previa entrada real só no M8; para chegar ao
  `game.rel` sem intervenção humana foi preciso apertar botões por quadro, nos dois lados.
