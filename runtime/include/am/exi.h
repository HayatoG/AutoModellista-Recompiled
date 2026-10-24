/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Expansion Interface (0xCC006800): 3 canais de 5 registradores de 32 bits (status, endereço e tamanho de DMA,
 * controle, dado imediato). YAGCD 5.9; Dolphin Core/HW/EXI/EXI.cpp, EXI_Channel.{h,cpp}, EXI_DeviceIPL.{h,cpp},
 * HW/Sram.{h,cpp} (tag 2609a).
 *
 * Aparelhos (os mesmos do oráculo, scripts/dolphin_trace.py): o IPL (canal 0, aparelho 1: SRAM, RTC e UART) e, só
 * quando há um cartão em am/memcard.h no exi_reset, o memory card do slot A (am/exi_card.h); slot B e portas seriais
 * vazios. */
#ifndef AM_EXI_H
#define AM_EXI_H

#include "am/types.h"

#define EXI_BASE          0x0C006800U
#define EXI_CHANNEL_COUNT 3U
#define EXI_CHANNEL_SIZE  0x14U

#define EXI_REG_STATUS  0x00U
#define EXI_REG_DMA_ADR 0x04U
#define EXI_REG_DMA_LEN 0x08U
#define EXI_REG_CONTROL 0x0CU
#define EXI_REG_IMM     0x10U

/* Status (Dolphin EXI_Channel.h: UEXI_STATUS). */
#define EXI_SR_EXIINTMASK 0x0001U
#define EXI_SR_EXIINT     0x0002U
#define EXI_SR_TCINTMASK  0x0004U
#define EXI_SR_TCINT      0x0008U
#define EXI_SR_CLK        0x0070U
#define EXI_SR_CS         0x0380U
#define EXI_SR_CS_SHIFT   7U
#define EXI_SR_EXTINTMASK 0x0400U
#define EXI_SR_EXTINT     0x0800U
#define EXI_SR_EXT        0x1000U
#define EXI_SR_ROMDIS     0x2000U

/* Controle (UEXI_CONTROL). */
#define EXI_CR_TSTART 0x01U
#define EXI_CR_DMA    0x02U

/* SRAM de 0x44 bytes (Dolphin Sram.h), endereços do aparelho IPL (EXI_DeviceIPL.h). */
#define EXI_SRAM_SIZE     0x44U
#define EXI_IPL_ROM_SIZE  0x200000U
#define EXI_IPL_SRAM_BASE 0x800000U
#define EXI_IPL_UART_BASE 0x800400U
#define EXI_IPL_UART_SIZE 0x50U

void exi_reset(void);
/* Eventos do cartão (fim de comando e de DMA) com prazo vencido; o chassi chama via bus_update. */
void exi_update(void);
/* Relógio do RTC (segundos desde 2000-01-01): o IPL o copia para sram[0..3] a cada comando, como o Dolphin. */
void exi_set_rtc(u32 seconds);
u64 exi_read(u32 offset, u8 size);
void exi_write(u32 offset, u64 value, u8 size);

/* Para testes: conteúdo atual da SRAM e o texto que já saiu pela UART do IPL (OSReport). */
const u8 *exi_sram(void);
const char *exi_uart_text(void);

#endif /* AM_EXI_H */
