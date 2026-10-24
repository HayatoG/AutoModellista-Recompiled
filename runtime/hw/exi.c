/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/exi.h"

#include <string.h>

#include "am/bus.h"
#include "am/exi_card.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/memcard.h"
#include "am/pi.h"

#define EXI_UART_TEXT_MAX 4096U
#define EXI_IPL_DEVICE    1U /* canal 0, aparelho 1 (chip select 2) */
#define EXI_CARD_DEVICE   0U /* canal 0, aparelho 0 (chip select 1): memory card do slot A */

typedef struct ExiChannel {
    u32 status;
    u32 dma_addr;
    u32 dma_len;
    u32 control;
    u32 imm;
} ExiChannel;

static struct {
    ExiChannel ch[EXI_CHANNEL_COUNT];
    u8 sram[EXI_SRAM_SIZE];
    u32 rtc;
    u32 ipl_command;
    u32 ipl_command_bytes;
    u32 ipl_cursor;
    char uart_line[256];
    size_t uart_len;
    char uart_text[EXI_UART_TEXT_MAX];
    size_t uart_text_len;
} s_exi;

static void put16(u8 *p, u16 v) {
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}

/* Dolphin HW/Sram.cpp: modelo sram_dump (inglês, estéreo, OOBE feito, IDs de cartão "DOLPHINSLOTA/B") e
 * FixSRAMChecksums (soma de 16 bits big-endian sobre rtc_bias..flags). O construtor de CEXIIPL zera rtc e rtc_bias e
 * aplica o idioma da configuração (MAIN_GC_LANGUAGE, padrão 0 = inglês). */
static void sram_init(void) {
    memset(s_exi.sram, 0, sizeof(s_exi.sram));
    s_exi.sram[0x17] = 0x20U | 0x08U | 0x04U; /* flags: bit 5, kOobeDone, kStereo */
    memcpy(&s_exi.sram[0x18], "DOLPHINSLOTA", 12);
    memcpy(&s_exi.sram[0x24], "DOLPHINSLOTB", 12);
    s_exi.sram[0x3E] = 0x6EU; /* flash_id_checksum */
    s_exi.sram[0x3F] = 0x6DU;
    u16 sum = 0;
    u16 inv = 0;
    for (u32 off = 0x10U; off < 0x18U; off += 2U) {
        const u16 v = (u16)((s_exi.sram[off] << 8) | s_exi.sram[off + 1U]);
        sum = (u16)(sum + v);
        inv = (u16)(inv + (u16)~v);
    }
    put16(&s_exi.sram[0x04], sum);
    put16(&s_exi.sram[0x06], inv);
}

/* Sram.cpp: SetCardFlashID (slot A). O flash ID da SRAM é o do cabeçalho do cartão "desembaralhado" com o mesmo
 * gerador congruencial que o SDK usa para embaralhá-lo (semente = horário da formatação, bytes 12..19). */
static void sram_set_card_flash_id(const u8 *header) {
    u64 rand = 0;
    for (u32 i = 12U; i < 20U; i++) {
        rand = (rand << 8) | header[i];
    }
    u8 csum = 0;
    for (u32 i = 0; i < 12U; i++) {
        rand = ((rand * 0x41C64E6DULL) + 0x3039ULL) >> 16;
        const u8 id = (u8)(header[i] - (u8)rand);
        s_exi.sram[0x18U + i] = id;
        csum = (u8)(csum + id);
        rand = ((rand * 0x41C64E6DULL) + 0x3039ULL) >> 16;
        rand &= 0x7FFFULL;
    }
    s_exi.sram[0x3E] = (u8)(csum ^ 0xFFU);
}

void exi_reset(void) {
    memset(&s_exi, 0, sizeof(s_exi));
    /* CEXIChannel: canais 0 e 1 começam com EXTINT; o canal 1 com chip select 1. */
    s_exi.ch[0].status = EXI_SR_EXTINT;
    s_exi.ch[1].status = EXI_SR_EXTINT | (1U << EXI_SR_CS_SHIFT);
    sram_init();
    exi_card_reset();
    if (exi_card_present()) {
        u8 header[MEMCARD_HEADER_BYTES];
        exi_card_header(header);
        sram_set_card_flash_id(header);
    }
}

