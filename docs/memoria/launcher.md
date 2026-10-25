# Launcher (decisão de 25/10/2026)

O usuário escolheu o **estilo B: tela inicial com arte**: arte do disco do próprio usuário (banner `files/opening.bnr`,
lido em tempo de execução), menu JOGAR / Configurações / Controles / Memory card / Sair, navegável por controle,
rodapé com backend · GPU · escala. Prioridades (todas pedidas): contador de FPS (F3); volume geral, tela cheia
(F11/Alt+Enter) e VSync; remapear teclado e gamepad e escolher o controle da porta 1; escolher a pasta do jogo
(conferindo o SHA-256 do `main.dol`) e, depois, extrair o `.rvz` direto.
Feito na parte 5 do M9 (`docs/M9_PARTE5_LAUNCHER.md`), com Dear ImGui 1.92.9 por FetchContent.
Fica para depois: extrair `.rvz`, volumes separados de música e efeitos, o logo grande da tela de título
(está nos `.afs`, formato da Capcom), remapear os sticks analógicos.
