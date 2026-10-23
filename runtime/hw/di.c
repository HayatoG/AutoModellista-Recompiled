/* SPDX-License-Identifier: GPL-3.0-or-later */
/* DVD Interface como o Dolphin 2609a a apresenta com disco inserido (Core/HW/DVD/DVDInterface.cpp). Até main o jogo só
 * configura as máscaras e manda um Inquiry (DVDInquiryAsync do OSInit); leitura de disco é M5 e é am_fatal aqui. */
#include "am/di.h"

#include <string.h>

#include "am/bus.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/pi.h"
#include "am/timebase.h"

#define DI_CR_TSTART 0x01U

/* DVDInterface.cpp: MINIMUM_COMMAND_LATENCY_US = 300; a 486 MHz são 145800 ciclos. */
#define DI_COMMAND_LATENCY_CYCLES (300ULL * 486ULL)

static struct {
    u32 sr;
    u32 cvr;
    u32 cmd[3];
    u32 mar;
    u32 length;
    u32 cr;
    u32 imm;
    bool busy;
    u64 done_cycle;
} s_di;

void di_reset(void) {
    memset(&s_di, 0, sizeof(s_di));
    /* DVDInterface::Init + disco inserido (SetDisc/UpdateCoverStatus: CVR = 0, tampa fechada). DICFG.CONFIG = 1:
     * descrambler da ROM de boot desligado. */
    s_di.cvr = 0;
}

static void update_interrupts(void) {
    const u32 sr = s_di.sr;
    const bool set = ((sr & DI_SR_DEINT) != 0U && (sr & DI_SR_DEINTMASK) != 0U) ||
                     ((sr & DI_SR_TCINT) != 0U && (sr & DI_SR_TCINTMASK) != 0U) ||
                     ((sr & DI_SR_BRKINT) != 0U && (sr & DI_SR_BRKINTMASK) != 0U) ||
                     ((s_di.cvr & DI_CVR_CVRINT) != 0U && (s_di.cvr & DI_CVR_CVRINTMASK) != 0U);
    pi_set_interrupt(PI_INT_DI, set);
}

/* FinishExecutingCommand(ReplyType::Interrupt, TCINT). */
void di_update(void) {
    if (!s_di.busy || timebase_cycles(NULL) < s_di.done_cycle) {
        return;
    }
    s_di.busy = false;
    s_di.mar += s_di.length;
    s_di.length = 0;
    if ((s_di.cr & DI_CR_TSTART) != 0U) {
        s_di.cr &= ~DI_CR_TSTART;
        s_di.sr |= DI_SR_TCINT;
        update_interrupts();
    }
}

static void execute_command(void) {
    const u32 command = s_di.cmd[0] >> 24;
    if (command != DI_CMD_INQUIRY) {
        am_fatal("DI: comando 0x%02X (0x%08X 0x%08X 0x%08X) não implementado (TODO(M5))", command, s_di.cmd[0],
                 s_di.cmd[1], s_di.cmd[2]);
    }
    /* Resposta do Inquiry que o Dolphin dá (drive de um Wii): revisão/código, data, versão. */
    const GuestAddr buf = MEM_CACHED_BASE | s_di.mar;
    mem_write32(NULL, buf, 0x00000002U);
    mem_write32(NULL, buf + 4U, 0x20060526U);
    mem_write32(NULL, buf + 8U, 0x41000000U);
    s_di.busy = true;
    s_di.done_cycle = timebase_cycles(NULL) + DI_COMMAND_LATENCY_CYCLES;
}

u64 di_read(u32 offset, u8 size) {
    if (size != 4U) {
        bus_unhandled_read(BUS_CPU_ADDR(DI_BASE + offset), size);
    }
    di_update();
    switch (offset) {
    case DI_REG_SR:
        return s_di.sr;
    case DI_REG_CVR:
        return s_di.cvr;
    case DI_REG_CMDBUF0:
    case DI_REG_CMDBUF1:
    case DI_REG_CMDBUF2:
        return s_di.cmd[(offset - DI_REG_CMDBUF0) / 4U];
    case DI_REG_MAR:
        return s_di.mar;
    case DI_REG_LENGTH:
        return s_di.length;
    case DI_REG_CR:
        return s_di.cr;
    case DI_REG_IMMBUF:
        return s_di.imm;
    case DI_REG_CFG:
        return 1U;
    default:
        bus_unhandled_read(BUS_CPU_ADDR(DI_BASE + offset), size);
    }
}

static void write_sr(u32 v) {
    const u32 copied = DI_SR_DEINTMASK | DI_SR_TCINTMASK | DI_SR_BRKINTMASK | DI_SR_BREAK;
    s_di.sr = (s_di.sr & ~copied) | (v & copied);
    s_di.sr &= ~(v & (DI_SR_DEINT | DI_SR_TCINT | DI_SR_BRKINT));
    if ((s_di.sr & DI_SR_BREAK) != 0U) {
        am_fatal("DI: BREAK pedido (abortar comando) não implementado (TODO(M5))");
    }
    update_interrupts();
}

void di_write(u32 offset, u64 value, u8 size) {
    if (size != 4U) {
        bus_unhandled_write(BUS_CPU_ADDR(DI_BASE + offset), value, size);
    }
    const u32 v = (u32)value;
    switch (offset) {
    case DI_REG_SR:
        write_sr(v);
        return;
    case DI_REG_CVR:
        s_di.cvr = (s_di.cvr & ~DI_CVR_CVRINTMASK) | (v & DI_CVR_CVRINTMASK);
        s_di.cvr &= ~(v & DI_CVR_CVRINT);
        update_interrupts();
        return;
    case DI_REG_CMDBUF0:
    case DI_REG_CMDBUF1:
    case DI_REG_CMDBUF2:
        s_di.cmd[(offset - DI_REG_CMDBUF0) / 4U] = v;
        return;
    case DI_REG_MAR:
        s_di.mar = v & ~0xFC00001FU;
        return;
    case DI_REG_LENGTH:
        s_di.length = v & ~0x1FU;
        return;
    case DI_REG_CR:
        s_di.cr = v & 7U;
        if ((s_di.cr & DI_CR_TSTART) != 0U) {
            execute_command();
        }
        return;
    case DI_REG_IMMBUF:
        s_di.imm = v;
        return;
    default:
        bus_unhandled_write(BUS_CPU_ADDR(DI_BASE + offset), value, size);
    }
}