void exi_update(void) {
    exi_card_update();
}

void exi_set_rtc(u32 seconds) {
    s_exi.rtc = seconds;
}

const u8 *exi_sram(void) {
    return s_exi.sram;
}

const char *exi_uart_text(void) {
    return s_exi.uart_text;
}

/* Aparelho por chip select (CEXIChannel::GetDevice: 1 -> 0, 2 -> 1, 4 -> 2). -1 = nenhum. */
static int device_of(u32 cs) {
    switch (cs) {
    case 1U:
        return 0;
    case 2U:
        return 1;
    case 4U:
        return 2;
    default:
        return -1;
    }
}

static bool device_present(u32 channel, int device) {
    return channel == 0U && (device == (int)EXI_IPL_DEVICE || (device == (int)EXI_CARD_DEVICE && exi_card_present()));
}

static void uart_byte(u8 c) {
    if (c != 0U && s_exi.uart_len + 1U < sizeof(s_exi.uart_line)) {
        s_exi.uart_line[s_exi.uart_len++] = (char)c;
    }
    if (c != 0U && s_exi.uart_text_len + 1U < sizeof(s_exi.uart_text)) {
        s_exi.uart_text[s_exi.uart_text_len++] = (char)c;
    }
    if (c == '\r' || c == '\n') {
        s_exi.uart_line[s_exi.uart_len > 0U ? s_exi.uart_len - 1U : 0U] = '\0';
        AM_LOG_INFO("[OSReport] %s", s_exi.uart_line);
        s_exi.uart_len = 0;
    }
}

/* CEXIIPL::TransferByte: 4 bytes de comando (bit 31 = escrita, endereço = bits 6..30), depois os dados. */
static u8 ipl_transfer(u8 data) {
    if (s_exi.ipl_command_bytes < 4U) {
        s_exi.ipl_command = (s_exi.ipl_command << 8) | data;
        if (++s_exi.ipl_command_bytes == 4U) {
            /* UpdateRTC: o RTC é lido como os 4 primeiros bytes da SRAM. */
            s_exi.sram[0] = (u8)(s_exi.rtc >> 24);
            s_exi.sram[1] = (u8)(s_exi.rtc >> 16);
            s_exi.sram[2] = (u8)(s_exi.rtc >> 8);
            s_exi.sram[3] = (u8)s_exi.rtc;
        }
        return 0xFFU;
    }
    const bool write = (s_exi.ipl_command >> 31) != 0U;
    const u32 addr = (s_exi.ipl_command >> 6) & 0x1FFFFFFU;
    if (addr < EXI_IPL_ROM_SIZE) {
        am_fatal("EXI IPL: leitura da ROM/fontes em 0x%06X não implementada (fontes do IPL: TODO(M7))", addr);
    }
    if (addr >= EXI_IPL_SRAM_BASE && addr < EXI_IPL_SRAM_BASE + EXI_SRAM_SIZE) {
        const u32 at = (addr - EXI_IPL_SRAM_BASE + s_exi.ipl_cursor++) % EXI_SRAM_SIZE;
        if (write) {
            s_exi.sram[at] = data;
            return data;
        }
        return s_exi.sram[at];
    }
    if (addr >= EXI_IPL_UART_BASE && addr < EXI_IPL_UART_BASE + EXI_IPL_UART_SIZE) {
        if (addr == EXI_IPL_UART_BASE) {
            if (write) {
                uart_byte(data);
                return data;
            }
            return 0; /* "Queue Length": instantâneo */
        }
        return data; /* 0x0C e 0x4C (Barnacle): Dolphin só registra */
    }
    am_fatal("EXI IPL: endereço 0x%06X (comando 0x%08X) desconhecido", addr, s_exi.ipl_command);
}

