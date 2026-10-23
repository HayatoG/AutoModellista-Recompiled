# Instruções para agentes: Auto Modellista Recompiled

Port nativo de Auto Modellista (GameCube, GAUE08) por recompilação estática (DolRecomp) + runtime própria em C17/SDL3.
Leia antes de trabalhar: `docs/PLANO_PORT_C.md` (plano e marcos), `docs/PADROES_DE_CODIGO.md` (regras de código) e
o `docs/M*_*.md` do último marco.

## Regra: manter os roteiros iguais ao projeto real

A cada etapa relevante (marco concluído ou interrompido, decisão de arquitetura, bug ou lição que vale contar,
ferramenta nova em uso, como Ghidra e REA), a sessão principal atualiza **todos** estes registros antes de encerrar
o turno:

| Registro | Onde | Como |
| --- | --- | --- |
| Roteiro de estudo | doc https://claude.ai/artifact/6NQA8T4JE8fNcTG2PGE2kT e `docs/ROTEIRO.md` | linha do tempo, decisões, problemas e lições, próximos passos |
| Roteiro do vídeo | doc https://claude.ai/artifact/6nuBK9gyqKM3wP5tHJhBpL e `docs/ROTEIRO_VIDEO.md` (imagens em `docs/img/`) | cena nova no capítulo certo com **Fala**, **Na tela** (o que mostrar, descrito) e **Motion** (animação sugerida); linha no "Registro de atualizações" |
| Status | ai-memory `notes/auto-modellista-port-status.md` | marcos, commits, lições |

- O roteiro descreve o que **aconteceu de fato**: números, mensagens e commits reais, conferidos. Cena planejada fica
  marcada "a gravar" até ser verdade; quando a realidade mudar, a cena muda junto.
- Em cada etapa, registrar **tokens e tempo** (modelo, início → fim, respostas, tokens gerados e processados) nos dois
  roteiros, com `python3 -I scripts/usage_report.py "$HOME/.claude/projects/<projeto>/**/*.jsonl"`.
- O volume `/Volumes/AMRecomp` (imagem no SSD) pode ser desanexado pelo macOS: antes de rodar algo longo, confira
  `ls /Volumes/AMRecomp`; para remontar, `hdiutil attach /Volumes/SSD/AMRecomp.sparsebundle`.
- Ênfase do vídeo: o "como funciona" de cada ferramenta (DolRecomp, runtime, Dolphin como oráculo, Ghidra, REA).
- Doc e `.md` ficam em sincronia; o `.md` é commitado. Nunca mostrar código ou bytes do jogo nos roteiros.
- Agentes em segundo plano não editam os roteiros: reportam o que fizeram, e a sessão principal escreve.

## Regras operacionais

- Tudo pesado (builds, traces, dumps, Dolphin, Ghidra) fica em `/Volumes/AMRecomp`; o disco interno é pequeno.
  Execuções com `scripts/with_timeout.sh`; traces com teto de tamanho.
- Nunca versionar bytes do jogo (ISO, DOL/REL, dumps de memória, traces brutos).
- Commits locais pequenos, mensagem em português `M<n>: ...`, com as linhas de atribuição da sessão.
- Dolphin é só oráculo de desenvolvimento; scripts que o abrem devem fechá-lo em qualquer saída.
