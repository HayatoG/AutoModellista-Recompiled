/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/bus.h"

#include "am/log.h"

/* Blocos de registradores de hardware, endereço físico de 0x0C000000 (visto pela CPU em 0xCC000000).
 * Fonte: YAGCD cap. 5 e Core/HW/MMIO + Memmap.cpp do Dolphin. */
typedef struct BusRegion {
    u32 base;
    u32 size;
    const char *name;
} BusRegion;

static const BusRegion s_bus_regions[] = {
    {0x0C000000U, 0x1000U, "CP"}, {0x0C001000U, 0x1000U, "PE"},     {0x0C002000U, 0x1000U, "VI"},
    {0x0C003000U, 0x1000U, "PI"}, {0x0C004000U, 0x1000U, "MI"},     {0x0C005000U, 0x1000U, "DSP"},
    {0x0C006000U, 0x0400U, "DI"}, {0x0C006400U, 0x0400U, "SI"},     {0x0C006800U, 0x0400U, "EXI"},
    {0x0C006C00U, 0x0400U, "AI"}, {0x0C008000U, 0x1000U, "WGPipe"},
};

const char *bus_region_name(GuestAddr addr) {
    const u32 phys = addr & 0x3FFFFFFFU;
    for (size_t i = 0; i < sizeof(s_bus_regions) / sizeof(s_bus_regions[0]); i++) {
        if (phys >= s_bus_regions[i].base && phys - s_bus_regions[i].base < s_bus_regions[i].size) {
            return s_bus_regions[i].name;
        }
    }
    return "desconhecido";
}

u64 bus_read(GuestAddr addr, u8 size) {
    am_fatal("bus: leitura de %u byte(s) em 0x%08X (%s) sem dispositivo; implementar no marco do dispositivo", size,
             addr, bus_region_name(addr));
}

void bus_write(GuestAddr addr, u64 value, u8 size) {
    am_fatal("bus: escrita de %u byte(s) em 0x%08X (%s) valor=0x%llX sem dispositivo; implementar no marco do "
             "dispositivo",
             size, addr, bus_region_name(addr), (unsigned long long)value);
}
