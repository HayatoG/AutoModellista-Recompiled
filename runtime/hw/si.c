/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Serial Interface como no Dolphin 2609a (Core/HW/SI/SI.cpp, SI_DeviceGCController.cpp, SI_DeviceNull.cpp). A
 * transferência pelo buffer de comunicação termina na hora (RunSIBuffer devolve resposta não nula): TCINT sobe na
 * própria escrita de TSTART. Polling de controle (SIPOLL.ENx, WR) é do PADInit, depois de main: am_fatal. */
#include "am/si.h"

#include <string.h>

#include "am/bus.h"
#include "am/log.h"
#include "am/pi.h"

/* Comandos de buffer (Dolphin SI_Device.h: EBufferCommands). */
#define SI_CMD_STATUS 0x00U
#define SI_CMD_RESET  0xFFU

typedef struct SiChannel {
    u32 out;
    u32 in_hi;
    u32 in_lo;
} SiChannel;

static struct {
    SiChannel ch[SI_CHANNEL_COUNT];
    u32 poll;
    u32 comcsr;
    u32 status;
    u32 exi_clock;
    u8 buffer[SI_BUFFER_SIZE];
} s_si;

void si_reset(void) {
    memset(&s_si, 0, sizeof(s_si));
    s_si.poll = 492U << 16; /* SerialInterfaceManager::Init: m_poll.X = 492 */
}

static void update_interrupts(void) {
    /* RDST0..3 = bits 29, 21, 13, 5 (0x20000000 >> 8*canal). */
    if ((s_si.status & 0x20202020U) != 0U) {
        s_si.comcsr |= SI_CSR_RDSTINT;
    } else {
        s_si.comcsr &= ~SI_CSR_RDSTINT;
    }
    const u32 c = s_si.comcsr;
    pi_set_interrupt(PI_INT_SI, ((c & SI_CSR_RDSTINT) != 0U && (c & SI_CSR_RDSTINTMSK) != 0U) ||
                                    ((c & SI_CSR_TCINT) != 0U && (c & SI_CSR_TCINTMSK) != 0U));
}

/* ISIDevice::RunBuffer: tamanho da resposta, -1 = sem resposta (porta vazia). */
static int run_device(u32 channel) {
    if (channel != 0U) {
        return -1; /* CSIDevice_Null */
    }
    const u8 command = s_si.buffer[0];
    if (command == SI_CMD_STATUS || command == SI_CMD_RESET) {
        s_si.buffer[0] = (u8)(SI_TYPE_GC_CONTROLLER >> 24); /* CreateStatusResponse: 3 bytes big-endian */
        s_si.buffer[1] = (u8)(SI_TYPE_GC_CONTROLLER >> 16);
        s_si.buffer[2] = (u8)(SI_TYPE_GC_CONTROLLER >> 8);
        return 3;
    }
    am_fatal("SI: comando de controle 0x%02X na porta 1 não implementado (origem/leitura direta: TODO(M7))", command);
}

/* RunSIBuffer, para aparelhos que respondem na hora. */
static void run_buffer(void) {
    const u32 channel = (s_si.comcsr & SI_CSR_CHANNEL) >> 1;
    const int response = run_device(channel);
    s_si.comcsr &= ~(SI_CSR_TSTART | SI_CSR_COMERR);
    if (response < 0) {
        s_si.comcsr |= SI_CSR_COMERR;
        s_si.status |= 0x08000000U >> (channel * 8U); /* NOREPn */
    }
    s_si.comcsr |= SI_CSR_TCINT;
    update_interrupts();
}

static void write_comcsr(u32 v) {
    const u32 copied = SI_CSR_CHANNEL | SI_CSR_INLNGTH | SI_CSR_OUTLNGTH | SI_CSR_RDSTINTMSK | SI_CSR_TCINTMSK;
    s_si.comcsr = (s_si.comcsr & ~copied) | (v & copied);
    s_si.comcsr &= ~(v & (SI_CSR_RDSTINT | SI_CSR_TCINT));
    if ((v & SI_CSR_TSTART) != 0U) {
        s_si.comcsr |= SI_CSR_TSTART;
        run_buffer();
    }
    if ((s_si.comcsr & SI_CSR_TSTART) == 0U) {
        update_interrupts();
    }
}

