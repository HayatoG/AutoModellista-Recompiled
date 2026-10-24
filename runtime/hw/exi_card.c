/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/exi_card.h"

#include <stdint.h>
#include <string.h>

#include "am/log.h"
#include "am/mem.h"
#include "am/memcard.h"
#include "am/timebase.h"

/* EXI_DeviceMemoryCard.cpp: bits do status do cartão. */
#define MC_STATUS_BUSY         0x80U
#define MC_STATUS_UNLOCKED     0x40U
#define MC_STATUS_ERASEERROR   0x10U
#define MC_STATUS_PROGRAMERROR 0x08U
#define MC_STATUS_READY        0x01U

/* Taxas de transferência do cartão e clock da CPU (SystemTimers::GetTicksPerSecond() = 486 MHz): o Dolphin divide em
 * inteiros, e o produto por `size` é o atraso do TCINT. */
#define MC_TICKS_PER_SECOND    486000000U
#define MC_TRANSFER_RATE_READ  (512U * 1024U)
#define MC_TRANSFER_RATE_WRITE 98432U /* (u32)(96.125f * 1024.0f) */
#define MC_CMD_DONE_CYCLES     5000U

#define MC_PAGE_BUFFER 128U

typedef enum McCommand {
    MC_NINTENDO_ID = 0x00,
    MC_READ_ARRAY = 0x52,
    MC_ARRAY_TO_BUFFER = 0x53,
    MC_SET_INTERRUPT = 0x81,
    MC_WRITE_BUFFER = 0x82,
    MC_READ_STATUS = 0x83,
    MC_READ_ID = 0x85,
    MC_READ_ERROR_BUFFER = 0x86,
    MC_WAKE_UP = 0x87,
    MC_SLEEP = 0x88,
    MC_CLEAR_STATUS = 0x89,
    MC_SECTOR_ERASE = 0xF1,
    MC_PAGE_PROGRAM = 0xF2,
    MC_EXTRA_BYTE_PROGRAM = 0xF3,
    MC_CHIP_ERASE = 0xF4,
} McCommand;

static struct {
    bool present;
    u32 interrupt_switch;
    bool interrupt_set;
    u32 command;
    u32 status;
    u32 position;
    u8 program_buffer[MC_PAGE_BUFFER];
    u32 card_id;
    u32 address;
    u32 card_size;
    bool cmd_done_pending;
    u64 cmd_done_cycle;
    bool transfer_pending;
    u64 transfer_cycle;
} s_mc;
static ExiCardStats s_stats;

const ExiCardStats *exi_card_stats(void) {
    return &s_stats;
}

void exi_card_reset(void) {
    memset(&s_mc, 0, sizeof(s_mc));
    memset(&s_stats, 0, sizeof(s_stats));
    s_mc.present = memcard_present();
    if (!s_mc.present) {
        return;
    }
    s_mc.command = MC_NINTENDO_ID;
    s_mc.status = MC_STATUS_BUSY | MC_STATUS_UNLOCKED | MC_STATUS_READY;
    s_mc.card_id = memcard_mbit();
    s_mc.card_size = s_mc.card_id * MEMCARD_MBIT_BYTES;
}

bool exi_card_present(void) {
    return s_mc.present;
}

void exi_card_header(u8 *out20) {
    memcard_read(0, MEMCARD_HEADER_BYTES, out20);
}

static void cmd_done_later(u64 cycles) {
    s_mc.cmd_done_pending = true; /* RemoveEvent + ScheduleEvent: só existe um */
    s_mc.cmd_done_cycle = timebase_cycles(NULL) + cycles;
}

void exi_card_set_cs(u32 cs) {
    if (cs != 0U) {
        s_mc.position = 0;
        return;
    }
    switch (s_mc.command) {
    case MC_SECTOR_ERASE:
        if (s_mc.position > 2U) {
            memcard_clear_block(s_mc.address & (s_mc.card_size - 1U));
            s_stats.sector_erases++;
            s_mc.status |= MC_STATUS_BUSY;
            s_mc.status &= ~MC_STATUS_READY;
            cmd_done_later(MC_CMD_DONE_CYCLES);
        }
        break;
    case MC_CHIP_ERASE:
        if (s_mc.position > 2U) {
            memcard_clear_all();
            s_stats.chip_erases++;
            s_mc.status &= ~MC_STATUS_BUSY;
        }
        break;
    case MC_PAGE_PROGRAM:
        if (s_mc.position >= 5U) {
            u32 count = s_mc.position - 5U;
            u32 i = 0;
            s_stats.page_programs++;
            s_stats.program_bytes += count;
            s_mc.status &= ~MC_STATUS_BUSY;
            while (count-- != 0U) {
                memcard_write(s_mc.address, 1, &s_mc.program_buffer[i++]);
                i &= (MC_PAGE_BUFFER - 1U);
                s_mc.address = (s_mc.address & ~0x1FFU) | ((s_mc.address + 1U) & 0x1FFU);
            }
            cmd_done_later(MC_CMD_DONE_CYCLES);
        }
        break;
    default:
        break;
    }
}

bool exi_card_interrupt(void) {
    return s_mc.present && s_mc.interrupt_switch != 0U && s_mc.interrupt_set;
}

/* Endereço de 4 bytes (AD1, AD2, AD3, BA) dos comandos de leitura e de programação. */
static void address_byte(u32 position, u8 byte) {
    switch (position) {
    case 1:
        s_mc.address = (u32)byte << 17;
        break;
    case 2:
        s_mc.address |= (u32)byte << 9;
        break;
    case 3:
        s_mc.address |= (u32)(byte & 3U) << 7;
        break;
    case 4:
        s_mc.address |= (u32)(byte & 0x7FU);
        break;
    default:
        break;
    }
}

