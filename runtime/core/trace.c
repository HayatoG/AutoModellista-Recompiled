/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/trace.h"

#include <stdio.h>

#include "am/dispatch.h"
#include "am/guest_cpu.h"
#include "am/log.h"
#include "am/mem.h"

static struct {
    FILE *file;
    u64 events;
    u64 bytes;
    u64 max_bytes;
} s_trace;

/* Cada evento é "XXXXXXXX XXXXXXXX XXXXXXXX\n". */
#define TRACE_LINE_BYTES 27U

/* Codificações do 750CL User's Manual (cap. 12): b (opcode 18), bc (16), bclr/bcctr (19, XO 16/528); LK = bit 0. */
bool trace_is_call(u32 insn, GuestAddr lr, GuestAddr pc, u32 ctr) {
    if ((insn & 1U) == 0U || lr < 4U) {
        return false;
    }
    const GuestAddr cia = lr - 4U;
    const u32 opcode = insn >> 26;
    const bool absolute = (insn & 2U) != 0U;
    if (opcode == 18U) {
        u32 li = insn & 0x03FFFFFCU;
        if ((li & 0x02000000U) != 0U) {
            li |= 0xFC000000U; /* extensão de sinal de 26 bits */
        }
        return (absolute ? li : cia + li) == pc;
    }
    if (opcode == 16U) {
        u32 bd = insn & 0xFFFCU;
        if ((bd & 0x8000U) != 0U) {
            bd |= 0xFFFF0000U;
        }
        return (absolute ? bd : cia + bd) == pc;
    }
    if (opcode == 19U) {
        const u32 xo = (insn >> 1) & 0x3FFU;
        if (xo == 528U) {
            return (ctr & ~3U) == pc;
        }
        /* bclrl: o alvo era o LR antigo, já sobrescrito. Um blr que volta exatamente para LR (retorno de quem foi
         * chamado por bclrl) não é chamada. */
        return xo == 16U && pc != lr;
    }
    return false;
}

bool trace_open(const char *path, const char *extra, u64 max_bytes) {
    trace_close();
    s_trace.file = fopen(path, "w");
    if (s_trace.file == NULL) {
        return false;
    }
    s_trace.events = 0;
    s_trace.bytes = 0;
    s_trace.max_bytes = max_bytes != 0U ? max_bytes : TRACE_DEFAULT_MAX_BYTES;
    (void)fprintf(s_trace.file, "# am-trace v1 source=runtime kind=calls%s%s\n", extra != NULL ? " " : "",
                  extra != NULL ? extra : "");
    return true;
}

void trace_close(void) {
    if (s_trace.file != NULL) {
        (void)fclose(s_trace.file);
        s_trace.file = NULL;
    }
}

bool trace_write_chunks(const char *path) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return false;
    }
    const DispatchEntry *e = dispatch_entries();
    for (size_t i = 0; i < dispatch_count(); i++) {
        (void)fprintf(f, "%08X %08X\n", e[i].start, e[i].end);
    }
    return fclose(f) == 0;
}

bool trace_is_open(void) {
    return s_trace.file != NULL;
}

bool trace_on_dispatch(const CPUState *cpu) {
    if (s_trace.file == NULL || cpu->lr < 4U) {
        return false;
    }
    /* LR fora da MEM1 (0 no __start, vetores físicos) não aponta para um bl; ler via barramento seria fatal. */
    if (mem_host_ptr(cpu->lr - 4U, 4U) == NULL) {
        return false;
    }
    const u32 insn = mem_read32(NULL, cpu->lr - 4U);
    if (!trace_is_call(insn, cpu->lr, cpu->pc, cpu->ctr)) {
        return false;
    }
    if (s_trace.bytes + TRACE_LINE_BYTES > s_trace.max_bytes) {
        trace_close();
        am_fatal("trace: limite de %llu bytes atingido em %llu chamadas (pc=0x%08X); laço sem fim?",
                 (unsigned long long)s_trace.max_bytes, (unsigned long long)s_trace.events, cpu->pc);
    }
    (void)fprintf(s_trace.file, "%08X %08X %08X\n", cpu->pc, cpu->lr, cpu->ctr);
    s_trace.bytes += TRACE_LINE_BYTES;
    s_trace.events++;
    return true;
}

u64 trace_event_count(void) {
    return s_trace.events;
}
