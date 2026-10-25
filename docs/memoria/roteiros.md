# Roteiros a manter atualizados

Regra do usuário (23/10/2026): a cada etapa relevante do port, atualizar **todos** estes registros, com números reais,
logo após cada evento (queda, erro, achado, agente novo, ferramenta usada). O roteiro é o produto (vídeo).

| Registro | Onde | Conteúdo |
| --- | --- | --- |
| Roteiro de estudo | Doc https://claude.ai/artifact/6NQA8T4JE8fNcTG2PGE2kT (corpo `2dff2a47-3dca`) + `docs/ROTEIRO.md` | decisões, linha do tempo, tokens e tempo, tempo acumulado, problemas e lições, próximos passos |
| Roteiro do vídeo | Doc https://claude.ai/artifact/6nuBK9gyqKM3wP5tHJhBpL (corpo `acaf7782-0cb4`) + `docs/ROTEIRO_VIDEO.md` | cada cena com **Fala**, **Na tela**, **Motion**; a cena de tokens é sempre a última do Cap. 4; "Registro de atualizações" no fim |
| Status | ai-memory `notes/auto-modellista-port-status.md` (no Mac); aqui, `docs/memoria/status.md` | marcos, commits, lições |

- Os docs do Claude só abrem com a conta do usuário (MCP Claude Docs: `update` com `ref {"object":"node","id":"<corpo>"}`, `engine "prose"`, `container {"kind":"project","id":"<artifact id>"}`). O `.md` é commitado e fica em sincronia com o doc.
- Nunca mostrar código ou bytes do jogo nos roteiros. Agentes em segundo plano não editam os roteiros; a sessão principal escreve.
- Tokens: `python3 -I scripts/usage_report.py "<dir de projetos do Claude Code>/**/*.jsonl"` (UTC; Brasília = UTC-3). Tempo acumulado: `scripts/tempo_dev.py` (pausa > 15 min não conta).
- Até 25/10 às 02:48: ~967 M tokens processados, 4,4 M gerados; trabalho acumulado 24h52, calendário 72h49 (M9 parte 1). Depois: parte 3a (Linux, 8,5 M) e parte 5 (launcher, 36,0 M, 13:07→13:59).
