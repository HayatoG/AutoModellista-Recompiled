# Roteiro: port nativo do Auto Modellista

Oct 23, 2026 · @Guilherme

## Ponto de partida

O projeto começou com uma pergunta simples: dá para rodar Auto Modellista (Capcom, GameCube, 2002–2003, ID GAUE08) nativo num PC x86\_64? A resposta curta foi: sim, mas não só com o decomp.

- **O que já existia:** um projeto de matching decompilation no decomp.wtf, rodando local (127.0.0.1:6464), com agentes de IA (Ollama qwen3 8b) casando funções. Estava em 3,04% do código (933 de 3.903 funções).
- **Contexto do decomp.wtf:** em 15/10/2026 o autor descontinuou o projeto, porque boa parte da comunidade de decompilação é contra decompilação feita por IA. A instalação local continua funcionando, sem atualizações.
- **Por que o decomp não basta:** o C casado recria o mesmo `.dol` do GameCube byte a byte. Para rodar no PC ainda faltaria reimplementar vídeo (GX), áudio (AX/DSP), disco, controle e memory card, tratar a ordem dos bytes (PowerPC é big-endian) e os ponteiros de 32 bits. E levar o decomp de 3% a 100% leva muito tempo.
- **O caminho escolhido:** recompilação estática com o DolRecomp, que traduz todo o PowerPC do DOL e dos RELs para C de uma vez, mais uma runtime própria que substitui o hardware. O decomp segue em paralelo, para dar nomes às funções e permitir mods no futuro.
- **Ponto de partida técnico:** a pasta `AutoModellista-Recompiled` já tinha os marcos M0–M2 (ferramentas, análise da ISO e recompilação do DOL e dos 12 RELs para C, compilando sem erros).

## Conceitos

Há três jeitos de levar um jogo de console para o PC, e o projeto combina os três, cada um num papel.

| Abordagem | O que faz | Papel aqui |
| --- | --- | --- |
| Emulação (Dolphin) | Interpreta ou faz JIT do PowerPC e simula o hardware do GameCube enquanto o jogo roda | Oráculo: roda o jogo original para conferir se o port faz a mesma coisa |
| Matching decompilation (decomp.wtf, dtk, objdiff) | Reescreve cada função em C até o compilador original (CodeWarrior GC 1.3.2) gerar os mesmos bytes | Nomes legíveis e, no futuro, base para mods (widescreen, 60 fps) |
| Recompilação estática (DolRecomp) | Traduz todo o PowerPC para C de uma vez, antes de rodar | O port em si: vira código nativo ARM64 ou x86\_64 |

- **Runtime:** o C gerado sozinho não roda. Ele chama a runtime para ler e escrever memória, falar com o hardware (registradores MMIO) e despachar chamadas indiretas. A runtime é o “GameCube falso” escrito em C: memória, interrupções, disco, vídeo, áudio e controle.
- **SDK embutido:** as bibliotecas da Nintendo (OS, GX, DVD, AX, CARD) e da CRI (ADX, Sofdec) estão dentro do DOL e foram recompiladas como código do jogo. A runtime emula o hardware por trás delas, não as funções.
- **RELs:** cerca de 43% do código (corrida, menus, VJ, textos) fica em módulos carregados dinamicamente. O jogo os carrega com `OSLink` em endereços que mudam; a runtime traduz esses endereços para o código recompilado.
- **Oráculo:** o mesmo trecho roda no Dolphin e no port. Cada um grava um trace (funções chamadas, leituras de disco, módulos carregados) e um script compara até a primeira divergência. O executável final não depende do Dolphin.
- **Não é preciso zerar o jogo:** todo o código já foi traduzido. O que precisa de teste é a runtime, por caminhos representativos (abertura, demo, menus, uma corrida de cada modo, save, vídeo).

## Decisões de arquitetura

A decisão central foi escrever uma runtime própria em C17 sobre SDL3 e deixar de lado a ModernGekko, a runtime oficial do DolRecomp.

| Decisão | Escolha | Por quê |
| --- | --- | --- |
| Linguagem da runtime | C17, sem C++ | Portável para qualquer plataforma; o usuário pediu C |
| Dependência de plataforma | Só SDL3 (janela, áudio, input, threads, GPU sobre Vulkan/Metal/D3D12) | Um único ponto de contato com o sistema, em `runtime/platform/` |
| Runtime | Própria, não ModernGekko | A ModernGekko é C++ sobre o Dolphin inteiro, não compila no MSVC, ocupa vários GB e deixaria os RELs (\~43% do código) no JIT do Dolphin |
| Código não coberto | Erro fatal, sem JIT | Nada roda emulado escondido; o que falta aparece na hora |
| Nível da emulação | Hardware (MMIO, gather pipe), com HLE só para threads, OSLink, disco, memory card e DSP | Pega até as chamadas de vídeo que o compilador colocou inline |
| Memória do jogo | Big-endian, acessada só por `mem_read*`/`mem_write*` | Resolve ordem de bytes e ponteiros de 32 vs 64 bits num lugar só |
| Vídeo | Decodificador de FIFO + um ubershader TEV compilado offline | Sem compilação de shader durante o jogo, sem travadas |
| Disco | Arquivos extraídos com `dtk disc extract` | Evita implementar o formato RVZ |
| Licença | GPL-3.0 | O C gerado inclui o `cpu.h` do DolRecomp |

