# Análise da ISO (marco M1)

Executado em 2026-10-23 com `scripts/phase1_iso.sh` (somente leitura sobre o original). Metadados brutos em
`analysis/iso/`.

## Identificação

| Campo | Valor | Fonte |
| --- | --- | --- |
| Título | auto modellista | boot.bin |
| Game ID | **GAUE08** (G = GameCube, AU = jogo, E = EUA, 08 = Capcom) | boot.bin |
| Região | NTSC-U | boot.bin |
| Disco / revisão | Disc 1, **Revision 0** | boot.bin, dtk |
| Streaming de áudio (byte 0x08) | 1 (habilitado no cabeçalho) | boot.bin — uso real não confirmado |
| Verificação | **Redump "Auto Modellista (USA)" ✅** — CRC32 `ab80ecfb`, SHA-1 `800b74412f60939c2d93d7f1ce679cd3da068e55`, XXH64 `87fe73d836e11ed5` | `dtk disc verify` |
| Arquivo local | `Auto Modellista (USA).rvz` (RVZ, Zstandard, bloco 128 KiB, lossless), SHA-256 `610d027f…70b4` | |
| Cópia no .zip | mesmo RVZ (TorrentZip), 743.620.524 bytes | `unzip -l` |
| Apploader | 2002/09/05 | apploader.img |

Não há dúvida de revisão: o dump bate com o único registro Redump conhecido do NTSC-U.

## main.dol

SHA-256 `75b22d528ebf702972931f901f48ce0e81cbf2b98567b3471d75182c1bdee71c`, 1.598.944 bytes, entry point
`0x80003154` (`__start`).

| Seção | Endereço | Tamanho |
| --- | --- | --- |
| .init | 0x80003100 | 0x25A8 (inclui a tabela de vetores do MetroTRK, que é dado) |
| .text | 0x800056C0 | 0x14AC68 (~1,29 MB, 3.868 funções segundo o dtk) |
| .ctors / .dtors | 0x80150340 / 0x80150360 | 0x4 / 0x8 |
| .rodata | 0x80150380 | 0x8BAF |
| .data | 0x80158F40 | 0x2DB60 |
| .bss | 0x80186AA0 | 0x146E8C |
| .sdata / .sbss / .sdata2 / .bss2 | 0x802CD940 … 0x802D0DC0 | pequenos |

`main` está em `0x800719AC` (identificado pelo dtk).

## Módulos REL (`files/module/`)

O código de jogo está dividido: o DOL tem SDK, middleware e núcleo; menus, corrida e textos são RELs carregados
dinamicamente. A ordem de IDs bate com a lista oficial `rave.str` (codinome interno do projeto: **raveGC**,
`E:\work\raveGC\raveSub\gc_project\*.plf`); `raveD.str` lista as versões de debug (`*D.plf`), ausentes do disco.

| REL | ID | .text | Instruções |
| --- | --- | --- | --- |
| game.rel | 1 | 0x4D124 | 78.921 |
| menu.rel | 2 | 0x57B94 | 89.829 |
| menu00.rel | 3 | 0x16CE8 | 23.354 |
| menu01.rel | 4 | 0x1CFF4 | 29.693 |
| vj.rel | 5 | 0x1F784 | 32.225 |
| msg_e/f/g/i/j/k/s.rel | 6–12 | ~0x230 cada | 120–147 cada (tabelas de texto por idioma) |
| **test.rel** | **3 (duplicado)** | 0x91C | resto de desenvolvimento, fora de `rave.str` |

Total: DOL ≈ 341 mil instruções, RELs ≈ 255 mil (**≈43% do código está em RELs**).

## Arquivos de dados

| Arquivo | Tamanho | Formato |
| --- | --- | --- |
| afs00.afs | 503 MB | AFS (CRI) |
| afs01_gu.afs | 216 MB | AFS (CRI) |
| afs02.afs | 137 MB | AFS (CRI) |
| 0flist.dir | 36 B | lista em texto dos três AFS |
| Fonts/*.bin | 21 KB–781 KB | fontes bitmap proprietárias (NEC, UTF-8, coreano) |
| GC/*.ico | 5–14 KB | ícones de memory card (Sistema, Replay, **Network**) |
| opening.bnr | 6,5 KB | banner BNR1 |

O conteúdo interno dos AFS (texturas, modelos, ADX, SFD) ainda não foi catalogado.

## Bibliotecas identificadas (evidência por strings no main.dol)

| Biblioteca | Evidência |
| --- | --- |
| Nintendo Dolphin SDK | 13 módulos: OS, EXI, SI, DVD, VI, PAD, AI, AR, ARQ, AX, DSP, CARD, GX. Builds de 05/09/2002 a 17/03/2003, "GC05Sep2002Patch2" |
| CodeWarrior / MSL / MetroTRK | `Metrowerks Target Resident Kernel`, `MW2407 GC05Sep2002Patch2`, runtime `__init_cpp_exceptions` etc. |
| CRI ADX (áudio) | ADXT 8.66, ADXF 7.04, ADXGC 1.21, AXRNA 1.02 (ADX sobre AX) |
| CRI Sofdec (vídeo MPEG) | MWSFD 2.65b, SFD 1.845, MPV 1.843, MPS 1.669, DCT 1.803 |
| CRI CVFS/SJ/SVM/LSC | sistema de arquivos e streaming da CRI |

## Compilador

Hipótese forte, não confirmada só por strings: **CodeWarrior GC 1.3.2** (versão 2.4.2 build 81), coerente com o
SDK de set/2002. Evidência: o projeto decomp.wtf configurado com `GC/1.3.2` já casou 593 funções byte a byte
(`/Volumes/DecompAM/Decomp/auto-modellista-gaue08`). Falta checar se há funções do jogo (não SDK) que só casam com
outras versões ou flags.

## Rede

O ícone `IconNetwork.ico` e strings em `msg_i.rel` indicam que a versão GC tem referências a modo de rede
(o jogo suportava LAN via Broadband Adapter no GameCube — **hipótese a confirmar** pelos símbolos do SDK de rede,
que não aparecem no main.dol).
