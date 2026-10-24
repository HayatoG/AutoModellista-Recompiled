# M8 parte 2b: tempo emulado (os 2 campos que sobravam)

Estado: **atingido**. Defasagem do tempo emulado contra o Dolphin no q6400: **0,000 campo** (era 2,096 = 0,035 s), sem
nenhum degrau em 6400 quadros; a maior defasagem em qualquer quadro é 0,038 campo (o ruído de "último `mftb`" dos
menus, que já existia e não acumula). A primeira interrupção do DVD cai no mesmo tique do Dolphin (836 463).

A causa era um erro de contagem de ciclos no **DolRecomp**: o laço contado do backend C cobrava só o primeiro bloco do
corpo. A correção é um patch no DolRecomp (`patches/dolrecomp/0001-ciclos-do-laco-contado.patch`) e exige regenerar o C.

## 1. Como foi achado

A parte 1 parou com a diferença "dentro de uma chamada indireta da função do quadro, num trecho sem `mftb`": o timebase
que o GDB do Dolphin mostra é o do último `mftb` do jogo, então sondas em funções sem `mftb` por perto não diziam nada.

### Sondas com o tempo exato no Dolphin (`dolphin_trace.py --probes-exact`)

Três tentativas não funcionaram e valem o registro:

1. **`s` do GDB** (um passo): com o JIT, o passo do Dolphin roda uma fatia inteira de tempo, não uma instrução.
2. **Mudar o PC para um trecho com `mftb`** (`P40=`): o GDB do Dolphin só muda `pc`; o `npc` fica, e a CPU volta para a
   instrução original sem passar pelo trecho (r12, que receberia o `mftb`, nunca mudou).
3. **Trecho em 0x80001800** (memória baixa "livre"): o jogo travou; o trecho foi para cima do `OSDumpContext`
   (0x800C43E8, só roda num erro fatal).

O que funciona: a instrução da sonda vira um desvio para um trecho `mftb r0 ; <instrução original> ; b <sonda+4>`, com
breakpoint nas duas primeiras. Na primeira lê-se r0; ao continuar parado num breakpoint o Dolphin executa a instrução
no interpretador (CPU.cpp), cujo `mftb` lê o tempo exato; na segunda lê-se o tempo e r0 volta. Serve para qualquer
instrução que não seja desvio (inclusive endereços de retorno, para medir a duração de uma chamada). Custa 3 instruções
emuladas por passagem. Na runtime a sonda ganhou a 5ª coluna com o timebase exato (e r3 na 3ª).

### O que as sondas mostraram (carga da pista, q5889)

- O quadro longo do q5889 é o fio principal **ocupado** (a thread do jogo, sem esperar nada) de 4 077,36 M a 4 079,31 M
  tiques; ninguém mais rouba CPU (troca de contexto, interrupções e `__OSReschedule` iguais nos dois lados) e as
  interrupções do áudio (AID a cada 5 ms, DSP 1 541 tiques depois) caem no mesmo tique nos dois.
- Com as sondas exatas, o atraso do Dolphin cresce devagar ao longo do trabalho: 0 no início, 156 mil tiques no fim.
  As funções de vértice (`fn_80041D74`, `fn_80041A8C`, `fn_8004211C`), o descompressor (`fn_80035990`, 3,83 M ciclos
  nos dois), o `DCFlushRangeNoSync` (7 ciclos por linha nos dois, 647 mil linhas) e o memcpy de bytes `fn_80046F6C`
  dão o mesmo tempo, tique a tique.
- O **memcpy do SDK (`memmove`, 0x800055CC)**: no Dolphin, 4 ciclos por byte em todas as 109 chamadas do quadro
  (3,72 M ciclos); na runtime, 1,83 M ciclos para os mesmos bytes, **2 por byte**.

### A causa

O laço do memcpy é `lbzu ; stbu ; addic. ; bc` (4 instruções, 4 ciclos), e o código antes dele entra pelo meio com um
`b` para o `addic.`. Esse alvo é um "líder" de bloco, então o corpo vira dois blocos de 2 ciclos. O DolRecomp emite o
laço contado como uma função C própria (`loop_80005608`) e subtraía do downcount só o primeiro bloco:
`emit_counted_loop` usava `block_cycles[first]`. As instruções do segundo bloco saíam de graça em toda volta.

Isso explica o padrão dos degraus: o memmove do jogo copia muito na carga da pista (centenas de KiB por quadro), e o
quadro longo terminava ~0,3–0,5 campo antes na runtime; quando isso atravessava um retrace, virava um campo inteiro.

**Correção** (patch no DolRecomp): o laço cobra a soma dos blocos de todos os líderes do corpo (o corpo é linha reta,
então cada volta executa todos). Mudam só linhas `ctx->downcount -= N`: **130 laços no DOL e 33 nos RELs**.

Com o C corrigido, o tempo antes da primeira leitura do DVD também ficou maior (o memcpy do boot), e a constante do
BS2 (`BOOT_BS2_CYCLES`), medida na parte 1 pela diferença na primeira interrupção do DVD, absorvia esse erro. Remedida
do mesmo jeito: 199 532 tiques (era 200 059). Com ela a primeira interrupção do DVD cai no mesmo tique (836 463), as 540
interrupções do DVD até o q6400 ficam a no máximo 129 tiques das do Dolphin (eram até 2 milhões) e o q6400 continua
em 0,000.

## 2. Hipóteses descartadas (com a medição)