**Padrões de código** (`docs/PADROES_DE_CODIGO.md`), aplicados por `clang-format`, `clang-tidy` e `.editorconfig`:

- Um módulo = um prefixo = um header público; estado em uma `static struct` por arquivo.
- Warnings como erro na runtime; o C gerado compila num alvo separado, sem warnings.
- Toda constante de hardware cita a fonte; hipóteses marcadas como `HIPÓTESE`.
- Nenhum byte do jogo no repositório, nem em testes.

**Divisão de modelos de IA** (também uma decisão de projeto):

| Tipo de tarefa | Modelo | Exemplo |
| --- | --- | --- |
| Arquitetura, depuração contra o Dolphin, revisão | Opus | M4 (boot), M5 (OS e disco), M6 (primeiro quadro), M7 (menus), M8 (tempo de CPU e GPU) |
| Implementação com especificação clara | Sonnet | M3 (esqueleto e build) |
| Tarefas mecânicas com verificador automático | Haiku | Matching de funções no decomp (o objdiff dá o veredito) |
| Verificação mecânica em paralelo, fora da cota do Claude | Codex CLI (GPT-6 Luna, raciocínio baixo) | Builds x86\_64/Debug com a SDL nova (M5) |
| Matching do decomp em paralelo, fora da cota do Claude | opencode CLI (DeepSeek V4.1 Flash via OpenCode Go) | Funções do decomp num branch próprio (M5) |

**Agentes de outras ferramentas (23/10, M5):** para não gastar a cota semanal do Claude em tarefas mecânicas, a sessão principal passou duas tarefas para o Codex CLI e o opencode CLI, rodando em segundo plano. Regras de segurança: cada um numa `git worktree` própria (o repo principal e as builds do agente do M5 ficam intocados); o Codex no sandbox `workspace-write` sem rede (a SDL vem de uma cópia local); o opencode com `opencode.json` que nega qualquer comando fora de uma lista (sem `rm`, `push`, `reset`, `clean`, troca de branch); a cópia do jogo fica fora do git e proibida no prompt; tudo com `with_timeout.sh`; a sessão principal confere o resultado antes de aceitar.

**GX em software em vez do ubershader (24/10, M6):** o plano previa desenhar com a GPU (SDL_GPU e um ubershader TEV). O agente do M6 traduziu para C o backend Software do Dolphin 2609a: o jogo desenha num EFB na memória, a cópia EFB→XFB grava o quadro em YUV na RAM emulada como o hardware, e a janela SDL3 só mostra esse XFB. Vantagem: a captura da runtime e a do Dolphin são o mesmo dado, então dá para comparar pixel a pixel. Custo: desenhar fica cerca de 10 vezes mais lento (0,11 s por quadro numa corrida). A ida para a GPU fica para o M8, com o software como referência.

**Desempenho ainda sem GPU (24/10, M7):** para dar para jogar já, o M7 acelerou o desenho em software em vez de ir para a GPU: o rasterizador divide cada quadro em faixas de linhas entre as threads (cada pixel tem um dono só, então 1 ou 10 threads dão a mesma imagem, byte a byte), as texturas decodificadas ficam em cache, e a janela em tempo real pula o desenho de até 4 quadros seguidos quando atrasa. O jogo anda a 57–60 quadros/s; a imagem dos menus atualiza 12 a 17 vezes por segundo. O perfil mostrou que metade do quadro é serial (a CPU emulada gasta \~9 ms), então mais threads não trazem os 60 quadros/s desenhados: isso é a GPU, no M8.

**GPU com um shader que é o software (24/10, M8 parte 1):** o `--gfx gpu` leva o desenho para a placa de vídeo com o SDL3 GPU e Metal. Em vez de usar o blend e o teste de profundidade fixos da GPU, o EFB fica em duas imagens de inteiros que o próprio shader lê e escreve: o TEV, o alfa, o Z e o blend são feitos com a mesma aritmética inteira do software. As cópias do EFB trazem só as linhas copiadas de volta para a CPU e usam o código do software. Nesta máquina não há o SDL_shadercross, então o shader foi escrito em MSL e compilado antes, embutido no executável: por enquanto a GPU só funciona no Mac com chip Apple, e as outras plataformas usam o software. Resultado inesperado: a imagem da GPU ficou mais perto do Dolphin que a do software, provavelmente porque o oráculo também desenha numa GPU.

