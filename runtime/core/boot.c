/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/boot.h"

#include <string.h>

#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/ppc_sys.h"
#include "am/timebase.h"

/* Campos do cabeçalho do disco (boot.bin; YAGCD 13.1 "Disk header"). */
#define DISC_HEADER_SIZE  0x440U
#define DISC_FST_MAX_SIZE 0x42CU
/* bi2.bin (YAGCD 13.2 "Disk header information"). */
#define BI2_SIZE           0x2000U
#define BI2_DEBUG_MON_SIZE 0x00U
#define BI2_SIM_MEM_SIZE   0x04U
#define BI2_COUNTRY        0x18U
#define BI2_COUNTRY_PAL    2U

/* Globais em memória baixa (YAGCD 4.2.1.1 "OS globals"). */
#define LOW_DISC_ID        0x80000000U /* 0x20 bytes do cabeçalho do disco */
#define LOW_BOOT_MAGIC     0x80000020U
#define LOW_VERSION        0x80000024U
#define LOW_MEM_SIZE       0x80000028U
#define LOW_CONSOLE_TYPE   0x8000002CU
#define LOW_ARENA_LO       0x80000030U
#define LOW_ARENA_HI       0x80000034U
#define LOW_FST_ADDR       0x80000038U
#define LOW_FST_MAX        0x8000003CU
#define LOW_VI_MODE        0x800000CCU
#define LOW_ARAM_SIZE      0x800000D0U
#define LOW_DEBUG_MON_ADDR 0x800000ECU
#define LOW_SIM_MEM_SIZE   0x800000F0U
#define LOW_BI2_ADDR       0x800000F4U
#define LOW_BUS_CLOCK      0x800000F8U
#define LOW_CPU_CLOCK      0x800000FCU
#define LOW_TB_PRESET      0x800030D8U

/* Dolphin Boot_BS2Emu.cpp, SetupGCMemory. */
#define BOOT_MAGIC_BOOTROM 0x0D15EA5EU /* "booted from bootrom" (0xE5207C22 = JTAG) */
#define BUS_CLOCK_HZ       0x09A7EC80U /* 162 MHz */
#define CPU_CLOCK_HZ       0x1CF7C580U /* 486 MHz */
#define ARAM_SIZE_BYTES    0x01000000U
#define PPC_INSN_RFI       0x4C000064U /* tratadores padrão de DSI, FPU e syscall até o OSInit instalar os seus */

/* Registradores do BS2 do Dolphin (EmulatedBS2_GC, NTSC) e SetupMSR/SetupHID/SetupBAT. */
#define BS2_R1_NTSC  0x81566550U
#define BS2_R2_NTSC  0x81465CC0U
#define BS2_R13_NTSC 0x81465320U
#define BS2_R1_PAL   0x815EDCA8U
#define BS2_R2_PAL   0x814B5B20U
#define BS2_R13_PAL  0x814B4FC0U
#define BS2_MSR      0x00002032U /* FP | IR | DR | RI */
#define BS2_HID0     0x0011C464U
#define BS2_HID1     0x80000000U
#define BS2_HID2     0xE0000000U /* LSQE | WPE | PSE */

static u32 be32_at(const u8 *p, u32 off) {
    return ((u32)p[off] << 24) | ((u32)p[off + 1U] << 16) | ((u32)p[off + 2U] << 8) | (u32)p[off + 3U];
}

const char *boot_result_name(BootResult result) {
    switch (result) {
    case BOOT_OK:
        return "ok";
    case BOOT_ERR_BOOT_BIN:
        return "boot.bin menor que 0x440 bytes";
    case BOOT_ERR_BI2:
        return "bi2.bin não tem 0x2000 bytes";
    case BOOT_ERR_FST:
        return "fst.bin vazio ou maior que o máximo do cabeçalho";
    case BOOT_ERR_MEMSIZE:
        return "bi2 pede memória simulada diferente de 24 MiB";
    }
    return "?";
}

static BootResult validate(const BootFiles *f) {
    if (f->boot_bin == NULL || f->boot_bin_size < DISC_HEADER_SIZE) {
        return BOOT_ERR_BOOT_BIN;
    }
    if (f->bi2 == NULL || f->bi2_size != BI2_SIZE) {
        return BOOT_ERR_BI2;
    }
    const u32 fst_max = be32_at(f->boot_bin, DISC_FST_MAX_SIZE);
    if (f->fst == NULL || f->fst_size == 0U || f->fst_size > fst_max) {
        return BOOT_ERR_FST;
    }
    const u32 sim = be32_at(f->bi2, BI2_SIM_MEM_SIZE);
    if (sim != 0U && sim != MEM_MEM1_SIZE) {
        return BOOT_ERR_MEMSIZE;
    }
    return BOOT_OK;
}

/* O apploader põe o FST no topo da memória, alinhado a 32 bytes, e o bi2 logo abaixo (0x2000 bytes). Conferido no
 * dump do Dolphin: FST max 0x338 -> FST em 0x817FFCC0, bi2 em 0x817FDCC0 (docs/M4_BOOT.md §3.2). */
static void place_disc_tables(const BootFiles *f, BootLayout *out) {
    out->fst_max_size = be32_at(f->boot_bin, DISC_FST_MAX_SIZE);
    out->fst_addr = (MEM_CACHED_BASE + MEM_MEM1_SIZE - out->fst_max_size) & ~0x1FU;
    out->bi2_addr = out->fst_addr - BI2_SIZE;
    memcpy(mem_host_ptr(out->fst_addr, (u32)f->fst_size), f->fst, f->fst_size);
    memcpy(mem_host_ptr(out->bi2_addr, BI2_SIZE), f->bi2, BI2_SIZE);
}

