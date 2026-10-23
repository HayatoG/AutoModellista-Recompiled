/* SPDX-License-Identifier: GPL-3.0-or-later */
/* DVD Interface como o Dolphin 2609a a apresenta com disco inserido (Core/HW/DVD/DVDInterface.cpp: ExecuteCommand).
 * Os dados vêm de core/disc.c (árvore extraída). Comandos: Inquiry, Read (setor e ID do disco), Seek, RequestError,
 * StopMotor e AudioBufferConfig; o resto, inclusive o streaming de áudio (0xE1/0xE2), é am_fatal com o comando.
 *
 * Tempo: o comando termina 300 µs depois (MINIMUM_COMMAND_LATENCY_US do Dolphin) mais o tempo de transferência a
 * DI_READ_BYTES_PER_SECOND. O Dolphin calcula busca e rotação do drive (DVDMath); aqui é um modelo fixo,
 * determinístico, e a diferença aparece só no instante em que o TCINT chega. Os dados vão para a memória no fim do
 * comando. */
#include "am/di.h"

#include <stdint.h>
#include <string.h>

#include "am/bus.h"
#include "am/disc.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/pi.h"
#include "am/timebase.h"

#define DI_CR_TSTART 0x01U

/* DVDInterface.cpp: MINIMUM_COMMAND_LATENCY_US = 300; a 486 MHz são 145800 ciclos. */
#define DI_COMMAND_LATENCY_CYCLES (300ULL * 486ULL)
/* Velocidade média de leitura do drive do GameCube (CAV, ~2-3 MiB/s); modelo fixo, ver o topo do arquivo. */
#define DI_READ_BYTES_PER_SECOND 3000000ULL
#define DI_CPU_HZ                486000000ULL
#define DI_BOUNCE_SIZE           0x10000U
#define DI_CMD_READ              0xA8U
#define DI_CMD_SEEK              0xABU
#define DI_CMD_REQUEST_ERROR     0xE0U
#define DI_CMD_AUDIO_STREAM      0xE1U
#define DI_CMD_AUDIO_STATUS      0xE2U
#define DI_CMD_STOP_MOTOR        0xE3U
#define DI_CMD_AUDIO_BUFFER      0xE4U

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
    bool read_pending; /* DMA do disco para fazer no fim do comando */
    u64 read_offset;
    u32 read_length;
    DiStats stats;
} s_di;

static u8 s_bounce[DI_BOUNCE_SIZE];
static DiReadObserver s_observer;

void di_set_read_observer(DiReadObserver observer) {
    s_observer = observer;
}

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

/* Copia do disco para a MRAM em `mar` (físico): direto na MEM1, ou byte a byte se o destino é a janela virtual dos
 * RELs (core/rel_map.c) que mem_write resolve. */
static void dma_from_disc(u64 offset, u32 mar, u32 length) {
    for (u32 done = 0; done < length;) {
        const u32 n = length - done < DI_BOUNCE_SIZE ? length - done : DI_BOUNCE_SIZE;
        if (!disc_read(offset + done, s_bounce, n)) {
            am_fatal("DI: falha de E/S lendo o disco em 0x%09llX (+%u bytes)", (unsigned long long)(offset + done), n);
        }
        const GuestAddr dst = MEM_CACHED_BASE | (mar + done);
        u8 *host = mem_host_ptr(dst, n);
        if (host != NULL) {
            memcpy(host, s_bounce, n);
        } else {
            for (u32 i = 0; i < n; i++) {
                mem_write8(NULL, dst + i, s_bounce[i]);
            }
        }
        done += n;
    }
}

/* FinishExecutingCommand(ReplyType::Interrupt, TCINT). */
void di_update(void) {
    if (!s_di.busy || timebase_cycles(NULL) < s_di.done_cycle) {
        return;
    }
    s_di.busy = false;
    if (s_di.read_pending) {
        s_di.read_pending = false;
        dma_from_disc(s_di.read_offset, s_di.mar, s_di.read_length);
    }
    s_di.mar += s_di.length;
    s_di.length = 0;
    if ((s_di.cr & DI_CR_TSTART) != 0U) {
        s_di.cr &= ~DI_CR_TSTART;
        s_di.sr |= DI_SR_TCINT;
        update_interrupts();
    }
}

