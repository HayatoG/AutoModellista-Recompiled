/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Endereços específicos do main.dol de Auto Modellista (GAUE08, rev. 0; SHA-256 em analysis/iso/main_dol.sha256).
 * Fonte: símbolos do dtk (analysis/call_graph.json, analysis/known_addresses.md) e disassembly do DOL. */
#ifndef AM_GAME_H
#define AM_GAME_H

#define GAME_ADDR_START   0x80003154U /* __start (entry point do cabeçalho do DOL) */
#define GAME_ADDR_OS_INIT 0x800C25DCU /* OSInit */
#define GAME_ADDR_MAIN    0x800719ACU /* main */

/* Modelo do vetor de exceção copiado por OSExceptionInit (0x800C29B4): começa no mtsprg 0,r4 e termina no nop de
 * __OSEVEnd. O "li r3,0" de __OSEVSetNumber recebe o número; quando MSR[RI]=0 o modelo desvia para 0x800C2D28
 * (lis/addi em 0x800C2D04). */
#define GAME_ADDR_OS_EV_START        0x800C2C8CU
#define GAME_ADDR_OS_EV_END          0x800C2D24U
#define GAME_ADDR_OS_EV_SET_NUMBER   0x800C2CF4U
#define GAME_ADDR_OS_DEFAULT_HANDLER 0x800C2D28U

/* Tratador de "sc" que __OSInitSystemCall (0x800C7A64) copia para 0x80000C00: liga HID0[ABE], isync/sync, restaura
 * HID0 e rfi. O SDK usa "sc" como barreira de sincronização no fim de DCFlushRange/DCStoreRange. */
#define GAME_ADDR_OS_SC_START 0x800C7A44U
#define GAME_ADDR_OS_SC_END   0x800C7A60U

/* OSLink.c do SDK (ordem do arquivo, conferida no disassembly: Link em 0x800C5990 chama OSNotifyLink 0x800C56CC):
 * OSLink = Link(m, bss, FALSE), OSLinkFixed = Link(m, bss, TRUE) só para versão >= 3, OSUnlink desfaz. Chamados pelo
 * carregador de módulos do jogo (fn_80071658 liga, fn_800715FC desliga). */
#define GAME_ADDR_OS_LINK       0x800C5C70U
#define GAME_ADDR_OS_LINK_FIXED 0x800C5C94U
#define GAME_ADDR_OS_UNLINK     0x800C5F08U

/* Laço principal: fn_8004838C chama fn_80051034 (um quadro: lógica, VIWaitForRetrace, VISetNextFrameBuffer...) em
 * 0x80048398; analysis/known_addresses.md. */
#define GAME_ADDR_FRAME           0x80051034U
#define GAME_ADDR_FRAME_RETURN    0x8004839CU
#define GAME_ADDR_VI_WAIT_RETRACE 0x800D276CU

/* VISetNextFrameBuffer (0x800D358C) guarda o próximo XFB nesta variável do vi.c do SDK; VIFlush (0x800D345C) a copia
 * para o VI (REA/Ghidra; scripts/dolphin_trace.py lê a mesma variável para as capturas do oráculo). */
#define GAME_ADDR_VI_NEXT_FB 0x8026E648U
/* XFB do jogo em NTSC: 640x448 YUYV, 1280 bytes por linha (o mesmo tamanho que o oráculo grava). */
#define GAME_XFB_WIDTH  640U
#define GAME_XFB_HEIGHT 448U

#endif /* AM_GAME_H */