## Linha do tempo

Em 23/10/2026, o port foi do plano até o jogo rodando nativo dos menus até a largada de uma corrida, lendo o disco e trocando módulos do mesmo jeito que o original. Na madrugada de 24/10, a runtime desenhou o primeiro quadro: o logo da Capcom, igual ao do Dolphin. Horas depois, já dava para jogar pelos menus com controle e som, e antes do amanhecer o desenho foi para a GPU e o relógio do jogo passou a bater com o do Dolphin. Mais recente primeiro:

| Data | Marco | Modelo | Resultado verificado | Commits |
| --- | --- | --- | --- | --- |
| 24/10 | M8 parte 2a: GX numa thread própria (concluída às 11:55, meta não atingida) | Opus | O desenho passou a rodar numa thread separada da CPU emulada, com capturas e tempo emulado idênticos byte a byte aos de antes (conferido no `verify_m8.sh`, que passa nos dois builds). Mas a corrida continua em 53 quadros/s de jogo e 31 desenhados: quase toda a espera fica num ponto só, o DrawDone, em que o jogo pede e espera o fim do desenho (27 s de 85 s em arm64). Por isso a thread ficou desligada por padrão (`--gx-thread on\|off`) | `1163500` |
| 24/10 | M8 parte 1: tempo de CPU e GPU (concluída às 06:21) | Opus | Tempo emulado contra o Dolphin no quadro 6400: de 1,37 s (82 campos de vídeo) para 0,035 s (2,1 campos), idêntico em arm64 e x86\_64; a meta era menos de 1 campo. Câmera da largada contra o Dolphin: de 20,9 para 29,4 dB. Com `--gfx gpu`, 34,9 a 43,3 dB contra o Dolphin em 4 quadros. Na janela (arm64), o menu desenha 60 quadros/s (eram 12–17); a corrida anda a 53 quadros/s de jogo e desenha 31, abaixo da meta de 60. 49/49 testes; `verify_m8.sh --oracle` passa, conferido de novo pela sessão principal com o Dolphin rodando outra vez, junto com os verify do M5, M6 e M7 | `d1a1aea`, `1441024`, `a5a2a6f`, `861ac14`, `7dbcfad`, `7521641` |
| 24/10 | M7 menus (concluído às 03:49) | Opus | A janela lê teclado e gamepad, e o jogo é navegável da abertura à largada; o som do AX sai pelo SDL3 e o `--audio-dump` grava as mesmas amostras do Dolphin, só deslocadas de alguns ms (correlação mediana 1,0, nível igual em 100% das janelas); o filme da abertura aparece com som. Menus contra o Dolphin: 31,4 a 40,6 dB em 6 quadros, menos de 1% de pixels com diferença visível. 57–60 quadros/s de jogo na janela. 47/47 testes; `verify_m7.sh` passa, conferido de novo pela sessão principal com o Dolphin rodando outra vez | `801c454`, `ff47f2b`, `14fcc48`, `297bc87`, `1efb410`, `4c5cc8e`, `b640dbf` |
| 24/10 | M6 primeiro quadro (concluído à 01:16) | Opus | O logo da Capcom aparece numa janela SDL3 no mesmo quadro do Dolphin (fade a partir de \~1076, inteiro de \~1110 a \~1410). Quadro 1250: PSNR 46,37 dB; quadro 1100 (fade): 49,18 dB; nenhum pixel com diferença acima de 24 nos dois. Arm64 e x86\_64 dão pixels idênticos; a janela mostra exatamente o XFB (99 dB); 44/44 testes. `verify_m6.sh` passa, conferido de novo pela sessão principal com o Dolphin rodando outra vez | `51da2d6`, `d901d25`, `985f7ff`, `f30abbb`, `eaf5da1` |
| 23/10 | Merge do opencode no decomp | (sessão principal) | 6 funções a 100% no objdiff entram na `main` do decomp por fast-forward; o `main.dol` continua com o SHA-1 certo | `9b9a66e` (decomp) |
| 23/10 | M5 OS e disco (concluído às 23:51) | Opus | Com um roteiro de controle que atravessa os menus até a largada, a runtime liga o `game.rel` no quadro 5731 e roda 669 quadros depois, sem erro, em arm64 (57 s para 6.400 quadros) e x86\_64 (81 s). Os 12 eventos de módulo caem nos mesmos quadros do Dolphin; 103 eventos estritos idênticos e na mesma ordem; 441 leituras de música ADX com a mesma contagem por trecho; 39/39 testes; `verify_m5.sh` e `verify_m4.sh` passam, conferidos de novo pela sessão principal | `4cd34e0`, `73cdf41`, `456174f`, `557e0f1`, `501e2cc`, `73d7e10`, `bb1131f`, `71330eb`, `c2d96c8`, `6be5216`, `6c8c419` |
| 23/10 | Builds x86\_64 Release, x86\_64 Debug e arm64 Debug com SDL 3.4.18 | Codex (GPT-6 Luna) | Três builds compilam, 38/38 testes em cada; verificado de novo pela sessão principal | (sem commit: só verificação) |
| 23/10 | SDL3 3.4.14 → 3.4.18 | (sessão principal) | 26/26 testes; boot idêntico (41.422 voltas, 6.420 chamadas) | `09deea0` |
| 23/10 | M4 boot até `main` | Opus | Trace idêntico ao Dolphin em 1.998 eventos (2.733 com trechos de 128); arm64 e x86\_64; 26 testes | `852ba6d`, `7614f88`, `64a09b6` |
| 23/10 | M3 esqueleto | Sonnet | DOL + 12 RELs linkados num executável (155 trechos); 12 testes; CI escrito | `f3dd0ac` |
| 23/10 | Plano do port em C e padrões de código | (sessão principal) | `PLANO_PORT_C.md`, `PADROES_DE_CODIGO.md`, configs de lint | `f3dd0ac` |
| antes de 23/10 | M0–M2 ferramentas, ISO, recompilação | (sessões anteriores) | ISO bate com o Redump; 0 instruções desconhecidas; todo o C compila | `f3dd0ac` |

