/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Serial Interface como no Dolphin 2609a (Core/HW/SI/SI.cpp, SI_DeviceGCController.cpp, SI_DeviceNull.cpp). A
 * transferência pelo buffer de comunicação termina na hora (RunSIBuffer devolve resposta não nula): TCINT sobe na
 * própria escrita de TSTART. O polling (UpdateDevices) é disparado pelo VI nas meias-linhas agendadas. O controle
 * da porta 1 está conectado; o estado (botões, sticks, gatilhos) vem de si_set_pad (roteiro de teste ou teclado/gamepad
 * da janela, M7). */
#include "am/si.h"

#include <string.h>

#include "am/bus.h"
#include "am/log.h"
#include "am/pi.h"

/* Comandos de buffer (Dolphin SI_Device.h: EBufferCommands) e direto (EDirectCommands). */
#define SI_CMD_STATUS      0x00U
#define SI_CMD_DIRECT      0x40U
#define SI_CMD_ORIGIN      0x41U
#define SI_CMD_RECALIBRATE 0x42U
#define SI_CMD_RESET       0xFFU
#define SI_DIRECT_WRITE    0x40U
/* SOrigin: botões (2), sticks (4), gatilhos (2), 2 desconhecidos. */
#define SI_ORIGIN_SIZE 10U

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
    u32 pad_mode; /* m_mode do controle (comando direto 0x40 com poll = 0) */
    SiPadState pad;
    u64 polls;
} s_si;

SiPadState si_pad_neutral(void) {
    return (SiPadState){.stick_x = SI_PAD_STICK_CENTER,
                        .stick_y = SI_PAD_STICK_CENTER,
                        .substick_x = SI_PAD_STICK_CENTER,
                        .substick_y = SI_PAD_STICK_CENTER};
}

void si_reset(void) {
    memset(&s_si, 0, sizeof(s_si));
    s_si.pad = si_pad_neutral();
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

/* CSIDevice_GCController::GetData + MapPadStatus: botões (com PAD_USE_ORIGIN) e stick principal na palavra alta; a
 * baixa muda com o modo (analogA/B não existem no controle padrão e ficam 0). */
void si_pad_encode(const SiPadState *pad, u32 mode, u32 *hi, u32 *lo) {
    const u32 tl = pad->trigger_l;
    const u32 tr = pad->trigger_r;
    const u32 cx = pad->substick_x;
    const u32 cy = pad->substick_y;
    *hi = (u32)pad->stick_y | ((u32)pad->stick_x << 8) | ((u32)(pad->buttons | 0x0080U) << 16);
    switch (mode) {
    case 0U:
    case 5U:
    case 6U:
    case 7U:
        *lo = ((tr >> 4) << 8) | ((tl >> 4) << 12) | (cy << 16) | (cx << 24);
        return;
    case 1U:
        *lo = (tr << 8) | (tl << 16) | ((cy >> 4) << 24) | ((cx >> 4) << 28);
        return;
    case 2U:
        *lo = ((tr >> 4) << 16) | ((tl >> 4) << 20) | ((cy >> 4) << 24) | ((cx >> 4) << 28);
        return;
    case 3U:
        *lo = tr | (tl << 8) | (cy << 16) | (cx << 24);
        return;
    case 4U:
        *lo = (cy << 16) | (cx << 24);
        return;
    default:
        am_fatal("SI: controle no modo %u (o Dolphin só conhece 0 a 7)", mode);
    }
}

static void pad_get_data(u32 *hi, u32 *lo) {
    si_pad_encode(&s_si.pad, s_si.pad_mode, hi, lo);
}

/* ISIDevice::RunBuffer: tamanho da resposta, -1 = sem resposta (porta vazia). */
static int run_device(u32 channel) {
    if (channel != 0U) {
        return -1; /* CSIDevice_Null */
    }
    const u8 command = s_si.buffer[0];
    switch (command) {
    case SI_CMD_STATUS:
    case SI_CMD_RESET:
        s_si.buffer[0] = (u8)(SI_TYPE_GC_CONTROLLER >> 24); /* CreateStatusResponse: 3 bytes big-endian */
        s_si.buffer[1] = (u8)(SI_TYPE_GC_CONTROLLER >> 16);
        s_si.buffer[2] = (u8)(SI_TYPE_GC_CONTROLLER >> 8);
        return 3;
    case SI_CMD_DIRECT: {
        u32 hi = 0;
        u32 lo = 0;
        pad_get_data(&hi, &lo);
        for (u32 i = 0; i < 4U; i++) {
            s_si.buffer[i] = (u8)(hi >> (24U - (i * 8U)));
            s_si.buffer[i + 4U] = (u8)(lo >> (24U - (i * 8U)));
        }
        return 8;
    }
    case SI_CMD_ORIGIN:
    case SI_CMD_RECALIBRATE:
        /* Origem centrada do construtor de CSIDevice_GCController: botões 0, sticks 0x80, gatilhos 0. */
        memset(s_si.buffer, 0, SI_ORIGIN_SIZE);
        s_si.buffer[2] = 0x80U;
        s_si.buffer[3] = 0x80U;
        s_si.buffer[4] = 0x80U;
        s_si.buffer[5] = 0x80U;
        return (int)SI_ORIGIN_SIZE;
    default:
        am_fatal("SI: comando 0x%02X para o controle da porta 1 desconhecido (Dolphin: PanicAlert)", command);
    }
}

void si_update_devices(void) {
    s_si.polls++;
    for (u32 i = 0; i < SI_CHANNEL_COUNT; i++) {
        SiChannel *c = &s_si.ch[i];
        const u32 errlatch = c->in_hi & 0x40000000U;
        if (i == 0U) {
            pad_get_data(&c->in_hi, &c->in_lo);
            c->in_hi = (c->in_hi & ~0x40000000U) | errlatch;
            s_si.status |= 0x20000000U >> (i * 8U); /* RDSTn */
        } else {
            s_si.status |= 0x08000000U >> (i * 8U); /* SetNoResponse: NOREPn */
            c->in_hi |= 0xC0000000U;                /* ERRSTAT e ERRLATCH */
        }
    }
    update_interrupts();
}

void si_set_pad(const SiPadState *pad) {
    s_si.pad = *pad;
}

u32 si_poll_x_lines(void) {
    return (s_si.poll >> 16) & 0x3FFU;
}

u64 si_poll_count(void) {
    return s_si.polls;
}

/* CSIDevice_GCController::SendCommand (escrita de SISR.WR): só o comando direto 0x40 (rumble + modo). */
static void send_command(u32 channel, u32 out, bool poll_enabled) {
    if (channel != 0U) {
        return; /* CSIDevice_Null::SendCommand não faz nada */
    }
    const u32 command = (out >> 16) & 0xFFU;
    if (command == SI_DIRECT_WRITE) {
        if (!poll_enabled) {
            s_si.pad_mode = out & 0xFFU;
            AM_LOG_DEBUG("SI: controle 1 no modo %u (rumble %u)", s_si.pad_mode, (out >> 8) & 0xFFU);
        }
        return;
    }
    if (command != 0U) {
        am_fatal("SI: comando direto 0x%08X desconhecido para o controle 1 (Dolphin: PanicAlert)", out);
    }
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
        /* SIPOLL.EN0..3 = bits 7..4. */
        for (u32 i = 0; i < SI_CHANNEL_COUNT; i++) {
            send_command(i, s_si.ch[i].out, ((s_si.poll >> (7U - i)) & 1U) != 0U);
        }
        s_si.status &= ~0x90101010U; /* WR (bit 31) e WRST0..3 (bits 28, 20, 12, 4) */
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
