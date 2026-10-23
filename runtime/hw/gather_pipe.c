/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/gather_pipe.h"

#include <string.h>

#include "am/bus.h"
#include "am/cp.h"
#include "am/mem.h"
#include "am/pi.h"

/* Folga para uma escrita de 8 bytes com 31 pendentes. */
#define GATHER_PIPE_BUFFER (GATHER_PIPE_SIZE * 2U)

static struct {
    u8 buf[GATHER_PIPE_BUFFER];
    u32 count;
    u64 total;
} s_gp;

void gather_pipe_reset(void) {
    memset(&s_gp, 0, sizeof(s_gp));
}

u32 gather_pipe_pending(void) {
    return s_gp.count;
}

u64 gather_pipe_total_bytes(void) {
    return s_gp.total;
}

/* GPFifoManager::UpdateGatherPipe. */
static void flush(void) {
    u32 done = 0;
    while (s_gp.count - done >= GATHER_PIPE_SIZE) {
        const u32 wptr = pi_fifo_write_pointer();
        for (u32 i = 0; i < GATHER_PIPE_SIZE; i++) {
            mem_write8(NULL, MEM_CACHED_BASE | (wptr + i), s_gp.buf[done + i]);
        }
        pi_fifo_advance();
        cp_gather_pipe_burst(wptr, &s_gp.buf[done]);
        done += GATHER_PIPE_SIZE;
    }
    memmove(s_gp.buf, &s_gp.buf[done], s_gp.count - done);
    s_gp.count -= done;
}

/* Qualquer endereço do bloco de 0xCC008000 é o pipe; o dado entra em big-endian. */
void gather_pipe_write(u32 offset, u64 value, u8 size) {
    (void)offset;
    if (size != 1U && size != 2U && size != 4U && size != 8U) {
        bus_unhandled_write(BUS_CPU_ADDR(GATHER_PIPE_BASE + offset), value, size);
    }
    for (u32 i = size; i > 0U; i--) {
        s_gp.buf[s_gp.count + i - 1U] = (u8)value;
        value >>= 8;
    }
    s_gp.count += size;
    s_gp.total += size;
    flush();
}

u64 gather_pipe_read(u32 offset, u8 size) {
    bus_unhandled_read(BUS_CPU_ADDR(GATHER_PIPE_BASE + offset), size);
}