O que cada marco provou, em uma linha:

- **M3:** o C gerado de todo o jogo cabe num executável só, sem conflito de símbolos entre DOL e RELs.
- **M4:** o próprio código do jogo, recompilado, inicializa o SDK e imprime “Dolphin OS … Console Type: Development HW3”, como no console.
- **M5:** o jogo nativo atravessa os menus com o mesmo roteiro de botões do Dolphin, pede os mesmos arquivos ao disco e liga o módulo da corrida no mesmo quadro que o original.
- **M6:** a runtime desenha a abertura do jogo sozinha, e a imagem bate com a do Dolphin pixel a pixel, dentro de um limite documentado.
- **M7:** dá para jogar pelos menus com controle, ouvindo a música e os efeitos iguais aos do Dolphin, e o filme da abertura toca com som.
- **M8 parte 1:** o relógio do jogo nativo anda junto com o do Dolphin até a carga da pista, e a GPU desenha os menus a 60 quadros por segundo com a imagem ainda mais perto do original.

### Tokens e tempo por etapa

Até 24/10 às 06:56 (horário de Brasília), o projeto processou cerca de 743 milhões de tokens, dos quais 3,4 milhões foram texto gerado pelos modelos. Somado dos registros de cada sessão do Claude Code, contando cada resposta uma vez.

| Etapa | Modelo | Início → fim | Duração | Respostas | Tokens gerados | Tokens processados |
| --- | --- | --- | --- | --- | --- | --- |
| M8 parte 2a (GX numa thread própria) | Opus 5.5 | 24/10 11:19 → 11:55 | 36 min | 44 | 54.956 | 4,8 M |
| M8 parte 1 (tempo de CPU igual ao do Dolphin, GX na GPU, `verify_m8.sh`) | Opus 5.5 | 24/10 04:18 → 06:21 | 2h03 | 271 | 270.722 | 80,9 M |
| M7 menus (controle, áudio do AX, desempenho, `verify_m7.sh`) | Opus 5.5 | 24/10 01:57 → 03:49 | 1h52 | 266 | 292.214 | 116,4 M |
| Sessão principal, retomada após o reinício (commits pendentes, roteiros, remontagem automática, merge do opencode, conferência do M5, do M6, do M7 e do M8 parte 1) | Opus 5.5 | 23/10 23:22 → 24/10 06:56+ | 7h34+ | 240 | 195.674 | 42,8 M |
| M6 primeiro quadro (GX em software, janela SDL3, comparação de capturas, `verify_m6.sh`) | Opus 5.5 | 24/10 00:22 → 01:16 | 54 min | 146 | 249.251 | 54,8 M |
| M5 OS e disco, parte 3 (menus até a largada, tempo do DVD, `verify_m5.sh`) | Opus 5.5 | 23/10 23:23 → 23:51 | 28 min | 127 | 97.658 | 21,4 M |
| opencode: matching do decomp (6 funções, interrompido pela queda do SSD) | DeepSeek V4.1 Flash (OpenCode Go) | 23/10 17:44 → 18:04 | 20 min | 98 passos | 30.013 (12.031 + 17.982 de raciocínio) | 4,9 M |
| Codex: builds x86\_64/Debug com SDL 3.4.18 (2 tentativas) | GPT-6 Luna | 23/10 17:44 → 17:57 | 12 min | — | 14.602 | 5,5 M |
| M5 OS e disco, parte 2 (parou na seleção de pista: 3ª queda do SSD) | Opus 5.5 | 23/10 17:11 → 19:06 | 1h55 | 114 | 55.951 | 20,3 M |
| Sessão principal, retomada após a queda do volume | Opus 5.5 | 23/10 17:08 → 22:09 | 5h01 | 96 | 81.725 | 13,8 M |
| M5 OS e disco, parte 1 (interrompida: o volume do SSD desmontou) | Opus 5.5 | 23/10 14:49 → 16:35 | 1h46 | 248 | 322.027 | 105,3 M |
| M4 boot até `main` | Opus 5.5 | 23/10 13:01 → 14:33 | 1h32 (inclui a parada do disco cheio) | 201 | 275.489 | 74,2 M |
| M3 esqueleto | Sonnet 5.5 | 23/10 12:26 → 12:49 | 23 min | 48 | 97.718 | 7,0 M |
| Sessão principal (plano, revisões, SDL3, roteiros, Ghidra/REA) | Opus 5.5 | 23/10 11:58 → 16:36+ | 4h38+ | 172 | 159.018 | 37,4 M |
| Agentes do decomp.wtf (5 em paralelo) | Sonnet 5.5, Haiku 5.5 | 22/10 22:35 → 23/10 02:45 | 4h10 | 1.925 | 1.095.949 | 103,1 M |
| M0–M2 (ferramentas, ISO, recompilação) | Opus 5.5 | 22/10 22:03 → 23/10 02:54 | 4h51 | 220 | 147.256 | 49,7 M |
| Sessão de 22/10 (início do projeto) | Opus 5.5 | 22/10 01:57 → 02:46 | 49 min | 100 | 41.470 | 12,4 M |