static u8 transfer(u32 channel, int device, u8 data) {
    if (!device_present(channel, device)) {
        return data; /* IEXIDevice vazio não altera nada */
    }
    return device == (int)EXI_CARD_DEVICE ? exi_card_transfer(data) : ipl_transfer(data);
}

/* IEXIDevice::ImmRead/ImmWrite (aparelho vazio: ImmRead dá 0; ImmReadWrite não faz nada). O cartão faz o DMA de uma vez
 * só (CEXIMemoryCard::DMARead/DMAWrite) e agenda o TCINT pela velocidade dele. */
static void run_transfer(u32 channel, int device) {
    ExiChannel *c = &s_exi.ch[channel];
    const u32 rw = (c->control >> 2) & 3U;
    if ((c->control & EXI_CR_DMA) != 0U && device == (int)EXI_CARD_DEVICE && device_present(channel, device)) {
        if (rw > 1U) {
            am_fatal("EXI%u: DMA com tipo %u inválido", channel, rw);
        }
        exi_card_dma(rw == 1U, c->dma_addr, c->dma_len);
        return;
    }
    if ((c->control & EXI_CR_DMA) == 0U) {
        const u32 len = ((c->control >> 4) & 3U) + 1U;
        if (rw == 0U) {
            u32 result = 0;
            for (u32 i = 0; i < len; i++) {
                const u8 b = device_present(channel, device) ? transfer(channel, device, 0) : 0U;
                result |= (u32)b << (24U - (i * 8U));
            }
            c->imm = result;
        } else if (rw == 1U) {
            for (u32 i = 0; i < len; i++) {
                (void)transfer(channel, device, (u8)(c->imm >> (24U - (i * 8U))));
            }
        } else if (rw != 2U) {
            am_fatal("EXI%u: tipo de transferência imediata %u inválido", channel, rw);
        }
        return;
    }
    const GuestAddr base = MEM_CACHED_BASE | (c->dma_addr & 0x03FFFFE0U);
    for (u32 i = 0; i < c->dma_len; i++) {
        if (rw == 0U) {
            mem_write8(NULL, base + i, transfer(channel, device, 0));
        } else if (rw == 1U) {
            (void)transfer(channel, device, mem_read8(NULL, base + i));
        } else {
            am_fatal("EXI%u: DMA com tipo %u inválido", channel, rw);
        }
    }
}

static void update_interrupts(void);

void exi_refresh_interrupts(void) {
    update_interrupts();
}

void exi_channel_transfer_complete(u32 channel) {
    s_exi.ch[channel].status |= EXI_SR_TCINT; /* SendTransferComplete */
    update_interrupts();
}

static void update_interrupts(void) {
    bool cause = false;
    for (u32 i = 0; i < EXI_CHANNEL_COUNT; i++) {
        /* CEXIChannel::IsCausingInterrupt: o aparelho 0 dos canais 0 e 1 (o cartão) é sempre consultado. */
        if (i == 0U && exi_card_interrupt()) {
            s_exi.ch[i].status |= EXI_SR_EXIINT;
        }
        const u32 s = s_exi.ch[i].status;
        cause = cause || ((s & EXI_SR_EXIINT) != 0U && (s & EXI_SR_EXIINTMASK) != 0U) ||
                ((s & EXI_SR_TCINT) != 0U && (s & EXI_SR_TCINTMASK) != 0U) ||
                ((s & EXI_SR_EXTINT) != 0U && (s & EXI_SR_EXTINTMASK) != 0U);
    }
    pi_set_interrupt(PI_INT_EXI, cause);
}

