# Avaliação da runtime ModernGekko

Estado: **leitura de código e documentação concluída; build não executado** (espaço em disco).

## O que a ModernGekko é, de fato

- `ExpansionPak/ModernGekko` (`a2e4e2b`, GPL-3.0) é uma camada de integração sobre `vendor/dolphin`, que é o
  **RecompCore** (fork do Dolphin com suporte a StaticRecomp; base Dolphin `1ccbcaa`).
- Vídeo, áudio e HLE vêm do Dolphin (VideoCommon: emulação de CP/XF/BP, decodificação de texturas, TEV em shaders).
  `src/gpu/` tem `gx_command_processor`, `gx_vertex_loader`, `gx_texture_decoder`, `dolphin_shader_compiler`.
- O código do jogo roda como módulo nativo gerado pelo DolRecomp. **Código não coberto pelo módulo cai no JIT do
  Dolphin** (`8b47e90bf6 support mod hooks and yielding fallback jit`), que retorna ao nativo quando possível.

Consequência para as alegações do projeto: enquanto houver fallback de JIT em uso, o resultado é um **híbrido
recompilação estática + emulação**, não um "port nativo puro". Vamos medir a cobertura (perfis `native_cycles` /
`module_miss`) e documentar a porcentagem que roda nativa antes de qualquer alegação.

## Subsistemas

| Subsistema | Fornecido por | Observação para Auto Modellista |
| --- | --- | --- |
| CPU/memória | DolRecomp + `src/memory/address_space.cpp` | endereços originais preservados (memória emulada big-endian) |
| GX/XF/BP/CP | Dolphin VideoCommon | cel-shading do jogo depende de TEV/EFB; Dolphin já roda o jogo, então é ponto de partida fiel |
| Áudio | Dolphin (DSP HLE/AX) | verificar se o jogo usa streaming de disco (header da ISO, byte 0x08) |
| Entrada | Dolphin GCPad | **sem UI**: exige `GCPadNew.ini` manual em `~/.local/share/moderngekko/Config/` |
| Disco/saves | Dolphin DVD/CARD | memory card virtual herdado do Dolphin |
| Mods | `mod_loader.cpp` | hooks de entrada/retorno, patches por endereço |

## Custo de build (por que não foi feito nesta rodada)

O submódulo `vendor/dolphin` (RecompCore, ~500 MB de histórico) puxa Externals do Dolphin (SDL, fmt, imgui,
Vulkan…). Clone + build Release do Dolphin costuma ocupar vários GB; com 8,3 GB livres no disco interno o risco de
encher o sistema é real. Plano: clonar e compilar em `/Volumes/SSD`.

## Template

Usar `ExpansionPak/ModernGekko-Template` (`c2553a9`, set/2026), não o `snazzmaster/ModernGekkate` (fork de jul/2026
sem as mudanças posteriores). O fluxo é `make check` → `make tools` → `make recompile ISO=…` → `make run GAME=…`.