- **Tokens processados** inclui o contexto relido a cada resposta (cache). É a maior parte do volume, mas é a parte mais barata.
- **Tokens gerados** é o que os modelos escreveram: código, documentos, comandos e respostas.
- Duração é o tempo de relógio entre a primeira e a última resposta, incluindo esperas por builds e pelo Dolphin.
- Codex e opencode não aparecem no `/usage` do Claude: os números vêm do log JSON de cada CLI (`/Volumes/AMRecomp/logs/paralelos/`); não consomem a cota do Claude.
- Fonte: `scripts/usage_report.py` sobre os registros em `~/.claude/projects/`; o comando `/usage` do Claude Code mostra o consumo da conta.

### Tempo de desenvolvimento até cada marco

Do primeiro registro (22/10, 01:58) até cada marco. **Calendário** é o relógio corrido, com sono e pausas. **Trabalho** soma só os trechos em que alguma IA estava respondendo: uma pausa de mais de 15 min entre respostas não conta, e IAs em paralelo contam uma vez só.

| Marco | Concluído em | Calendário | Trabalho |
| --- | --- | --- | --- |
| M0–M2 ferramentas, ISO, recompilação | 23/10 02:54 | 24h55 | 4h34 |
| M3 esqueleto | 23/10 12:49 | 34h50 | 5h23 |
| M4 boot até `main` | 23/10 14:33 | 36h34 | 7h07 |
| M5 OS e disco | 23/10 23:51 | 45h52 | 10h43 |
| M6 primeiro quadro | 24/10 01:16 | 47h17 | 11h48 |
| M7 menus | 24/10 03:49 | 49h50 | 13h59 |
| M8 parte 1: tempo de CPU e GPU | 24/10 06:21 | 52h22 | 16h31 |
| M8 parte 2a: GX numa thread (meta não atingida) | 24/10 11:55 | 57h56 | 17h15 |

- Fonte: `scripts/tempo_dev.py` sobre os registros do Claude Code (port, decomp e agentes do decomp). Com limite de 10 min, o M6 fica em 10h21; com 30 min, em 13h24 (a escolha de 15 min é da sessão principal).
- Não entram o Codex e o opencode (rodaram em paralelo com o Claude, então não mudariam o total) nem o tempo do usuário fora das sessões.

## Problemas e lições

O problema mais caro não foi técnico do jogo: foi um log sem limite que encheu o disco do Mac.

