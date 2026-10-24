/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/bus.h"

#include "am/ai.h"
#include "am/cp.h"
#include "am/di.h"
#include "am/dsp.h"
#include "am/exi.h"
#include "am/gather_pipe.h"
#include "am/gx_fifo.h"
#include "am/log.h"
#include "am/mi.h"
#include "am/pe.h"
#include "am/pi.h"
#include "am/rel_map.h"
#include "am/si.h"
#include "am/vi.h"

typedef u64 (*BusReadFn)(u32 offset, u8 size);
typedef void (*BusWriteFn)(u32 offset, u64 value, u8 size);

/* Blocos de registradores de hardware, endereço físico de 0x0C000000 (visto pela CPU em 0xCC000000).
 * Fonte: YAGCD cap. 5 e Core/HW/MMIO + Memmap.cpp do Dolphin. Bloco sem read/write: ainda sem dispositivo. */
typedef struct BusRegion {
    u32 base;
    u32 size;
    const char *name;
    BusReadFn read;
    BusWriteFn write;
} BusRegion;

static const BusRegion s_bus_regions[] = {
    {0x0C000000U, 0x1000U, "CP", cp_read, cp_write},
    {0x0C001000U, 0x1000U, "PE", pe_read, pe_write},
    {0x0C002000U, 0x1000U, "VI", vi_read, vi_write},
    {0x0C003000U, 0x1000U, "PI", pi_read, pi_write},
    {0x0C004000U, 0x1000U, "MI", mi_read, mi_write},
    {0x0C005000U, 0x1000U, "DSP", dsp_read, dsp_write},
    {0x0C006000U, 0x0400U, "DI", di_read, di_write},
    {0x0C006400U, 0x0400U, "SI", si_read, si_write},
    {0x0C006800U, 0x0400U, "EXI", exi_read, exi_write},
    {0x0C006C00U, 0x0400U, "AI", ai_read, ai_write},
    {0x0C008000U, 0x1000U, "WGPipe", gather_pipe_read, gather_pipe_write},
};

#define BUS_REGION_COUNT (sizeof(s_bus_regions) / sizeof(s_bus_regions[0]))

/* O hardware só responde em 0xCC000000 (DBAT1 do BS2, sem cache). Endereço físico 0x0C... só existiria com a
 * tradução desligada, que a runtime não emula para dados: cai em "desconhecido" e é fatal. */
static const BusRegion *find_region(GuestAddr addr) {
    if ((addr >> 24) != 0xCCU) {
        return NULL;
    }
    const u32 phys = addr & 0x3FFFFFFFU;
    for (size_t i = 0; i < BUS_REGION_COUNT; i++) {
        if (phys >= s_bus_regions[i].base && phys - s_bus_regions[i].base < s_bus_regions[i].size) {
            return &s_bus_regions[i];
        }
    }
    return NULL;
}

const char *bus_region_name(GuestAddr addr) {
    const BusRegion *r = find_region(addr);
    const u32 phys = addr & 0x3FFFFFFFU;
    if (r == NULL && (addr >> 30) >= 2U && phys >= REL_MAP_PHYS_START && phys < REL_MAP_PHYS_END) {
        return "REL virtual sem módulo ligado"; /* core/rel_map.c: janela sem OSLink que a cubra */
    }
    return r != NULL ? r->name : "desconhecido";
}

void bus_unhandled_read(GuestAddr addr, u8 size) {
    am_fatal("bus: leitura de %u byte(s) em 0x%08X (%s) sem dispositivo; implementar no marco do dispositivo", size,
             addr, bus_region_name(addr));
}

void bus_unhandled_write(GuestAddr addr, u64 value, u8 size) {
    am_fatal("bus: escrita de %u byte(s) em 0x%08X (%s) valor=0x%llX sem dispositivo; implementar no marco do "
             "dispositivo",
             size, addr, bus_region_name(addr), (unsigned long long)value);
}

u64 bus_read(GuestAddr addr, u8 size) {
    const BusRegion *r = find_region(addr);
    if (r == NULL || r->read == NULL) {
        bus_unhandled_read(addr, size);
    }
    return r->read((addr & 0x3FFFFFFFU) - r->base, size);
}

void bus_write(GuestAddr addr, u64 value, u8 size) {
    const BusRegion *r = find_region(addr);
    if (r == NULL || r->write == NULL) {
        bus_unhandled_write(addr, value, size);
    }
    r->write((addr & 0x3FFFFFFFU) - r->base, value, size);
}

void bus_reset(void) {
    vi_reset(true);
    ai_reset();
    pi_reset();
    mi_reset();
    dsp_reset();
    di_reset();
    si_reset();
    exi_reset();
    cp_reset();
    pe_reset();
    gather_pipe_reset();
    const GxFifoSink sink = {pe_signal_finish, pe_signal_token};
    gx_fifo_reset(&sink);
}

void bus_update(void) {
    dsp_update();
    di_update();
    exi_update();
    vi_update();
    ai_update();
}
