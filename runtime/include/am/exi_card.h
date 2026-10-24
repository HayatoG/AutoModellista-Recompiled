/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Memory card no slot A (canal 0, aparelho 0, chip select 1) como o Dolphin 2609a o emula: Core/HW/EXI/
 * EXI_DeviceMemoryCard.{h,cpp}. Usado só por hw/exi.c; o conteúdo do cartão vem de am/memcard.h. */
#ifndef AM_EXI_CARD_H
#define AM_EXI_CARD_H

#include "am/types.h"

/* Estado de power-on do aparelho; "presente" se memcard_present(). */
void exi_card_reset(void);
bool exi_card_present(void);
/* CEXIMemoryCard::SetCS: `cs` é o campo CHIP_SELECT inteiro do canal. */
void exi_card_set_cs(u32 cs);
/* CEXIMemoryCard::TransferByte. */
u8 exi_card_transfer(u8 data);
/* DMARead (cartão para a memória) e DMAWrite (memória para o cartão): copiam já e agendam o TCINT conforme a
 * velocidade do cartão. `addr` é o endereço físico do registrador de DMA do canal. */
void exi_card_dma(bool write, u32 addr, u32 size);
/* IsInterruptSet. */
bool exi_card_interrupt(void);
/* Eventos agendados (fim de comando e fim de DMA) com prazo vencido; o chassi chama via bus_update. */
void exi_card_update(void);
/* Ciclo do próximo evento, ou UINT64_MAX. */
u64 exi_card_next_event_cycle(void);
/* Primeiros MEMCARD_HEADER_BYTES do cartão, de onde sai o flash ID da SRAM (Sram.cpp: SetCardFlashID). */
void exi_card_header(u8 *out20);

/* Contadores para o relatório (nada disso entra no tempo emulado). */
typedef struct ExiCardStats {
    u64 commands;    /* chip selects com comando */
    u64 array_bytes; /* bytes lidos de um em um (ReadArray imediato) */
    u64 dma_reads;   /* DMAs cartão -> memória */
    u64 dma_read_bytes;
    u64 dma_writes; /* DMAs memória -> cartão */
    u64 dma_write_bytes;
    u64 page_programs; /* PageProgram concluídos (até 128 bytes cada) */
    u64 program_bytes;
    u64 sector_erases;
    u64 chip_erases;
} ExiCardStats;
const ExiCardStats *exi_card_stats(void);

/* Ponte para hw/exi.c. */
void exi_channel_transfer_complete(u32 channel);
void exi_refresh_interrupts(void);

#endif /* AM_EXI_CARD_H */