| Problema | Causa | Lição |
| --- | --- | --- |
| Disco interno cheio (20,7 GB de trace) e agente travado | Primeira execução do M4 entrou em laço infinito gravando o trace dentro do repo, no disco interno | Tudo pesado vai para o SSD externo (`/Volumes/AMRecomp`); trace com teto de tamanho; watchdog no laço; toda execução com timeout |
| Laço infinito após `__OSPSInit` | O DolRecomp emite instruções de sistema (`mfspr`, `mtspr`, cache) como fallback sem avançar o `pc` | A runtime faz `pc = cia + 4`; ler a interface do gerador antes de assumir o contrato |
| Trace não vê todas as chamadas | `bl` dentro do mesmo trecho de 16 KiB vira `goto` em C | Comparar só chamadas entre trechos (71% de cobertura); para teste fino, regenerar com trechos de 128 instruções (98%) |
| Hipótese do cache no-op | Suposição do M3 sem medir | Medido no M4: os 85 `icbi` até `main` caem nos vetores de exceção; a runtime agora dá erro fatal se código do jogo mudar |
| Dolphin órfão aberto por 2h30 | Script do oráculo não fechava o Dolphin ao terminar | Scripts matam o Dolphin e o grupo de processos em qualquer saída |
| Número de frame difere do Dolphin | Tempo de leitura do disco é diferente nos dois | Comparar a sequência de eventos, não o frame exato |
| Mudança paralela com agente trabalhando | Mesma árvore e mesmas pastas de build | Testar numa `git worktree` separada e avisar o agente |
| Primeira tentativa do Codex falhou em 36 s nas três builds (`DolRecomp não encontrado`) | O prompt não dizia onde está `tools/DolRecomp`, que fica fora do git e não existe na worktree | O Codex obedeceu e não tentou consertar; prompt corrigido com o caminho (só leitura) e a segunda tentativa passou. Ao isolar em worktree, listar tudo que não está no git |
| opencode não autenticava (`Invalid API key` no 9router, 403 no provedor nvidia) | Chave do 9router inválida | Testar o modelo com uma mensagem mínima antes de lançar; usado o provedor OpenCode Go, que respondeu |
| Volume do SSD desmontado três vezes no M5 (23/10, \~16:35, \~17:30 e \~18:05); na primeira o agente morreu após 1h46, na segunda a sessão inteira parou de rodar comandos, na terceira o agente do M5 parou na seleção de pista | O macOS desanexou a imagem `AMRecomp.sparsebundle` (SSD USB). A primeira suspeita foi `disksleep 10` (repouso de disco após 10 min), mas a terceira queda veio com `disksleep 0`: a suspeita agora é a ligação USB (cabo, porta ou energia). A pasta temporária do Claude Code também ficava no volume, então nem o `hdiutil attach` rodava | Uma edição não commitada se perdeu (o `--pad-script`, reescrito depois); o resto estava na imagem. Correções: `sudo pmset -a disksleep 0`; pasta temporária do Claude Code no disco interno (`RETOMAR.md`); regra no `CLAUDE.md` de conferir `ls /Volumes/AMRecomp` antes de execuções longas. Depois da terceira: reinício do Mac e retomada por `RETOMAR.md` + página de status; o trabalho pendente virou o commit `bb1131f` |
| Leituras do disco fora de ordem no menu principal (evento 71: quadro 2092 no Dolphin, 2053 na runtime) | Tempo de leitura fixo na runtime (300 µs + 3 MB/s), enquanto o Dolphin simula o drive | Modelo do drive copiado do Dolphin (busca, rotação, velocidade pelo raio, buffer de 1 MiB) em `runtime/hw/dvd_timing.c`; os eventos de módulo passaram a cair no mesmo quadro |
| Telas de “now loading” lendo arquivos diferentes nos dois lados | O Dolphin usava o relógio do Mac, e a runtime começa em 2000-01-01; provavelmente o jogo sorteia algo pelo relógio (não confirmado) | O oráculo fixa o relógio (`--rtc`); as leituras ficaram iguais e na mesma ordem. Fixar toda entrada externa do oráculo, não só o controle |
| `OSUnlink` do `menu01` recebia o endereço do `menu00` | `menu.rel` e `game.rel` usam o mesmo endereço real, um depois do outro, e a runtime não tirava o módulo descarregado da tradução real → virtual | Corrigido com teste de regressão (`c2d96c8`); um salto para o `game.rel` cairia no código do menu |
| Leituras de música ADX não batem byte a byte (a 1ª diferença no quadro 2121) | Cada leitura repõe o que já tocou, em setores de 2 KiB, e isso depende da fração do quadro em que a CPU olha; a runtime não tem o ciclo de CPU do Dolphin | Comparação em dois níveis: estrita para módulos e arquivos; para a música, mesma contagem de leituras por trecho |
| O mesmo C desenhava pixels diferentes em arm64 e x86\_64 (M6) | O compilador funde `a*b+c` numa instrução só (FMA) onde a arquitetura tem, e o arredondamento muda | O GX compila sem contração de ponto flutuante; agora as capturas dos dois builds são idênticas e o golden guarda o hash das duas |
| A cópia para o XFB não batia com o Dolphin (M6) | O backend Software do Dolphin arredonda e faz a média de U/V de um jeito; o oráculo roda o backend de hardware (Metal), com outro shader | A runtime segue o shader do caminho de hardware, porque é ele que o oráculo executa: comparar com o que o oráculo de fato roda |
| Desenhar a rota inteira do M5 levou 9 min 31 s, contra \~57 s sem desenhar (M6) | O GX em software rasteriza cada triângulo na CPU | Os 551 eventos continuam iguais aos do Dolphin (desenhar não mexe no relógio do jogo); o `verify_m5.sh` passou a rodar com `--gfx off` |
| Os 551 eventos batiam com o Dolphin, mas a câmera da largada ficava noutra posição (M6–M7) | Nos carregamentos, o trabalho da CPU entre uma leitura e outra dura até 1,9x mais no Dolphin; no quadro 6400 o tempo emulado do Dolphin está 1,37 s (82 campos de vídeo) adiante, e a câmera anda pelo relógio, não pelos quadros | Medido com `--timebase-out` nos dois lados; não era o áudio. Corrigido no M8 (veja a linha seguinte). Lição: animações que andam pelo relógio denunciam diferença de tempo que a contagem de eventos não vê |
| O relógio do jogo nativo ficava 1,37 s atrás do Dolphin nos carregamentos (M8) | O DolRecomp manda algumas instruções para um interpretador e dava 0 ciclos a elas: o laço que limpa o cache antes de cada leitura do disco contava 2 ciclos por linha de memória em vez de 7. Menores: o Dolphin roda o apploader antes do jogo (meio campo de vídeo de diferença no começo) e o vetor de exceção feito na runtime não contava ciclos | Contar os ciclos dessas instruções pela tabela do próprio Dolphin levou de 82 para 2,5 campos; o resto, para 2,1. Lição: o que custa zero se esconde, e o oráculo também tem jeito de medir: o “timebase” que o Dolphin mostra é o da última leitura do jogo, não o tempo atual |
| Tirar o desenho da CPU revelou um cache de texturas que decodificava 87 mil vezes as mesmas texturas até a largada (M8) | Uma escrita de outro dado na mesma página de 4 KiB invalidava a textura | Um hash do conteúdo antes de decodificar de novo: 264 decodificações. Lição: o gargalo muda de lugar quando o maior sai |
| Pôr o desenho numa thread própria não acelerou a corrida (M8 parte 2a) | A cada quadro o jogo manda quase tudo em display lists, baratas para a CPU, e logo pede o DrawDone e espera o desenho acabar: a CPU emulada fica parada esperando e sobra pouca sobreposição | Medir onde a thread espera antes de otimizar. Ganhar exige não esperar no DrawDone, e aí a CPU precisa copiar o que o GX vai ler (como o Dolphin faz): fica para a parte 2b |
| O decodificador de áudio ADPCM saturava em -32767 em vez de -32768 (M7) | Limite errado no código da runtime, invisível até o som ser comparado com o do Dolphin | Corrigido; o áudio passou a bater amostra por amostra |
| Mais duas quedas do SSD no fechamento do M7 (24/10, \~04:00 e \~04:05); a imagem não remontava (“sistemas de arquivos não montáveis”) e a sessão principal ficou sem rodar comandos | O SSD saiu da USB de novo; a imagem `AMRecomp` ficou presa a dois dispositivos antigos das quedas, e o mesmo volume não monta duas vezes. A sessão foi aberta sem o `CLAUDE_CODE_TMPDIR` do `RETOMAR.md`, então a pasta temporária estava no volume | O usuário religou o cabo e soltou os dispositivos presos pelo Terminal; nada se perdeu (os roteiros estavam na imagem e foram commitados logo em seguida). O `am-montar` agora solta sozinho os dispositivos presos e mostra o erro do `hdiutil` (`2f313aa`). Lição: abrir as sessões sempre com a linha do `RETOMAR.md` |
| Cada queda do SSD exigia remontar à mão | A imagem fica num SSD exFAT ligado por USB; quando o dispositivo some (`media is not present` no kernel), nada remonta sozinho | `~/.local/bin/am-montar` (no disco interno) + agente do launchd a cada 60 s (`c19caf3`); testado desmontando o DecompAM |

