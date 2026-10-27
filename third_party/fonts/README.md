# Fontes embutidas na tela inicial

Todas sob a SIL Open Font License 1.1 (texto em `OFL-*.txt` desta pasta). Origem: repositório github.com/google/fonts
(`ofl/saira`, `ofl/sairacondensed`, `ofl/jetbrainsmono`).

| Arquivo em `assets/launcher/` | Origem | Alteração |
| --- | --- | --- |
| `Saira-400/500/600/700.ttf` | `Saira[wdth,wght].ttf` (Saira Project Authors) | instância estática (wdth=100, wght=400/500/600/700) e subconjunto Latin (ASCII, Latin-1, Latin Extended-A, pontuação e setas); nomes internos mantidos (a fonte não declara nome reservado) |
| `SairaCondensed-ExtraBold.ttf` | `SairaCondensed-ExtraBold.ttf` | nenhuma (o arquivo é o original: a licença reserva o nome "Saira") |
| `JetBrainsMono-400.ttf` | `JetBrainsMono[wght].ttf` (JetBrains Mono Project Authors) | instância estática wght=400 e o mesmo subconjunto |

`scripts/gen_launcher_assets.py` gera `runtime/platform/ui_assets_data.c` a partir de `assets/launcher/`; as fontes
foram feitas com `--make-fonts` do mesmo script (fontTools). Os arquivos são embutidos como dados no executável, não
distribuídos como fontes avulsas.
