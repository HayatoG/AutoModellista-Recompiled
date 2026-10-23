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

#endif /* AM_GAME_H */