static u8 read_array_byte(u8 byte) {
    const u32 pos = s_mc.position;
    if (pos == 1U) {
        address_byte(pos, byte);
        return 0xFFU;
    }
    if (pos <= 4U) {
        address_byte(pos, byte);
    }
    if (pos > 1U) {
        memcard_read(s_mc.address & (s_mc.card_size - 1U), 1, &byte);
        s_stats.array_bytes++;
        /* Depois de 9 bytes só o deslocamento dentro do setor avança; o ponteiro dá a volta. */
        if (pos >= 9U) {
            s_mc.address = (s_mc.address & ~0x1FFU) | ((s_mc.address + 1U) & 0x1FFU);
        }
    }
    return byte;
}

static u8 command_byte(u8 byte) {
    const u32 pos = s_mc.position;
    switch (s_mc.command) {
    case MC_NINTENDO_ID:
        /* Cartão Nintendo: 00 | 80 00 00 00 10 00 00 00 (o "bigben" difere; o Dolphin imita o Nintendo). */
        return pos == 1U ? 0x80U : (u8)(s_mc.card_id >> (24U - (((pos - 2U) & 3U) * 8U)));
    case MC_READ_ARRAY:
        return read_array_byte(byte);
    case MC_READ_STATUS:
        return (u8)s_mc.status;
    case MC_READ_ID: {
        const u32 id = 0xC221U; /* cartão de marca Nintendo */
        if (pos == 1U) {
            return (u8)(id >> 8);
        }
        return (u8)((pos & 1U) != 0U ? id : id >> 8);
    }
    case MC_SECTOR_ERASE:
        if (pos <= 2U) {
            address_byte(pos, byte);
        }
        return 0xFFU;
    case MC_SET_INTERRUPT:
        if (pos == 1U) {
            s_mc.interrupt_switch = byte;
        }
        return 0xFFU;
    case MC_CHIP_ERASE:
        return 0xFFU;
    case MC_PAGE_PROGRAM:
        if (pos <= 4U) {
            address_byte(pos, byte);
        }
        if (pos >= 5U) {
            s_mc.program_buffer[(pos - 5U) & (MC_PAGE_BUFFER - 1U)] = byte; /* dá a volta depois de 128 bytes */
        }
        return 0xFFU;
    default:
        AM_LOG_WARN("EXI MEMCARD: byte de comando desconhecido %02x (comando %02x)", byte, s_mc.command);
        return 0xFFU;
    }
}

u8 exi_card_transfer(u8 byte) {
    if (s_mc.position == 0U) {
        s_mc.command = byte; /* o primeiro byte é o comando */
        s_stats.commands++;
        byte = 0xFFU;
        if (s_mc.command == MC_CLEAR_STATUS) {
            s_mc.status &= ~(MC_STATUS_PROGRAMERROR | MC_STATUS_ERASEERROR);
            s_mc.status |= MC_STATUS_READY;
            s_mc.interrupt_set = false;
            s_mc.position = 0; /* o Dolphin zera e depois incrementa: o próximo byte já é o byte 1 */
        }
    } else {
        byte = command_byte(byte);
    }
    s_mc.position++;
    return byte;
}

static void transfer_in(u32 size, u32 rate) {
    s_mc.transfer_pending = true;
    s_mc.transfer_cycle = timebase_cycles(NULL) + (u64)(u32)(size * (MC_TICKS_PER_SECOND / rate));
}

void exi_card_dma(bool write, u32 addr, u32 size) {
    const GuestAddr guest = MEM_CACHED_BASE | (addr & 0x03FFFFFFU);
    u8 *host = mem_host_ptr(guest, size);
    if (host == NULL) {
        am_fatal("EXI MEMCARD: DMA de %u bytes em 0x%08X fora da MEM1", size, addr);
    }
    if (write) {
        memcard_write(s_mc.address, size, host);
        s_stats.dma_writes++;
        s_stats.dma_write_bytes += size;
        transfer_in(size, MC_TRANSFER_RATE_WRITE);
    } else {
        memcard_read(s_mc.address, size, host);
        s_stats.dma_reads++;
        s_stats.dma_read_bytes += size;
        mem_mark_written(addr & 0x03FFFFFFU, size);
        transfer_in(size, MC_TRANSFER_RATE_READ);
    }
}

u64 exi_card_next_event_cycle(void) {
    u64 next = UINT64_MAX;
    if (s_mc.cmd_done_pending) {
        next = s_mc.cmd_done_cycle;
    }
    if (s_mc.transfer_pending && s_mc.transfer_cycle < next) {
        next = s_mc.transfer_cycle;
    }
    return next;
}

void exi_card_update(void) {
    if (!s_mc.cmd_done_pending && !s_mc.transfer_pending) {
        return;
    }
    const u64 now = timebase_cycles(NULL);
    for (;;) {
        const u64 next = exi_card_next_event_cycle();
        if (next > now) {
            return;
        }
        if (s_mc.cmd_done_pending && s_mc.cmd_done_cycle == next) {
            /* CmdDone: pronto, não ocupado, interrupção do cartão. */
            s_mc.cmd_done_pending = false;
            s_mc.status |= MC_STATUS_READY;
            s_mc.status &= ~MC_STATUS_BUSY;
            s_mc.interrupt_set = true;
            exi_refresh_interrupts();
        } else {
            s_mc.transfer_pending = false;
            exi_channel_transfer_complete(0);
        }
    }
}