Padrões de trabalho que funcionaram:

- **Critério de saída verificável por marco**, conferido de novo pela sessão principal depois do agente.
- **Commits pequenos por marco**, sem bytes do jogo, conferidos antes de cada commit.
- **Memória entre sessões** no ai-memory: decisão de arquitetura fixada e página de status atualizada a cada etapa.

**Ferramentas de engenharia reversa (23/10):** Ghidra 12.1.4 + JDK 21 (Temurin 21.0.13) instalados no SSD em `/Volumes/AMRecomp/ext`, e REA 6.7.0 no notebook, registrado como MCP no Claude Code (só entra em uso após reiniciar a sessão).

- O REA só aceita Ghidra 12.1.x; a 12.2, lançada em 22/10, ainda não.
- O Ghidra oficial não traz o decompilador nativo `mac_arm_64`: foi compilado com `support/gradle/gradlew buildNatives` (36 s).
- O `main.elf` do dtk vem com `EF_PPC_EMB` (0x80000000) e o REA recusa; uma cópia com `e_flags = 0` em `/Volumes/AMRecomp/work/rea/main.elf` funciona: 3.897 funções, 9.902 strings, perfil `PowerPC:BE:32:default`.
- O Ghidra padrão não tem perfil Gekko. Resolvido com a extensão GameCubeLoader 1.3.1 (Apache-2.0): linguagem `PowerPC:BE:32:Gekko_Broadway`, `.sla` compilado com `support/sleigh`, fixada no MCP via `REA_GHIDRA_LANGUAGE_ID`. Resultado: 3.897 funções, 9.905 strings.

