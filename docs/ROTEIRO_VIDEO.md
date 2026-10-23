# Roteiro de vídeo: recompilando Auto Modellista para PC

23/10/2026 · Guilherme · Espelho de [Roteiro de vídeo (doc)](https://claude.ai/artifact/6nuBK9gyqKM3wP5tHJhBpL)

## Ficha do vídeo

Vídeo horizontal de bastidores, em primeira pessoa, contando como um jogo de GameCube está virando um programa nativo de PC com ajuda de IA.

| Item | Valor |
| --- | --- |
| Formato | Talking head + b-roll (gameplay no Dolphin, telas de terminal, diagramas) |
| Duração estimada | 15–20 min no vídeo completo; um Short de 60–90 s com o gancho e o “main alcançado” |
| Tom | Didático e honesto: mostra o que deu certo e o que deu errado |
| Público | Quem curte jogos antigos e tecnologia, sem exigir saber programar |
| Cuidados | Não mostrar código do jogo nem bytes da ISO; gameplay do Dolphin em trechos curtos; citar que o decomp.wtf foi descontinuado e o motivo |

**Como este roteiro é atualizado:** a cada marco ou descoberta relevante do projeto, entra uma cena nova no capítulo certo e uma linha no registro do fim. Cada cena tem **Fala** (o que você diz), **Na tela** (o que mostrar, descrito para gravar ou montar) e **Motion** (sugestão de animação ou transição para aquele momento).

## Gancho (0:00–0:45)

**Fala:** “Esse é Auto Modellista, um jogo de corrida da Capcom de 2002, com cara de desenho animado. Ele nunca saiu pra PC. A pergunta é: dá pra pegar o código de GameCube e transformar num programa que roda nativo no computador, sem emulador? Spoiler: essa semana o jogo ligou pela primeira vez fora do console. Sem imagem ainda, mas ligou. E quem fez boa parte do trabalho foi IA.”

**Na tela:**

- Abertura do jogo rodando no Dolphin (o mosaico de carros amarelo).
- Corte seco para o terminal: `[OSReport] Dolphin OS … Console Type : Development HW3` e `M4: main alcançado`.
- Texto na tela: “GameCube → C → PC”.

**Motion:**

- 0:00–0:08: abertura em tela cheia com zoom lento (Ken Burns) no mosaico de carros, música de fundo baixa.
- No “sem emulador?”: a imagem racha em blocos de pixel e cai, revelando o terminal preto (transição “glitch” curta, 6 frames).
- As linhas do `OSReport` aparecem digitando, uma por vez; `main alcançado` acende em verde com um “pulse”.
- Card de título: “GameCube → C → PC”, cada palavra entra da esquerda com seta desenhando entre elas.

## Cap. 1 — O jogo e o ponto de partida

**Fala:** “Eu já vinha brincando com o decomp.wtf, uma ferramenta que põe agentes de IA para reescrever as funções de um jogo em C, uma por uma, até o compilador original gerar exatamente os mesmos bytes. Chegamos a 3% do jogo. Só que tem dois problemas. Primeiro: mesmo com 100% decompilado, o resultado ainda é um jogo de GameCube. Segundo: o autor descontinuou a ferramenta, porque a comunidade de decompilação é contra fazer isso com IA. Então eu precisava de outro caminho.”

**Na tela:**

- Painel do decomp.wtf: “3,04% of code · 933 / 3.903 functions”.
- Print da página de descontinuação do decomp.wtf, com o trecho sobre a comunidade.
- Gráfico simples: 3% hoje → 100% “muito tempo”.

**Motion:**

- Gravação de tela do painel do decomp.wtf com zoom no contador “3,04%” e um círculo desenhando em volta.
- Barra de progresso animada: enche até 3% e trava; um texto “100% = muito tempo” aparece no fim da barra vazia.
- Print da página de descontinuação em moldura no canto, com a frase sobre a comunidade sublinhada por um marca-texto animado.
- Card do jogo (capa + “Auto Modellista · Capcom · 2002”) no canto superior durante o capítulo.

## Cap. 2 — Como funciona: três jeitos de levar um jogo para o PC

**Fala:** “Existem três jeitos de fazer um jogo de console rodar no PC. O primeiro você conhece: emulador. O Dolphin finge ser um GameCube e traduz o código enquanto o jogo roda. Funciona muito bem, mas o jogo continua sendo de GameCube. O segundo é a decompilação: reescrever o jogo em C até bater byte a byte com o original. Ótimo pra entender e modificar, mas o resultado volta a ser um arquivo de GameCube. O terceiro é o que estamos fazendo: recompilação estática. Uma ferramenta pega todo o código PowerPC do jogo e traduz pra C de uma vez só, antes de rodar. Aí eu compilo esse C pro meu processador. Só que esse código ainda acha que está num GameCube, então eu preciso escrever um ‘GameCube falso’ em C, a runtime, que responde no lugar do hardware.”

![Três caminhos do código do GameCube; só a recompilação vira nativo](img/tres_caminhos.png)

**Na tela:**

- Este diagrama, animado caminho por caminho; o terceiro acende por último.
- Analogia visual: emulação = intérprete ao vivo; recompilação = livro traduzido inteiro antes de ler.
- Detalhe para quem gosta: o PowerPC guarda os números “de trás pra frente” em relação ao PC (big-endian), e a runtime cuida disso em um lugar só.

**Motion:**

- O diagrama monta em três tempos, sincronizado com a fala: caixa “main.dol + RELs” surge; a cada caminho citado, a seta desenha da esquerda para a direita e as duas caixas da linha entram com fade + leve subida.
- Emulação e decompilação ficam acinzentadas depois de explicadas; na recompilação, a caixa final acende em azul com brilho suave.
- Analogia: split screen; à esquerda um balão de intérprete traduzindo frase a frase em tempo real; à direita um livro inteiro sendo “impresso” de uma vez (páginas folheando rápido).
- Big-endian: os bytes `12 34 56 78` em quatro blocos que giram e trocam de ordem para `78 56 34 12`, com rótulos “GameCube” e “PC”.

## Cap. 3 — As ferramentas e como funcionam

Uma cena por ferramenta. As de Ghidra e REA ficam marcadas “a gravar” até instalarmos e usarmos de verdade.

### 3.1 DolRecomp: o tradutor

**Fala:** “O DolRecomp lê o executável do GameCube, o `main.dol`, e os 12 módulos extras, os RELs. Cada instrução PowerPC vira uma linha de C. Foram 341 mil instruções no DOL e 255 mil nos RELs, nenhuma desconhecida. Deu 84 MB de C que ninguém vai ler, mas o compilador lê em 30 segundos.”

**Na tela:** uma instrução PowerPC ao lado da linha de C equivalente (exemplo genérico, não código do jogo); contador “0 instruções desconhecidas”.

**Motion:** a instrução PowerPC fica à esquerda; uma seta “DolRecomp” varre a tela e a linha de C aparece à direita. Depois, um “chuva” rápida de linhas de C borradas (para não mostrar código do jogo) com o contador subindo até “341.000 + 255.000 instruções” e o selo “0 desconhecidas” batendo como carimbo.

### 3.2 A runtime: o GameCube falso

**Fala:** “O C gerado acha que está num GameCube. Quando ele escreve num endereço que no console seria o chip de vídeo, alguém precisa responder. Esse alguém é a runtime, que eu estou escrevendo em C puro, com uma única biblioteca, a SDL3, pra janela, som e controle. E tem uma regra: se aparecer algo que a runtime não conhece, o programa para e diz exatamente o quê. Nada de emulador escondido.”

**Na tela:** a mensagem `[FATAL] bus: leitura de 4 byte(s) em 0xCC002000 (VI)` como exemplo de “parar e dizer o quê”; diagrama de camadas: C gerado → runtime → SDL3 → sistema.

**Motion:** pilha de camadas montando de baixo para cima (Sistema → SDL3 → runtime → C gerado), cada uma deslizando no lugar. Uma “bolinha” de pedido desce da camada do jogo até a runtime e volta com a resposta. Na mensagem `[FATAL]`, a tela dá um tremor curto e o endereço `0xCC002000 (VI)` ganha destaque em vermelho.

### 3.3 Dolphin como gabarito

**Fala:** “E como eu sei que está certo, se ainda nem tem imagem? Uso o Dolphin como gabarito. Eu rodo o jogo original no Dolphin com milhares de pontos de parada e anoto cada função que ele chama. A minha versão anota a mesma coisa. Um script compara as duas listas. Até a função `main`, foram 1.998 eventos idênticos.”

**Na tela:** dois terminais lado a lado (Dolphin x runtime) rolando a lista; o veredito `IGUAIS: 1998 eventos normalizados idênticos; os dois terminam em main: True`.

**Motion:** dois terminais lado a lado rolando em sincronia, com linhas iguais piscando em verde nos dois lados ao mesmo tempo. Um contador central sobe até 1.998. No fim, um “=” grande aparece entre os terminais e o veredito `IGUAIS` entra em destaque.

### 3.4 Ghidra (a gravar)

**Fala (rascunho):** “Até aqui eu não precisei entender o jogo, só traduzir. Daqui pra frente preciso: como ele desenha o cel-shading, como toca o áudio. Pra isso entra o Ghidra, a ferramenta de engenharia reversa da NSA, que mostra o assembly, um pseudocódigo em C e quem chama quem.”

**Na tela (planejado):** o Ghidra aberto no `main.elf` do decomp, com as funções já nomeadas.

**Motion (planejado):** gravação de tela do Ghidra com zoom guiado: lista de funções → clique numa função → painel de assembly e painel de pseudocódigo lado a lado, com um realce percorrendo a linha correspondente nos dois. Borrar o código do jogo; manter visíveis só nomes e estrutura.

### 3.5 REA: a IA dentro do Ghidra (a gravar)

**Fala (rascunho):** “O REA é um MCP, um conector que deixa a IA usar o Ghidra sozinha. Eu pergunto ‘onde o jogo monta o cel-shading?’, e ela abre o binário, procura, lê o pseudocódigo, segue as referências e me devolve a resposta com a evidência: qual função, qual endereço, o que ainda é incerto. Um detalhe: ele ainda não lê o formato de GameCube direto, só ELF, então eu dou pra ele o ELF que o projeto do decomp já gera.”

**Na tela (planejado):** o fluxo pergunta → REA → Ghidra → resposta com evidência; a primeira consulta real que fizermos no M6.

**Motion (planejado):** fluxo em quatro caixas (Você pergunta → IA → REA → Ghidra) com um pacote de luz indo e voltando entre elas; na volta, a resposta abre como um cartão com três campos: “função”, “evidência”, “incerto”. Selo “só ELF por enquanto” aparece no canto quando a fala cita o limite.

### 3.6 Instalando Ghidra e REA: dois tropeços (gravado em 23/10)

**Fala:** “Instalar não foi só ‘next, next’. Primeiro: o Ghidra oficial não vem com o decompilador nativo pra Mac com chip Apple, só pra Linux e Windows. Tive que compilar essa peça, levou 36 segundos. Segundo: o REA recusou o arquivo do jogo, porque ele vinha marcado como ‘PowerPC embarcado’, uma variante diferente. Essa marcação é só um rótulo no cabeçalho, então fiz uma cópia sem ela. Aí funcionou: o Ghidra achou 3.897 funções e quase 10 mil textos dentro do jogo. E ainda tem um porém: o Ghidra não conhece as instruções especiais do processador do GameCube, o Gekko. Pra isso existe uma extensão da comunidade que ensina o Gekko pro Ghidra. Instalei, e as instruções de matemática vetorial do GameCube passaram a ser lidas.”

**Na tela:**

- A pasta do Ghidra mostrando só `linux_x86_64` e `win_x86_64` em `Decompiler/os/`; depois o `BUILD SUCCESSFUL in 36s` e o arquivo `mac_arm_64/decompile`.
- A mensagem do REA: “EF_PPC_EMB declares an embedded PowerPC variant”.
- O byte do cabeçalho mudando de `0x80000000` para `0x00000000`.
- O resultado: `procedure_count = 3897`, `string_count = 9902`.
- Depois: a extensão GameCubeLoader na pasta de extensões e o perfil `PowerPC:BE:32:Gekko_Broadway` no resultado do REA.

**Motion:** a pasta do Ghidra abre e as duas plataformas aparecem; um espaço vazio pisca com “mac_arm_64?” e depois é preenchido com uma engrenagem girando (compilando) e um check. Para o rótulo: o cabeçalho do arquivo como uma etiqueta de bagagem com “EMB” carimbado; uma tesoura corta o carimbo e o arquivo passa pela catraca do REA. Contadores sobem até 3.897 e 9.902.

## Cap. 4 — Bastidores: como a IA trabalhou

### 4.1 Um time de IAs, cada uma no seu tamanho

**Fala:** “Eu não usei uma IA só. Pensei como num time. O Opus, o modelo mais forte, ficou com as partes em que é preciso pensar e investigar: o boot do jogo e o sistema operacional do GameCube. O Sonnet ficou com o trabalho grande mas bem especificado: montar o esqueleto, a build, os testes. E o Haiku, o mais rápido e barato, é o candidato pro trabalho repetitivo que tem um corretor automático, tipo casar funções no decomp, onde a ferramenta diz na hora se acertou.”

**Na tela:** três cartões (Opus, Sonnet, Haiku), cada um com as tarefas que pegou; abaixo, a lista de commits do projeto (`git log --oneline`) com os marcos M3, M4 e M5.

**Motion:** os três cartões entram como cartas sendo distribuídas na mesa; cada tarefa citada voa do centro para o cartão certo. Os commits sobem como uma timeline vertical, um por vez, com o nome do marco acendendo.

### 4.2 M3: o jogo inteiro num executável só

**Fala:** “Primeiro passo: juntar os 84 MB de C do jogo e dos módulos num programa só. 155 pedaços, nenhum conflito. O programa ainda não faz nada, mas ele existe, roda no Mac M4 e no modo x86_64 via Rosetta, que é o processador de PC.”

**Na tela:** terminal com `C gerado: ligado; 155 trecho(s) no dispatcher` e `M3: esqueleto OK`; o comando `file` mostrando “arm64” e depois “x86_64”.

**Motion:** 155 blocos pequenos caindo como Tetris e encaixando num retângulo único com o nome `automodellista`; depois o retângulo “vira” (flip) mostrando o selo arm64 de um lado e x86_64 do outro.

### 4.3 M4: o jogo liga

**Fala:** “Aí veio o momento mais legal até agora. O código do próprio jogo, traduzido, inicializou o sistema do GameCube e imprimiu a mensagem que só aparecia em kit de desenvolvimento: Dolphin OS, Console Type: Development HW3, Memory 24 MB. E chegou na função `main` fazendo exatamente as mesmas chamadas que o jogo original no Dolphin.”

**Na tela:** o log completo do `OSReport` até `M4: main alcançado`; em seguida o comparador com `IGUAIS: 1998 eventos`.

**Motion:** o log digita linha a linha com som de teclado sutil; “Console Type : Development HW3” ganha zoom e um rótulo explicativo (“kit de desenvolvimento da Nintendo”); no `main alcançado`, confete discreto ou um “ding” visual.

### 4.4 O dia em que o disco encheu

**Fala:** “E nem tudo deu certo. Na primeira vez que a IA rodou o boot, o programa entrou num loop infinito e ficou escrevendo um log sem parar. Foram 20 GB, o disco do Mac lotou e até a própria IA travou, porque não conseguia nem rodar comando. A causa era um detalhe: o tradutor deixa para a runtime avançar o contador de programa depois de certas instruções, e a runtime não fazia isso. Então ele executava a mesma instrução pra sempre. Lição: tudo pesado foi pro SSD externo, o log ganhou limite de tamanho e o programa ganhou um ‘cão de guarda’ que mata o loop.”

**Na tela:** o `df -h` com 1,4 GB livres; o arquivo `runtime_trace.txt` de 20,7 GB; depois o `df -h` com 21 GB livres e a pasta no SSD.

**Motion:** um medidor de disco enchendo do verde ao vermelho em acelerado, com o tamanho do arquivo subindo em contador; a tela “trava” por meio segundo (freeze frame com ruído). Depois o arquivo é arrastado para uma lixeira e o medidor despenca. Para explicar o bug: uma seta de “pc” presa batendo na mesma instrução em loop, e então um “+4” empurrando a seta para a próxima linha.

### 4.5 M5: o jogo lê o disco como o original

**Fala:** “No M5 o jogo passou do boot. Ele começou a ler arquivos do disco e carregar os módulos de menu. E o mais bonito: ele pede exatamente os mesmos arquivos, na mesma ordem e nos mesmos lugares do disco que o jogo original no Dolphin. Só não no mesmo frame, porque cada um lê o disco numa velocidade diferente.”

**Na tela:** as duas listas de eventos lado a lado (`di_read` … `oslink id=2` …) com as linhas iguais alinhadas; o print da abertura que o Dolphin mostra no frame 2.000.

**Motion:** as duas listas rolam e linhas correspondentes se conectam por fios entre os lados; os números de frame diferentes piscam em amarelo com o rótulo “tempo de disco, esperado”. Os nomes `menu.rel`, `menu00`, `msg_e` saem da lista e encaixam numa “memória” desenhada.

## Cap. 5 — Onde estamos e o que vem

**Fala:** “Hoje o Auto Modellista liga no meu computador sem emulador, inicializa o sistema, lê o disco e carrega os menus. Só que tela, ainda nada: o próximo passo é ensinar a runtime a desenhar, traduzindo o chip gráfico do GameCube pra Metal, Vulkan e DirectX. É a parte mais difícil, porque o visual de desenho animado do jogo depende de truques de hardware. Quando o logo da Capcom aparecer pela primeira vez na janela, sai o próximo vídeo. Se você quer ver isso, já sabe.”

**Na tela:**

- Timeline dos marcos M3 a M10 com os concluídos marcados e o M6 piscando como “próximo”.
- Lado a lado: janela preta da runtime x abertura no Dolphin, com a legenda “por enquanto”.
- Encerramento com o mosaico de carros e o nome do canal.

**Motion:**

- Timeline horizontal desenhando da esquerda para a direita; os marcos concluídos ganham um check verde ao passar; no M6 a linha para e um cursor pisca.
- Na frase “quando o logo aparecer”: a janela preta acende num flash branco curto e corta para o card “próximo vídeo”.
- Card final com o handle do canal e um botão de inscrição animado discreto.

## Registro de atualizações

| Data | O que entrou no roteiro |
| --- | --- |
| 23/10/2026 | Versão inicial: gancho, capítulos 1–5, cenas de Ghidra e REA como “a gravar”; Motion em todas as cenas; 3.6 instalação do Ghidra 12.1.4 e do REA 6.7.0 (decompilador ARM64 compilado, flag EF_PPC_EMB, 3.897 funções) |
