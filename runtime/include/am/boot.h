/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Estado inicial que o IPL (BS2) e o apploader deixam antes de saltar para o entry point do DOL.
 *
 * O port não roda o IPL nem o apploader (apploader.img é código do disco que só existe para carregar o DOL). Este
 * módulo reproduz o efeito deles, campo a campo, com a fonte de cada valor em boot.c. A referência executável é o
 * Dolphin 2609a parado no entry point (scripts/dolphin_trace.py boot-state); a comparação com o estado da runtime é
 * scripts/compare_boot_state.py (docs/M4_BOOT.md §3). */
#ifndef AM_BOOT_H
#define AM_BOOT_H

#include "am/types.h"

/* Arquivos de sys/ do disco extraído (dtk disc extract): boot.bin (cabeçalho, 0x440 bytes), bi2.bin (0x2000) e
 * fst.bin. Buffers do host, só lidos. */
/* M8: tempo emulado que o Dolphin já passou ao chegar no entry point: o BS2 em HLE roda o apploader na CPU emulada
 * (Boot_BS2Emu.cpp: RunFunction), com o TB e o VI contando desde o início. Medido, não derivado: é a diferença do
 * timebase na primeira interrupção do DVD (200059 tiques x 12 ciclos), com o resto do caminho igual (docs/M8_CORRIDA.md
 * §2). Começar a runtime com esse tempo deixa a fase do VI igual à do Dolphin. HIPÓTESE: o apploader leva sempre esse
 * tempo (não depende do host; o oráculo é determinístico). */
#define BOOT_BS2_CYCLES 2400708ULL

typedef struct BootFiles {
    const u8 *boot_bin;
    size_t boot_bin_size;
    const u8 *bi2;
    size_t bi2_size;
    const u8 *fst;
    size_t fst_size;
} BootFiles;

/* Tipo de console em 0x8000002C (YAGCD 4.2.1.1.2). O Dolphin usa o de devkit mais recente (Boot_BS2Emu.cpp:
 * SetupGCMemory, Core::ConsoleType::LatestDevkit); o varejo é 0x00000003. */
#define BOOT_CONSOLE_RETAIL        0x00000003U
#define BOOT_CONSOLE_LATEST_DEVKIT 0x10000006U

typedef struct BootConfig {
    u32 console_type;
    /* Segundos desde 2000-01-01 (época do GameCube) para o time base inicial. */
    u64 rtc_seconds;
} BootConfig;

typedef struct BootLayout {
    GuestAddr fst_addr;
    u32 fst_max_size;
    GuestAddr bi2_addr;
    u64 time_base; /* valor inicial do TB, também gravado em 0x800030D8 */
} BootLayout;

typedef enum BootResult {
    BOOT_OK,
    BOOT_ERR_BOOT_BIN, /* boot.bin menor que 0x440 */
    BOOT_ERR_BI2,      /* bi2.bin diferente de 0x2000 bytes */
    BOOT_ERR_FST,      /* fst.bin vazio ou maior que o tamanho máximo do cabeçalho */
    BOOT_ERR_MEMSIZE,  /* bi2 pede memória simulada diferente dos 24 MiB */
} BootResult;

const char *boot_result_name(BootResult result);

/* Escreve na MEM1 (exige mem_init): globais em 0x80000000..0x80000100, 0x800030D8, cópia de bi2 e do FST. */
BootResult boot_prepare_memory(const BootFiles *files, const BootConfig *config, BootLayout *out);

/* Registradores e SPRs como o BS2 do Dolphin os deixa (exige guest_cpu_create e ppc_sys_reset antes). Reinicia o
 * time base em layout->time_base. */
void boot_prepare_cpu(CPUState *cpu, GuestAddr entry, const BootLayout *layout);

#endif /* AM_BOOT_H */