static void write_status(u32 v) {
    /* UNRUN/OVRUN/COLL/NOREP de cada canal (bits 0-3 de cada byte): escrever 1 apaga. */
    s_si.status &= ~(v & 0x0F0F0F0FU);
    if ((v & 0x80000000U) != 0U) {
        am_fatal("SI: SISR.WR (copiar saída para os controles) não implementado (TODO(M7))");
    }
}

static u32 buffer_read(u32 at, u8 size) {
    u32 v = 0;
    for (u32 i = 0; i < size; i++) {
        v = (v << 8) | s_si.buffer[at + i];
    }
    return v;
}

static void buffer_write(u32 at, u32 v, u8 size) {
    for (u32 i = size; i > 0U; i--) {
        s_si.buffer[at + i - 1U] = (u8)v;
        v >>= 8;
    }
}

static u32 read_channel(u32 offset) {
    const u32 channel = offset / 0xCU;
    const u32 reg = offset % 0xCU;
    if (reg == 0U) {
        return s_si.ch[channel].out;
    }
    s_si.status &= ~(0x20000000U >> (channel * 8U)); /* ler IN apaga RDSTn */
    update_interrupts();
    return reg == 4U ? s_si.ch[channel].in_hi : s_si.ch[channel].in_lo;
}

u64 si_read(u32 offset, u8 size) {
    if (offset >= SI_REG_BUFFER && offset + size <= SI_REG_BUFFER + SI_BUFFER_SIZE && (size == 2U || size == 4U)) {
        return buffer_read(offset - SI_REG_BUFFER, size);
    }
    if (size != 4U || (offset & 3U) != 0U) {
        bus_unhandled_read(BUS_CPU_ADDR(SI_BASE + offset), size);
    }
    if (offset < SI_REG_POLL) {
        return read_channel(offset);
    }
    switch (offset) {
    case SI_REG_POLL:
        return s_si.poll;
    case SI_REG_COMCSR:
        return s_si.comcsr;
    case SI_REG_STATUS:
        return s_si.status;
    case SI_REG_EXICLK:
        return s_si.exi_clock;
    default:
        bus_unhandled_read(BUS_CPU_ADDR(SI_BASE + offset), size);
    }
}

void si_write(u32 offset, u64 value, u8 size) {
    const u32 v = (u32)value;
    if (offset >= SI_REG_BUFFER && offset + size <= SI_REG_BUFFER + SI_BUFFER_SIZE && (size == 2U || size == 4U)) {
        buffer_write(offset - SI_REG_BUFFER, v, size);
        return;
    }
    if (size != 4U || (offset & 3U) != 0U) {
        bus_unhandled_write(BUS_CPU_ADDR(SI_BASE + offset), value, size);
    }
    if (offset < SI_REG_POLL) {
        SiChannel *c = &s_si.ch[offset / 0xCU];
        const u32 reg = offset % 0xCU;
        if (reg == 0U) {
            c->out = v;
        } else if (reg == 4U) {
            c->in_hi = v;
        } else {
            c->in_lo = v;
        }
        return;
    }
    switch (offset) {
    case SI_REG_POLL:
        if ((v & 0xF0U) != 0U) {
            am_fatal("SI: polling de controle habilitado (SIPOLL=0x%08X) não implementado (TODO(M7))", v);
        }
        s_si.poll = v;
        return;
    case SI_REG_COMCSR:
        write_comcsr(v);
        return;
    case SI_REG_STATUS:
        write_status(v);
        return;
    case SI_REG_EXICLK:
        s_si.exi_clock = v;
        return;
    default:
        bus_unhandled_write(BUS_CPU_ADDR(SI_BASE + offset), value, size);
    }
}
