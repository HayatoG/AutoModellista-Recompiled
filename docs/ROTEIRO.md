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
| Arquitetura, depuração contra o Dolphin, revisão | Opus | M4 (boot), M5 (OS e disco) |
| Implementação com especificação clara | Sonnet | M3 (esqueleto e build) |
| Tarefas mecânicas com verificador automático | Haiku | Matching de funções no decomp (o objdiff dá o veredito) |
| Verificação mecânica em paralelo, fora da cota do Claude | Codex CLI (GPT-6 Luna, raciocínio baixo) | Builds x86\_64/Debug com a SDL nova (M5) |
| Matching do decomp em paralelo, fora da cota do Claude | opencode CLI (DeepSeek V4.1 Flash via OpenCode Go) | Funções do decomp num branch próprio (M5) |

**Agentes de outras ferramentas (23/10, M5):** para não gastar a cota semanal do Claude em tarefas mecânicas, a sessão principal passou duas tarefas para o Codex CLI e o opencode CLI, rodando em segundo plano. Regras de segurança: cada um numa `git worktree` própria (o repo principal e as builds do agente do M5 ficam intocados); o Codex no sandbox `workspace-write` sem rede (a SDL vem de uma cópia local); o opencode com `opencode.json` que nega qualquer comando fora de uma lista (sem `rm`, `push`, `reset`, `clean`, troca de branch); a cópia do jogo fica fora do git e proibida no prompt; tudo com `with_timeout.sh`; a sessão principal confere o resultado antes de aceitar.

## Linha do tempo

Em 23/10/2026, o port foi do plano até o jogo rodando nativo além de `main`, carregando menus do disco do mesmo jeito que o original. Mais recente primeiro:

| Data | Marco | Modelo | Resultado verificado | Commits |
| --- | --- | --- | --- | --- |
| 23/10 | M5 OS e disco (em andamento; parte 2 desde 17:11, retomado à noite) | Opus | Leituras de disco na mesma ordem e offsets do Dolphin; `menu.rel`, `menu00`, `menu01` e `msg_e` carregados via `OSLink`; RELs regenerados com base em 0x81800000. Parte 2: vozes do AX andando sem mixagem (teste novo com vozes sintéticas), detector de laço de quadros parado; 38/38 testes; 600 quadros em 11,4 s sem erro e 31 eventos de disco/módulos iguais ao Dolphin. Oráculo com roteiro de controle (`--pad-script` no pipe do Dolphin) e capturas da tela (`--shots`); caminho pelos menus achado até a seleção de pista | `4cd34e0`, `73cdf41`, `456174f`, `557e0f1`, `501e2cc`, `73d7e10`, `bb1131f` |
| 23/10 | Builds x86\_64 Release, x86\_64 Debug e arm64 Debug com SDL 3.4.18 | Codex (GPT-6 Luna) | Três builds compilam, 38/38 testes em cada; verificado de novo pela sessão principal | (sem commit: só verificação) |
| 23/10 | SDL3 3.4.14 → 3.4.18 | (sessão principal) | 26/26 testes; boot idêntico (41.422 voltas, 6.420 chamadas) | `09deea0` |
| 23/10 | M4 boot até `main` | Opus | Trace idêntico ao Dolphin em 1.998 eventos (2.733 com trechos de 128); arm64 e x86\_64; 26 testes | `852ba6d`, `7614f88`, `64a09b6` |
| 23/10 | M3 esqueleto | Sonnet | DOL + 12 RELs linkados num executável (155 trechos); 12 testes; CI escrito | `f3dd0ac` |
| 23/10 | Plano do port em C e padrões de código | (sessão principal) | `PLANO_PORT_C.md`, `PADROES_DE_CODIGO.md`, configs de lint | `f3dd0ac` |
| antes de 23/10 | M0–M2 ferramentas, ISO, recompilação | (sessões anteriores) | ISO bate com o Redump; 0 instruções desconhecidas; todo o C compila | `f3dd0ac` |

O que cada marco provou, em uma linha:

- **M3:** o C gerado de todo o jogo cabe num executável só, sem conflito de símbolos entre DOL e RELs.
- **M4:** o próprio código do jogo, recompilado, inicializa o SDK e imprime “Dolphin OS … Console Type: Development HW3”, como no console.
- **M5 (até aqui):** o jogo nativo pede os mesmos arquivos ao disco que o original e carrega os módulos de menu.

### Tokens e tempo por etapa

Até 23/10 às 16:36 (horário de Brasília), o projeto processou cerca de 389 milhões de tokens, dos quais 2,1 milhões foram texto gerado pelos modelos. Somado dos registros de cada sessão do Claude Code, contando cada resposta uma vez.

| Etapa | Modelo | Início → fim | Duração | Respostas | Tokens gerados | Tokens processados |
| --- | --- | --- | --- | --- | --- | --- |
| opencode: matching do decomp (em andamento, parcial) | DeepSeek V4.1 Flash (OpenCode Go) | 23/10 17:44 → 17:59+ | 15 min+ | 77 | 8.679 | 3,3 M |
| Codex: builds x86\_64/Debug com SDL 3.4.18 (2 tentativas) | GPT-6 Luna | 23/10 17:44 → 17:57 | 12 min | — | 14.602 | 5,5 M |
| M5 OS e disco, parte 2 (em andamento, parcial) | Opus 5.5 | 23/10 17:11 → 17:57+ | 46 min+ | 104 | 49.950 | 17,9 M |
| Sessão principal, retomada após a queda do volume | Opus 5.5 | 23/10 17:08 → 17:59+ | 51 min+ | 64 | 39.013 | 8,1 M |
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

O próximo marco visível é o M6: a primeira imagem do jogo na tela, desenhada pela runtime.

- [ ] M5: carregar `game.rel` e rodar 600 frames do laço principal em arm64 e x86\_64
- [ ] M6: FIFO da GX, vertex loader, texturas, ubershader TEV; logo da Capcom na tela
- [ ] M7: menus jogáveis com controle, áudio (AX) e vídeos (Sofdec)
- [ ] M8: corrida completa a 60 fps, cel-shading correto, saves
- [ ] M9: builds de Windows e Linux x86\_64 testadas de verdade
- [ ] M10: widescreen, resolução interna, 60 fps, troca por funções decompiladas

Perguntas ainda sem resposta:

- Qual microcódigo de DSP o jogo carrega, e se é o AX padrão.
- Se o jogo usa streaming de áudio direto do disco (o byte 0x08 do cabeçalho está ligado).
- Como o cel-shading usa as cópias de EFB e o z-texture, o maior risco do M6–M8.
- Se o modo LAN (Broadband Adapter) existe mesmo na versão GameCube.
- O CI de Linux e Windows ainda não rodou: falta um remote no GitHub.
