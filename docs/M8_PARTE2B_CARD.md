# M8 parte 2b: memory card (CARD) gravado e lido

Peça "card" da parte 2b (branch `m8-2b-card`). Critério: o jogo grava no memory card do slot A, a runtime fecha, abre de
novo e o jogo lê o save. **Atingido**, e o cartão que o jogo grava na runtime é **byte a byte igual** ao que o Dolphin
2609a grava com o mesmo roteiro (§5).

## 1. O que o jogo faz com o cartão

O SDK inteiro (OS, CARD, EXI) é código do jogo, recompilado: a runtime só precisa responder pelos registradores de EXI
como um cartão de verdade. O que existia: o canal 0 só tinha o IPL (aparelho 1: SRAM, RTC, UART), o slot A estava
vazio e o jogo parava em "No Memory Card... start without saving?" (M5 §4, o roteiro escolhia "Yes").

Com um cartão no slot A o jogo, sem nenhuma ajuda, faz isto (capturas conferidas, `--gfx soft`):

| Quadro | Tela |
| --- | --- |
| 320 | "1 free file and 19 free blocks are required to save a System File. Will you create a System File on the Memory Card in Slot A?" (o cartão de 59 blocos basta) |
| a partir do 400 | "Creating System File on the Memory Card in Slot A..." 12,5 % ... 96,2 %, depois "A System File has been created" |
| 2600 a 2800 | menu principal: garage life, arcade, VJ & theater, options, **save/load**, special movie |
| save | save/load > save > Memory Card Slot A > "A System File already exists... overwrite the previous data?" > "Saving the file to the Memory Card in Slot A..." > "The file has been saved" |
| load | save/load > load > Slot A > "OK to load a file from the Memory Card in Slot A?" > "The file on the Memory Card in Slot A has been loaded" |

O "save" do jogo é um único arquivo, o System File (19 blocos, `auto_modellista_System.SYS`, código `GAUE` `08`):
opções, recordes e carros. Salvar de novo reescreve o arquivo noutros blocos (o SDK faz o rodízio das duas cópias do
diretório e da BAT), por isso o diretório e a BAT acabam com contadores 8/9 e 2/3 depois de criar e salvar.

## 2. Como funciona

Três peças, todas em `runtime/hw/`, nos moldes do Dolphin 2609a:

- `memcard.c` (`am/memcard.h`): a imagem do cartão na memória (um `.raw` do Dolphin: nada de cabeçalho extra, os bytes
  do cartão) com leitura, escrita, apagar bloco, apagar tudo e a marca "sujo". Cartão novo = `GCMemcard::Format` do
  Dolphin: bloco 0 cabeçalho (serial = flash ID "DOLPHINSLOTA" embaralhado com o horário da formatação, tamanho em Mbit,
  Windows-1252, soma e soma inversa), blocos 1 e 2 o diretório vazio (soma 0xF003), blocos 3 e 4 a BAT (59 livres, último
  alocado 4), resto em 0xFF. Padrão **4 Mbit (59 blocos, 512 KiB)**; um `.raw` que já existe pode ter qualquer tamanho de
  4 a 128 Mbit (potência de dois), como no Dolphin.
- `exi_card.c` (`am/exi_card.h`): o aparelho `CEXIMemoryCard` (EXI_DeviceMemoryCard.cpp), canal 0, aparelho 0, chip select
  1. Comandos pelo primeiro byte depois do chip select: NintendoID (ID = tamanho em Mbit, `0x00000004`), ReadArray
  (endereço AD1/AD2/AD3/BA; byte a byte ou DMA), ReadStatus, ReadID (`0xC221`), SetInterrupt, ClearStatus, SectorErase,
  PageProgram, ChipErase. O estado inicial é o do Dolphin (`BUSY|UNLOCKED|READY`, o desbloqueio por DSP do SDK não
  roda).
- `exi.c`: o canal passa a ter o aparelho 0 (EXT = 1 no status do canal 0, o aparelho recebe o `SetCS`, o DMA e a
  interrupção `EXIINT`), e o SRAM ganha o flash ID do slot A calculado do cabeçalho do cartão (`SetCardFlashID`).

**Tempo, como o Dolphin** (é o que mantém o tempo emulado igual): `DMARead` e `DMAWrite` copiam na hora, mas o `TCINT`
só chega depois de `tamanho * (486 000 000 / taxa)` ciclos, com taxa de leitura 512 KiB/s (926 ciclos por byte) e de
escrita 98 432 B/s (4 937 ciclos por byte). Transferências imediatas **não** geram `TCINT` no cartão
(`UseDelayedTransferCompletion`): o SDK as espera por polling de `TSTART`. Apagar setor e PageProgram agendam o fim de
comando 5 000 ciclos depois, que limpa `BUSY`, liga `READY` e levanta a interrupção do cartão se o SetInterrupt a ligou.
Os eventos são checados a cada volta do chassi (`bus_update`), como o DVD.

## 3. Arquivo do cartão

- `--memcard <arq.raw>`: o arquivo do slot A. Se não existe, a runtime cria e formata um cartão novo de 4 Mbit (com o
  horário do `--rtc`, e não o relógio do host como o Dolphin, para a execução ser determinística) e o grava ao fechar.
  `--memcard none` desliga.