- **Trabalho de outra thread ou mais interrupções no Dolphin**: `OSLoadContext`, `__OSReschedule`,
  `OSSwitchFPUContext` e o decrementer contados nos dois lados; nada a mais que explique 2,7 M ciclos.
- **Áudio (AX/DSP) atrasando o jogo**: AID e DSP no mesmo tique nos dois lados durante todo o quadro.
- **`dcbf` e o `DCFlushRange`** (a causa da parte 1): 7 ciclos por linha nos dois, medido por chamada.
- **GX/DrawDone**: o quadro longo não espera a GPU (nenhum tempo ocioso do fio principal no meio do trabalho).
- **Tabela de ciclos**: conferida de novo contra `PPCTables.cpp` (todas as instruções com custo diferente de 1).

## 3. Resultado (`verify_m8.sh --no-window`, C regenerado com o patch)

| O quê | Antes | Agora |
| --- | --- | --- |
| Defasagem no q6400 | 2,096 campos (0,035 s) | **0,000** |
| Degraus | q5890, q5891, q5899, q5900, q5910, q5911, q5947, q5948 | nenhum |
| Maior defasagem em qualquer quadro | 2,096 | 0,038 |
| 1ª interrupção do DVD (Dolphin 836 463) | 836 475 | 836 463 |
| Interrupções do DVD a ≤ 20 tiques do Dolphin (de 540) | 187 | 259 |
| Câmera da largada, q6400 soft x Dolphin | 29,38 dB | **29,38 dB (imagem idêntica, mesmo SHA-256)** |

A câmera não mudou: o jogo anda por quadro, não por relógio, e o quadro 6400 é o mesmo com ou sem os 2 campos. A
diferença que sobra na largada (29,38 dB, 3,0% de pixels com diferença > 24) não vem do tempo emulado.

Execução completa (`BUILD_ROOT=/Volumes/AMRecomp/wt-builds/tempo`, `AM_GENERATED_DIR=/Volumes/AMRecomp/wt-builds/tempo/gen`):
clang-format ok; ctest 49/49 em arm64 e em x86_64; os dois builds dão o mesmo arquivo de timebase; com `--gx-thread on`
capturas e tempo idênticos aos de `off`; todas as comparações de imagem com os mesmos números de antes (soft x Dolphin
34,96 / 40,64 / 35,68 / 29,38 dB; GPU x soft 36,91 / 44,56 / 39,21 / 30,65; GPU x Dolphin 39,44 / 43,34 / 39,95 /
34,92). O único passo que acusa diferença é o golden, no bloco `timebase` (esperado: o golden não foi regravado).

## 4. O que muda no golden e nos outros roteiros

- `tests/golden/m8_corrida.json`: `timebase.lag_fields` 2,096 → 0,0 e `timebase.max_abs_lag_fields` 2,096 → 0,038.
  As capturas do software (PSNR e SHA-256 dos quatro quadros) não mudam. Não regravei (regra da parte 2b).
- O C gerado compartilhado (`/Volumes/AMRecomp/recomp/dol` e `recomp/m5/rels`) precisa ser regenerado com o patch
  (`scripts/fetch_tools.sh` aplica, `scripts/build_dolrecomp.sh`, `scripts/regen_generated.sh <saída>`); este trabalho
  usou `/Volumes/AMRecomp/wt-builds/tempo/gen`, idêntico ao gerado de antes salvo as 163 linhas de ciclos.
- O tempo emulado muda desde o boot (o memcpy roda em todo lugar), então os goldens que guardam tempo do M5
  (`m5_events.json`, posições das leituras do ADX) e do M7 (atraso do áudio, quadros q3200/q3500) podem mudar. Não
  rodei `verify_m5.sh` nem `verify_m7.sh`: fica para a sessão principal ao juntar as peças.

## 5. Reproduzir

```sh
scripts/fetch_tools.sh && scripts/build_dolrecomp.sh      # aplica patches/dolrecomp e recompila
scripts/regen_generated.sh /Volumes/AMRecomp/recomp/novo  # DOL + RELs
AM_GENERATED_DIR=/Volumes/AMRecomp/recomp/novo scripts/verify_m8.sh --no-window

# sondas exatas (Dolphin) e o mesmo na runtime (5ª coluna)
python3 -I scripts/dolphin_trace.py --iso <rvz> events --pad-script "<corrida>" --frames 5890 \
    --timebase-out d.txt --probes 5889:800055CC,80026D44 --probes-exact --out d_ev.txt
automodellista --sys <sys> --frames 5890 --pad-script "<corrida>" --gfx off --timebase-out r.txt \
    --probes 5889:8001C410,800414C0
```

## 6. Lições

- **Medir os dois lados com a mesma régua.** Enquanto o Dolphin só dava o "último `mftb`", a diferença ficava presa
  num trecho de 2 milhões de tiques. Com um `mftb` injetado na sonda, a comparação virou tique a tique: todas as
  funções iguais, menos uma, com exatamente o dobro.
- **Um número redondo é uma pista.** "Exatamente 2 ciclos por byte contra 4" não é ruído nem modelo de hardware: é
  metade das instruções do laço sem custo.
- **Uma calibração esconde o erro que veio antes dela.** A constante do BS2 da parte 1 tinha engolido o memcpy barato
  do boot; corrigida a causa, a constante precisou ser medida de novo.
- **O tempo e a imagem são independentes aqui.** Zerar a defasagem não mexeu num pixel da largada: o jogo conta
  quadros, não segundos.