BootResult boot_prepare_memory(const BootFiles *files, const BootConfig *config, BootLayout *out) {
    const BootResult valid = validate(files);
    if (valid != BOOT_OK) {
        return valid;
    }
    memset(out, 0, sizeof(*out));
    place_disc_tables(files, out);

    /* Dolphin DVDReadDiscID: os primeiros 0x20 bytes do disco (ID do jogo, disco, versão, streaming, magic). */
    memcpy(mem_host_ptr(LOW_DISC_ID, 0x20U), files->boot_bin, 0x20U);
    /* Dolphin SetupGCMemory. */
    mem_write32(NULL, LOW_BOOT_MAGIC, BOOT_MAGIC_BOOTROM);
    mem_write32(NULL, LOW_MEM_SIZE, MEM_MEM1_SIZE);
    mem_write32(NULL, LOW_CONSOLE_TYPE, config->console_type);
    const u32 country = be32_at(files->bi2, BI2_COUNTRY);
    mem_write32(NULL, LOW_VI_MODE, country == BI2_COUNTRY_PAL ? 1U : 0U); /* "Fake the VI Init of the IPL" */
    mem_write32(NULL, LOW_ARAM_SIZE, ARAM_SIZE_BYTES);
    mem_write32(NULL, LOW_BUS_CLOCK, BUS_CLOCK_HZ);
    mem_write32(NULL, LOW_CPU_CLOCK, CPU_CLOCK_HZ);
    mem_write32(NULL, 0x80000300U, PPC_INSN_RFI);
    mem_write32(NULL, 0x80000800U, PPC_INSN_RFI);
    mem_write32(NULL, 0x80000C00U, PPC_INSN_RFI);
    /* Dolphin PresetTimeBaseTicks: tempo emulado * 40,5 MHz. */
    out->time_base = config->rtc_seconds * TIMEBASE_TICKS_PER_SECOND;
    mem_write64(NULL, LOW_TB_PRESET, out->time_base);

    /* Apploader (stw em 0x81200748..0x81200768 e 0x81202C58..60 do apploader.img deste disco; valores conferidos no
     * dump do Dolphin): versão 1, arena alta = FST, FST e tamanho máximo, monitor de depuração no topo da memória
     * simulada, tamanho simulado e ponteiro do bi2. */
    const u32 sim = be32_at(files->bi2, BI2_SIM_MEM_SIZE) != 0U ? be32_at(files->bi2, BI2_SIM_MEM_SIZE) : MEM_MEM1_SIZE;
    mem_write32(NULL, LOW_VERSION, 1U);
    mem_write32(NULL, LOW_ARENA_LO, 0U);
    mem_write32(NULL, LOW_ARENA_HI, out->fst_addr);
    mem_write32(NULL, LOW_FST_ADDR, out->fst_addr);
    mem_write32(NULL, LOW_FST_MAX, out->fst_max_size);
    mem_write32(NULL, LOW_DEBUG_MON_ADDR, MEM_CACHED_BASE + sim - be32_at(files->bi2, BI2_DEBUG_MON_SIZE));
    mem_write32(NULL, LOW_SIM_MEM_SIZE, sim);
    mem_write32(NULL, LOW_BI2_ADDR, out->bi2_addr);
    return BOOT_OK;
}

void boot_prepare_cpu(CPUState *cpu, GuestAddr entry, const BootLayout *layout) {
    const bool pal = mem_read32(NULL, LOW_VI_MODE) == 1U;
    memset(cpu->gpr, 0, sizeof(cpu->gpr));
    cpu->gpr[1] = pal ? BS2_R1_PAL : BS2_R1_NTSC;
    cpu->gpr[2] = pal ? BS2_R2_PAL : BS2_R2_NTSC;
    cpu->gpr[13] = pal ? BS2_R13_PAL : BS2_R13_NTSC;
    /* iAppLoaderClose devolve o entry point em r3 e o Dolphin salta para ele (RunApploader: pc = gpr[3]). */
    cpu->gpr[3] = entry;
    cpu->pc = entry;
    cpu->lr = 0;
    cpu->ctr = 0;
    cpu->cr = 0;
    cpu->xer = 0;
    cpu->msr = BS2_MSR;
    cpu->hid2 = BS2_HID2;

    ppc_sys_set_spr(PPC_SPR_HID0, BS2_HID0);
    ppc_sys_set_spr(PPC_SPR_HID1, BS2_HID1);
    /* SetupBAT: IBAT0/DBAT0 = 0x80000000 cacheado, DBAT1 = 0xC0000000 sem cache, 256 MiB cada. */
    ppc_sys_set_spr(PPC_SPR_IBAT0U, 0x80001FFFU);
    ppc_sys_set_spr(PPC_SPR_IBAT0U + 1U, 0x00000002U);
    ppc_sys_set_spr(PPC_SPR_IBAT0U + 8U, 0x80001FFFU); /* DBAT0U = 536 */
    ppc_sys_set_spr(PPC_SPR_IBAT0U + 9U, 0x00000002U);
    ppc_sys_set_spr(PPC_SPR_IBAT0U + 10U, 0xC0001FFFU); /* DBAT1U = 538 */
    ppc_sys_set_spr(PPC_SPR_IBAT0U + 11U, 0x0000002AU);

    timebase_reset(layout->time_base);
    timebase_advance(BOOT_BS2_CYCLES);
}