- **Padrão**: só no modo interativo (janela sem `--pad-script`) o cartão é `MemoryCardA.raw` no diretório de dados do
  usuário (`SDL_GetPrefPath("automodellista", "AutoModellista")`, no macOS
  `~/Library/Application Support/AutoModellista/`), fora do repositório. Nos outros modos o padrão é **sem cartão**,
  para os roteiros do M5 a M8 (que escolhem "Yes" em "No Memory Card" no quadro 360) e os golden continuarem iguais.
- O arquivo é gravado ao fechar a runtime (fim normal, janela fechada ou `am_fatal`, por `atexit`), por um temporário e
  `rename`, só se o jogo escreveu algo. Um `kill -9` perde a sessão (o Dolphin grava a cada 15 s; a runtime, não).
- Nunca versionar o `.raw`: tem o save do jogo.

## 4. Provas

### Teste unitário (`tests/unit/memcard_test.c`, `ctest -R memcard`)

Seis testes, 54 verificações, pelos registradores como o SDK os usa: formato do cartão novo (campos, somas, BAT, mesmo
horário = mesmos bytes), slot A vazio sem cartão (EXT = 0), ID e flash ID da SRAM (o flash ID desfaz o embaralhamento
com qualquer horário de formatação), leitura por byte e por DMA com o tempo do `TCINT` (512 bytes: 474 112 ciclos, nem
um antes), apagar setor com `BUSY` e fim 5 000 ciclos depois, PageProgram imediato e DMA de escrita (128 bytes:
631 936 ciclos), SetInterrupt/`EXIINT`.

### Roteiro de controle: salvar e ler (`scripts/verify_card.sh`)

Execução 1, cartão novo (a runtime o cria): cria o System File no boot (`400:A`), vai ao menu e salva; execução 2, com o
cartão da 1: carrega. Roteiros (quadros iguais nos dois lados, cada botão 8 quadros):

```
salvar: 400:A, 1700:START, 2000:START, 2600:D_DOWN, 2660:D_DOWN, 2720:D_DOWN, 2800:A (save/load),
        3000:A (save), 3200:A (Slot A), 3400:D_LEFT, 3450:A (overwrite yes)          até o quadro 4300
ler:    1700:START, 2000:START, 2600:D_DOWN x3, 2800:A, 3000:D_DOWN, 3060:A (load), 3260:A (Slot A), 3500:A (yes)
```

O verify confere nos dois builds (arm64 e x86_64): o cartão depois de salvar tem o System File (19 blocos, 40 livres),
a execução de leitura leu os dados do cartão, o cartão depois da leitura ainda tem o arquivo, e o cartão e as telas
"saved" (q3800) e "loaded" (q3700) são idênticos nos dois builds. O golden `tests/golden/m8_card.json` guarda só
contagens e SHA-256.

### Contra o Dolphin

O mesmo roteiro de salvar no Dolphin 2609a (`dolphin_trace.py events --memcard ...`: slot A = cartão de memória
(`SlotA=1`), o mesmo `.raw` inicial, RTC igual) dá, no q4300:

- **cartão: 0 bytes diferentes** (524 288 bytes iguais, inclusive o horário do arquivo no diretório e as somas);
- **tempo emulado: defasagem 0,000 campo** em todos os 4300 quadros (`compare_timebase.py`), com o cartão ligado e
  gravando 400 KiB;
- eventos do DVD: os mesmos 339 eventos, os 71 estritos idênticos e as 256 leituras em fluxo idênticas.

## 5. O boot sem cartão não mudou

O jogo sonda o slot A no boot. Sem `--memcard` o slot continua vazio e nada mudou (o `exi_update` do chassi só confere
dois booleanos): roteiro padrão do M8 (6400 quadros, sem cartão) com o build desta peça: os mesmos 551 eventos
(`compare_events.py`: estrito igual, fluxo igual, 12 eventos de módulo nos mesmos quadros) e defasagem 2,096 campos no
q6400, igual ao golden do M8 parte 1. Com cartão o tempo muda de fato (o jogo cria o System File, 19 blocos), e por isso
o roteiro do cartão é outro e tem o seu próprio golden.

## 6. Decisões, hipóteses descartadas e o que ficou de fora

- **Formatar na runtime, como o Dolphin**, e não esperar que o jogo formate (a tela "No Memory Card"/formatação não
  foi explorada: o Dolphin entrega o cartão já formatado e o jogo só cria o System File nele). A data da formatação vem
  do RTC emulado.
- **Cartão de 4 Mbit**, não os 128 Mbit que o Dolphin cria por padrão: o System File cabe com folga (19 blocos), e o
  tamanho entra no ID (`0x00000004`) e no header, então o jogo vê um cartão de 59 blocos como num cartão real barato.
- `.gci` por arquivo (do `PLANO_PORT_C.md` §3) **não foi feito**: o `.raw` é o que o Dolphin 2609a usa por padrão e
  permite comparar byte a byte. Importar/exportar `.gci` e o modo "GCI folder" ficam de fora.
- Slot B, cartão de mais de um jogo, troca do cartão com o jogo rodando (EXTINT/ejetar) e gravação periódica ficam de
  fora; o único caminho de gravação é ao fechar.
- Hipótese descartada: precisar de tempo especial no boot para o cartão (`MC_STATUS_BUSY` inicial, desbloqueio). O
  SDK do jogo lê o status, vê `UNLOCKED` e segue; a defasagem de tempo contra o Dolphin ficou em zero.

## 7. Valores do golden que mudariam

Nenhum: `tests/golden/m8_corrida.json` e os de M5 a M7 não mudam (o roteiro padrão não toca o cartão). Novo:
`tests/golden/m8_card.json`.
