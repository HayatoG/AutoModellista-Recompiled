/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Microcódigos do DSP em HLE, no nível do protocolo de mailbox (Dolphin Core/HW/DSPHLE: DSPHLE.cpp, MailHandler.cpp,
 * UCodes/UCodes.cpp, ROM.cpp, INIT.cpp, AX.cpp; tag 2609a).
 *
 *   ROM   anuncia 0x8071FEED; recebe a tarefa de boot (0x80F3A001 endereço, 0x80F3A002 tamanho, 0x80F3B002 DRAM,
 *         0x80F3C002 destino na IRAM, 0x80F3D001 pc inicial), calcula o hash Ector da IRAM enviada e troca de
 *         microcódigo pelo hash (UCodeFactory);
 *   INIT  o microcódigo de inicialização do __OSInitAudioSystem (mail 0x80544348);
 *   AX    responde DSP_INIT ao subir, recebe listas de comando (0xBABE<tamanho> + endereço) e devolve DSP_YIELD com
 *         interrupção 2500 ciclos depois; atende MAIL_RESUME/NEW_UCODE/RESET/CONTINUE. Da lista, só PB_ADDR e PROCESS
 *         têm efeito: as vozes andam (endereço, laço, fim; audio/ax_voice.c), mas nada é mixado (M7).
 *
 * Microcódigo de hash desconhecido é am_fatal (o Dolphin força o AX com um aviso). */
#ifndef AM_DSP_HLE_H
#define AM_DSP_HLE_H

#include "am/types.h"

/* Hashes especiais do Dolphin (UCodes.h) e os de AX do GameCube que o UCodeFactory reconhece. */
#define DSP_HLE_UCODE_ROM  0x00000000U
#define DSP_HLE_UCODE_INIT 0x00000001U

typedef enum DspHleKind {
    DSP_HLE_KIND_ROM,
    DSP_HLE_KIND_INIT,
    DSP_HLE_KIND_AX,
} DspHleKind;

typedef struct DspHleStats {
    u32 ucode_crc;      /* microcódigo atual */
    u32 last_boot_addr; /* MRAM de onde veio a IRAM do último boot */
    u32 last_boot_len;  /* bytes */
    u64 boots;          /* trocas de microcódigo pela tarefa de boot */
    u64 mails_to_dsp;
    u64 mails_from_dsp;
    u64 command_lists; /* listas do AX recebidas */
    u64 command_words; /* palavras percorridas nelas */
} DspHleStats;

void dsp_hle_reset(void);
/* DSP_CONTROL: DSPHalt (mail só sai com o DSP rodando), DSPReset (volta à ROM), DSPInit 1 -> 0 (INIT). */
void dsp_hle_set_halted(bool halted);
void dsp_hle_boot_rom(void);
void dsp_hle_boot_init(void);

/* CPU -> DSP (DSP_WriteMailBoxLow já juntou as duas metades). */
void dsp_hle_mail_to_dsp(u32 mail);
/* DSP -> CPU: a leitura da metade baixa tira o mail da fila (MailHandler::ReadDSPMailboxLow). */
u16 dsp_hle_read_mail_high(void);
u16 dsp_hle_read_mail_low(void);

/* Interrupção INT_DSP pedida pelo microcódigo, com prazo em ciclos de CPU. Devolve true (e consome o pedido) se
 * algum pedido venceu até `now`. */
bool dsp_hle_take_interrupt(u64 now);
/* Ciclo do próximo pedido pendente, ou UINT64_MAX. */
u64 dsp_hle_next_event(void);

DspHleKind dsp_hle_kind(void);
const DspHleStats *dsp_hle_stats(void);

/* Hash do Dolphin (Common/Hash.cpp: HashEctor) sobre `size` bytes da MRAM em `addr`. Exposto para teste. */
u32 dsp_hle_hash_ector(GuestAddr addr, u32 size);

#endif /* AM_DSP_HLE_H */