static void finish_after(u64 cycles) {
    s_di.busy = true;
    s_di.done_cycle = timebase_cycles(NULL) + DI_COMMAND_LATENCY_CYCLES + cycles;
}

/* ExecuteReadCommand: DILENGTH tem de cobrir o pedido (o Dolphin corta e avisa; aqui é fatal para não esconder). */
static void start_read(u64 offset, u32 length) {
    if (!disc_is_open()) {
        am_fatal("DI: leitura de disco sem imagem aberta (--disc)");
    }
    if (s_di.length < length) {
        am_fatal("DI: leitura de 0x%X bytes com DILENGTH=0x%X", length, s_di.length);
    }
    u64 in_file = 0;
    const char *file = disc_region_at(offset, &in_file);
    s_di.stats.reads++;
    s_di.stats.read_bytes += length;
    AM_LOG_DEBUG("DI: leitura 0x%09llX +0x%X -> 0x%08X (%s +0x%llX)", (unsigned long long)offset, length, s_di.mar,
                 file != NULL ? file : "enchimento", (unsigned long long)in_file);
    if (s_observer != NULL) {
        s_observer(offset, length, s_di.mar);
    }
    s_di.read_pending = true;
    s_di.read_offset = offset;
    s_di.read_length = length;
    finish_after((u64)length * DI_CPU_HZ / DI_READ_BYTES_PER_SECOND);
}

static void execute_command(void) {
    const u32 command = s_di.cmd[0] >> 24;
    s_di.stats.commands++;
    switch (command) {
    case DI_CMD_INQUIRY: {
        /* Resposta do Inquiry que o Dolphin dá (drive de um Wii): revisão/código, data, versão. */
        const GuestAddr buf = MEM_CACHED_BASE | s_di.mar;
        mem_write32(NULL, buf, 0x00000002U);
        mem_write32(NULL, buf + 4U, 0x20060526U);
        mem_write32(NULL, buf + 8U, 0x41000000U);
        finish_after(0);
        return;
    }
    case DI_CMD_READ:
        if ((s_di.cmd[0] & 0xFFU) == 0x40U) {
            start_read(0, 0x20U); /* Read DiscID */
        } else if ((s_di.cmd[0] & 0xFFU) == 0x00U) {
            start_read((u64)s_di.cmd[1] << 2, s_di.cmd[2]);
        } else {
            am_fatal("DI: subcomando de leitura 0x%08X desconhecido (Dolphin só registra)", s_di.cmd[0]);
        }
        return;
    case DI_CMD_SEEK:
        s_di.stats.seeks++;
        finish_after(0); /* o Dolphin ignora a busca */
        return;
    case DI_CMD_REQUEST_ERROR:
        s_di.imm = 0; /* drive pronto (estado 0), sem erro */
        finish_after(0);
        return;
    case DI_CMD_STOP_MOTOR:
        s_di.stats.stop_motor++;
        finish_after(0);
        return;
    case DI_CMD_AUDIO_BUFFER:
        /* O IPL e o SDK ligam ou desligam o DTK pelo byte 0x08 do cabeçalho; sem áudio do disco, só registra. */
        s_di.stats.audio_buffer_config++;
        AM_LOG_INFO("DI: AudioBufferConfig 0x%08X (DTK %s, buffer %u)", s_di.cmd[0],
                    ((s_di.cmd[0] >> 16) & 1U) != 0U ? "ligado" : "desligado", s_di.cmd[0] & 0xFU);
        finish_after(0);
        return;
    case DI_CMD_AUDIO_STREAM:
    case DI_CMD_AUDIO_STATUS:
        am_fatal("DI: comando de streaming de áudio 0x%08X 0x%08X 0x%08X: o jogo usa DTK, não implementado (M7)",
                 s_di.cmd[0], s_di.cmd[1], s_di.cmd[2]);
    default:
        am_fatal("DI: comando 0x%02X (0x%08X 0x%08X 0x%08X) não implementado", command, s_di.cmd[0], s_di.cmd[1],
                 s_di.cmd[2]);
    }
}

const DiStats *di_stats(void) {
    return &s_di.stats;
}

u64 di_next_event_cycle(void) {
    return s_di.busy ? s_di.done_cycle : UINT64_MAX;
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