**Primeiro uso real do REA (M5, 23/10):** o agente do M5 usou o REA sobre o `main.elf` para achar o caminho até o `game.rel`, consultando strings, referências cruzadas e pseudocódigo. Achados até aqui: a função que pré-carrega os módulos na memória de áudio (ARAM) no começo (`game`, `menu`, `menu00`, `menu01`, `vj` e 7 `msg_*`), a função que liga os módulos (`OSLink`) e as 6 funções de troca de módulo (o `game.rel` entra no espaço 1, o `menu.rel` no 2). De quebra, corrigiu um endereço do projeto: o que estava anotado como `VISetNextFrameBuffer` era `VIFlush`.

## Perguntas em aberto e próximos passos

O M8 está pela metade. A parte 1 (24/10, 06:21) igualou o tempo de CPU ao do Dolphin até a carga da pista e levou o desenho para a GPU. A parte 2a (11:55) pôs o GX numa thread própria, determinística, mas sem ganho: a CPU espera o desenho no DrawDone. A parte 2 ficou dividida; falta a 2b: não esperar no DrawDone para desenhar a corrida a 60 quadros/s (hoje 31), o cel-shading conferido com o carro andando, os 2 campos de tempo que sobram e o save no memory card.

- [x] M5: carregar `game.rel` e rodar 600 frames do laço principal em arm64 e x86\_64 (concluído em 23/10, 23:51)
- [x] M6: FIFO da GX, vertex loader, texturas, TEV; logo da Capcom na tela (concluído em 24/10, 01:16, com GX em software em vez de ubershader)
- [x] M7: menus jogáveis com controle, áudio (AX) e vídeos (Sofdec) (concluído em 24/10, 03:49; imagem dos menus a 12–17 quadros/s até a GPU do M8)
- [ ] M8: corrida completa a 60 fps, cel-shading correto, saves (parte 1 concluída em 24/10, 06:21: tempo de CPU e GPU; corrida a 31 quadros/s desenhados)
- [ ] M9: builds de Windows e Linux x86\_64 testadas de verdade
- [ ] M10: widescreen, resolução interna, 60 fps, troca por funções decompiladas

Perguntas ainda sem resposta:

- ~~Qual microcódigo de DSP o jogo carrega, e se é o AX padrão~~ Respondida no M7: é o AX (microcódigo 0x07F88145, 7.968 bytes); o HLE do AX na runtime toca o mesmo som que o Dolphin.
- ~~Como o jogo toca os vídeos~~ Respondida no M7: SFD (MPEG-1 com áudio ADX) dentro dos `.afs`, decodificados pela biblioteca da CRI que já vem dentro do executável do jogo; o quadro vira textura do GX. Não precisou de código novo.
- ~~Se o jogo usa streaming de áudio direto do disco~~ Respondida no M5: sim, a música é ADX lida em fluxo de dentro dos `.afs` (441 leituras de 10 arquivos até a largada).
- Como o cel-shading usa as cópias de EFB e o z-texture, o maior risco do M6–M8. Parcial no M6: até a largada, o jogo não copia o EFB para textura nem usa textura de Z ou TEV indireto; usa neblina em 92% dos desenhos e no máximo 3 estágios de TEV, e descarta 1,6 milhão de triângulos pela face. Hipótese: o contorno é geometria (o modelo repetido um pouco maior, com a frente descartada). Falta confirmar com o carro em movimento, no replay e na garagem.
- Por que a câmera da introdução da corrida fica em outra posição na runtime (20,9 dB contra o Dolphin, na largada em Suzuka). Respondida no M7: não é o áudio; o Dolphin gasta mais tempo de CPU nos carregamentos e chega à largada 1,37 s adiante no tempo emulado. Corrigido no M8 parte 1: a diferença caiu para 0,035 s e a câmera da largada foi de 20,9 para 29,4 dB contra o Dolphin.
- Se o modo LAN (Broadband Adapter) existe mesmo na versão GameCube.
- O CI de Linux e Windows ainda não rodou: falta um remote no GitHub.