/* CEXIChannel::RegisterMMIO, EXI_STATUS (escrita). */
static void write_status(u32 channel, u32 v) {
    ExiChannel *c = &s_exi.ch[channel];
    u32 s = c->status;
    s = (s & ~(EXI_SR_EXIINTMASK | EXI_SR_TCINTMASK | EXI_SR_CLK)) |
        (v & (EXI_SR_EXIINTMASK | EXI_SR_TCINTMASK | EXI_SR_CLK));
    s &= ~(v & (EXI_SR_EXIINT | EXI_SR_TCINT)); /* 1 apaga */
    if (channel < 2U) {
        s = (s & ~EXI_SR_EXTINTMASK) | (v & EXI_SR_EXTINTMASK);
        s &= ~(v & EXI_SR_EXTINT);
    }
    if (channel == 0U) {
        s = (s & ~EXI_SR_ROMDIS) | (v & EXI_SR_ROMDIS);
    }
    const u32 old_cs = (s & EXI_SR_CS) >> EXI_SR_CS_SHIFT;
    const u32 new_cs = (v & EXI_SR_CS) >> EXI_SR_CS_SHIFT;
    const int changed = device_of(old_cs ^ new_cs);
    if (device_present(channel, changed) && changed == (int)EXI_CARD_DEVICE) {
        exi_card_set_cs(new_cs);
    } else if (device_present(channel, changed) && new_cs != 0U) {
        s_exi.ipl_command_bytes = 0; /* CEXIIPL::SetCS */
        s_exi.ipl_cursor = 0;
    }
    c->status = (s & ~EXI_SR_CS) | (new_cs << EXI_SR_CS_SHIFT);
    update_interrupts();
}

static void write_control(u32 channel, u32 v) {
    ExiChannel *c = &s_exi.ch[channel];
    c->control = v;
    if ((v & EXI_CR_TSTART) == 0U) {
        return;
    }
    const int device = device_of((c->status & EXI_SR_CS) >> EXI_SR_CS_SHIFT);
    if (device < 0) {
        return; /* Dolphin: sem aparelho selecionado, TSTART fica ligado */
    }
    run_transfer(channel, device);
    c->control &= ~EXI_CR_TSTART;
    if (device == (int)EXI_CARD_DEVICE && device_present(channel, device)) {
        return; /* UseDelayedTransferCompletion: o imediato não gera TCINT, o DMA gera quando o cartão termina */
    }
    exi_channel_transfer_complete(channel);
}

static u32 read_status(u32 channel) {
    ExiChannel *c = &s_exi.ch[channel];
    /* EXT: aparelho 0 do canal presente (canal 2: sempre 0). Só o slot A tem aparelho, e só com --memcard. */
    c->status &= ~EXI_SR_EXT;
    if (channel == 0U && exi_card_present()) {
        c->status |= EXI_SR_EXT;
    }
    return c->status;
}

u64 exi_read(u32 offset, u8 size) {
    const u32 channel = offset / EXI_CHANNEL_SIZE;
    if (size != 4U || (offset & 3U) != 0U || channel >= EXI_CHANNEL_COUNT) {
        bus_unhandled_read(BUS_CPU_ADDR(EXI_BASE + offset), size);
    }
    const ExiChannel *c = &s_exi.ch[channel];
    switch (offset % EXI_CHANNEL_SIZE) {
    case EXI_REG_STATUS:
        return read_status(channel);
    case EXI_REG_DMA_ADR:
        return c->dma_addr;
    case EXI_REG_DMA_LEN:
        return c->dma_len;
    case EXI_REG_CONTROL:
        return c->control;
    default:
        return c->imm;
    }
}

void exi_write(u32 offset, u64 value, u8 size) {
    const u32 channel = offset / EXI_CHANNEL_SIZE;
    if (size != 4U || (offset & 3U) != 0U || channel >= EXI_CHANNEL_COUNT) {
        bus_unhandled_write(BUS_CPU_ADDR(EXI_BASE + offset), value, size);
    }
    ExiChannel *c = &s_exi.ch[channel];
    const u32 v = (u32)value;
    switch (offset % EXI_CHANNEL_SIZE) {
    case EXI_REG_STATUS:
        write_status(channel, v);
        return;
    case EXI_REG_DMA_ADR:
        c->dma_addr = v;
        return;
    case EXI_REG_DMA_LEN:
        c->dma_len = v;
        return;
    case EXI_REG_CONTROL:
        write_control(channel, v);
        return;
    default:
        c->imm = v;
        return;
    }
}
