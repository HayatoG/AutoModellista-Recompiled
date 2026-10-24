# Endereços conhecidos (GAUE08)

Tudo aqui é evidência externa ou derivada; nada é nome definitivo. Confirmar no decomp antes de usar como fato.

| Endereço | O quê | Fonte | Confiança |
| --- | --- | --- | --- |
| 0x80003154 | `__start` (entry point) | DOL header, dtk | confirmado |
| 0x800719AC | `main` | dtk (análise de `__start`) | confirmado |
| 0x800C25DC | `OSInit` | dtk (assinatura SDK) | confirmado |
| 0x801B3018 | tempo total de jogo (u32) | Action Replay do Dolphin `GAUE08.ini` | hipótese forte |
| 0x801B301C | total de corridas (u16) | idem | hipótese forte |
| 0x801B301E / 20 / 22 / 24 | contagem de 1º / 2º / 3º / 4º+ lugares (u16) | idem | hipótese forte |
| 0x801EA826 | estado dos botões do controle 1 (u16; 0x0020 = R? 0x0080 = C-stick?) | condicionais `3A1EA826` dos AR | hipótese |
| 0x80205CC0 | flag ligada a velocidade máxima / câmbio | AR "No Top Speed/No Shifting Needed" | hipótese |
| 0x80206704 + n·0xB60 | struct do carro n (n = 0..6), campo de progresso/posição na pista | AR "C Stick Sends Car N Back To Start" | hipótese forte para o passo 0xB60 |

Configuração de vídeo exigida pelo Dolphin (`GAU.ini`): `ImmediateXFBEnable = False` (evita flicker de camadas
na corrida). A ModernGekko herda o banco `GameSettings` do Dolphin, então isso já se aplica.

## Alvos priorizados no decomp.wtf (2026-10-23)

Derivados de `analysis/call_graph.json` (chamadas diretas) e de cargas de constantes do `.sdata2` (SDA2 base
`0x802D6720`). Todos são hipóteses até o match/leitura do C confirmar.

| Ordem | Função | Hipótese | Evidência |
| --- | --- | --- | --- |
| 1 | fn_800489D4 | ~~fim de frame~~ **encerramento da aplicação** (casada) | strings `gcfounda.c`, linha 485, `End of application`; VISetBlack → VIFlush → VIWaitForRetrace → OSPanic |
| 2 | fn_80071D00 | **init da câmera principal** (casada) | grava a struct `0x80211BFC` (ver abaixo) |
| 3–5 | fn_8001222C, fn_800122DC, fn_8001239C | setup de câmera | vizinhas, todas carregam 4/3 |
| 6 | fn_80051034 | laço de frame | 27 chamadas, laço, chama fn_800D276C |
| 7 | fn_80036690 | projeção 2D/HUD? | carrega 4/3; 20 chamadores |
| 8 | fn_80035F88 | getter/setter de aspect? | 44 bytes, carrega 4/3 |
| 9 | fn_80048BD8 | laço chamado por main | 1216 bytes, 17 chamadas |
| 10 | main | boot | dtk |
| 11 | fn_800D276C | VIWaitForRetrace | OSDisableInterrupts + laço com fn_800C89FC + OSRestoreInterrupts |
| 12 | fn_800D345C | VIFlush (revisado no M5) | junta as máscaras de registradores pendentes do VI; sem argumento (REA/Ghidra) |
| 13 | fn_800D35F8 | VISetBlack (revisado) | chamada com 1 antes de VIFlush no encerramento; no Dolphin r3 = 1 |
| 13a | fn_800D358C | VISetNextFrameBuffer (M5) | guarda o XFB em 0x8026E648 (o VIFlush copia); chamada por fn_8004FAA8/fn_8004FBA8 (REA/Ghidra) |
| 14 | fn_800D2C34 | VIConfigure | 2 KB, módulo vi.c |

Funções que carregam a constante deg→rad (`0.017453292`) — candidatas a câmera/física de rotação:
fn_80010ED8, fn_80015400, fn_80027180, fn_800A1E20, fn_80019A7C e outras (lista completa via `scripts/call_graph.py`).

## Struct da câmera principal — `0x80211BFC` (de fn_80071D00, casada)

| Offset | Endereço | Valor inicial | Hipótese |
| --- | --- | --- | --- |
| +0x28 | 0x80211C24 | 1000.0 | plano distante |
| +0x2C | 0x80211C28 | 1.0 | plano próximo |
| +0x30 | 0x80211C2C | 30.0 | FOV vertical em graus |
| +0x34 | 0x80211C30 | 1.3333 | **aspect ratio** — alvo do widescreen (16:9 = 1.7778, `0x3FE38E39`) |
| +0x158 / +0x15C | 0x80211D54 / 58 | 640 / 448 | viewport/EFB |

Teste barato antes de tocar em código: no Dolphin, escrever `0x3FE38E39` em `0x80211C30` (Action Replay
`04211C30 3FE38E39`) e ver se a imagem 3D fica em 16:9 sem esticar. Outras funções que carregam 4/3
(fn_8001222C/122DC/1239C, fn_80036690) podem reescrever o campo.
