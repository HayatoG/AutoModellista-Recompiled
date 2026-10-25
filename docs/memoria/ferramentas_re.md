# Ghidra e REA (engenharia reversa, instalados no Mac em 23/10/2026)

- Ghidra 12.1.4 (o REA só aceita 12.1.x), decompilador nativo compilado com `gradlew buildNatives`; JDK 21 (Temurin 21.0.13).
- Extensão GameCubeLoader 1.3.1 (Apache-2.0): linguagem `PowerPC:BE:32:Gekko_Broadway` e loader de DOL/REL.
- REA 6.7.0 (`npm install -g rea-agents`) como MCP `rea` com `REA_GHIDRA_LANGUAGE_ID=PowerPC:BE:32:Gekko_Broadway`.
- O `main.elf` do dtk tem `e_flags = 0x80000000` e o REA recusa: usar uma cópia com `e_flags = 0` (3.897 funções, 9.905 strings).
- Não são necessários para o M9 (portabilidade); só para depurar o jogo ou para o decomp (M10).
