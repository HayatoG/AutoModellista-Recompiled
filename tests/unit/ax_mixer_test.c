/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lista de comandos do AX e mixagem (audio/ax_mixer.c), com listas e buffers montados à mão na MEM1: SETUP com valor
 * inicial e delta, OUTPUT (R antes de L, big-endian, saturado), troca dos AUX com a CPU, UPLOAD_LRS, DL_AND_VOL_MIX,
 * SET_OPPOSITE_LR, compressor (ataque e soltura) e CMD_MORE. Valores esperados pelas fórmulas de AX.cpp do Dolphin. */
#include "am/ax_mixer.h"
#include "am/mem.h"

#include "am_test.h"

#define LIST    0x00130000U /* físicos */
#define LIST2   0x00130400U
#define INIT    0x00131000U
#define BUF_A   0x00140000U
#define BUF_B   0x00142000U
#define BUF_C   0x00144000U
#define TABLE   0x00150000U
#define AX_HASH 0x07F88145U
#define N       AX_SAMPLES_PER_FRAME

static void w16(u32 addr, u16 v) {
    mem_write16(NULL, MEM_CACHED_BASE | addr, v);
}

static u16 r16(u32 addr) {
    return mem_read16(NULL, MEM_CACHED_BASE | addr);
}

static s32 r32(u32 addr) {
    return (s32)mem_read32(NULL, MEM_CACHED_BASE | addr);
}

static void w32(u32 addr, s32 v) {
    mem_write32(NULL, MEM_CACHED_BASE | addr, (u32)v);
}

/* Lista a partir de palavras; os endereços de 32 bits viram duas palavras (hi, lo). */
static void put_list(u32 at, const u16 *words, u32 count) {
    for (u32 i = 0; i < count; i++) {
        w16(at + (i * 2U), words[i]);
    }
}

#define HI(a) (u16)((a) >> 16)
#define LO(a) (u16)(a)

/* Tabela do SETUP: 9 x (hi, lo, delta). */
static void put_init(u32 channel, s32 value, s16 delta) {
    w16(INIT + (channel * 6U), (u16)((u32)value >> 16));
    w16(INIT + (channel * 6U) + 2U, (u16)value);
    w16(INIT + (channel * 6U) + 4U, (u16)delta);
}

static void clear_init(void) {
    for (u32 c = 0; c < AX_CHANNELS; c++) {
        put_init(c, 0, 0);
    }
}

TEST(setup_then_output_interleaves_r_l_and_saturates) {
    EXPECT_TRUE(mem_init());
    ax_mixer_reset();
    clear_init();
    put_init(AX_MAIN_L, 0x1000, 1);  /* 4096 + j */
    put_init(AX_MAIN_R, 0x10000, 0); /* 65536: satura em 32767 */
    put_init(AX_MAIN_S, 7, 0);
    const u16 list[] = {0x0000, HI(INIT), LO(INIT), 0x000E, HI(BUF_B), LO(BUF_B), HI(BUF_A), LO(BUF_A), 0x000F};
    put_list(LIST, list, sizeof(list) / sizeof(list[0]));
    const u64 clipped = ax_mixer_stats()->clipped;
    ax_mixer_command_list(LIST, sizeof(list) / sizeof(list[0]), AX_HASH);
    EXPECT_EQ(r16(BUF_A), 0x7FFFU);      /* R primeiro */
    EXPECT_EQ(r16(BUF_A + 2U), 0x1000U); /* depois L */
    EXPECT_EQ(r16(BUF_A + 4U), 0x7FFFU);
    EXPECT_EQ(r16(BUF_A + 6U), 0x1001U);
    EXPECT_EQ(r16(BUF_A + ((N - 1U) * 4U) + 2U), 0x1000U + N - 1U);
    EXPECT_TRUE(r32(BUF_B) == 7 && r32(BUF_B + ((N - 1U) * 4U)) == 7); /* surround como int */
    EXPECT_EQ(ax_mixer_stats()->clipped - clipped, N);
}

TEST(aux_round_trip_and_upload_lrs) {
    EXPECT_TRUE(mem_init());
    ax_mixer_reset();
    clear_init();
    put_init(AX_AUXA_L, 100, 0);
    for (u32 i = 0; i < N; i++) { /* o que a CPU devolve: L = 1, R = 2, S = 3 */
        w32(BUF_B + (i * 4U), 1);
        w32(BUF_B + ((N + i) * 4U), 2);
        w32(BUF_B + ((2U * N + i) * 4U), 3);
    }
    const u16 list[] = {0x0000,    HI(INIT),  LO(INIT), 0x0004,    HI(BUF_A), LO(BUF_A),
                        HI(BUF_B), LO(BUF_B), 0x0006,   HI(BUF_C), LO(BUF_C), 0x000F};
    put_list(LIST, list, sizeof(list) / sizeof(list[0]));
    ax_mixer_command_list(LIST, sizeof(list) / sizeof(list[0]), AX_HASH);
    EXPECT_TRUE(r32(BUF_A) == 100 && r32(BUF_A + (N * 4U)) == 0); /* AUX A L, depois R */
    EXPECT_TRUE(r32(BUF_C) == 1 && r32(BUF_C + (N * 4U)) == 2 && r32(BUF_C + (2U * N * 4U)) == 3);
    EXPECT_TRUE(ax_mixer_buffers()->ch[AX_AUXA_L][5] == 100); /* o AUX não é zerado depois de enviado */
}

TEST(download_with_volume_and_opposite_lr) {
    EXPECT_TRUE(mem_init());
    ax_mixer_reset();
    for (u32 i = 0; i < 3U * N; i++) {
        w32(BUF_A + (i * 4U), 1000);
    }
    w32(BUF_B, 300);
    const u16 list[] = {0x0001, HI(BUF_A), LO(BUF_A), 0x8000, 0x4000, 0x0000, 0x000F};
    put_list(LIST, list, sizeof(list) / sizeof(list[0]));
    ax_mixer_command_list(LIST, sizeof(list) / sizeof(list[0]), AX_HASH);
    const AxMixBuffers *b = ax_mixer_buffers();
    EXPECT_TRUE(b->ch[AX_MAIN_L][0] == 1000 && b->ch[AX_MAIN_S][N - 1U] == 1000);
    EXPECT_TRUE(b->ch[AX_AUXA_R][3] == 500 && b->ch[AX_AUXB_L][3] == 0);
    const u16 list2[] = {0x0011, HI(BUF_B), LO(BUF_B), 0x000F};
    put_list(LIST, list2, sizeof(list2) / sizeof(list2[0]));
    ax_mixer_command_list(LIST, sizeof(list2) / sizeof(list2[0]), AX_HASH);
    EXPECT_TRUE(b->ch[AX_MAIN_L][0] == -300 && b->ch[AX_MAIN_R][0] == 300 && b->ch[AX_MAIN_S][0] == 0);
}

TEST(compressor_attack_then_release) {
    EXPECT_TRUE(mem_init());
    ax_mixer_reset();
    for (u32 i = 0; i < N; i++) {
        w16(TABLE + (i * 2U), 0x4000);             /* rampa de ataque 0: 0,5 */
        w16(TABLE + ((12U * N) + i) * 2U, 0x2000); /* soltura 1 (entrada 11 + 1): 0,25 */
    }
    clear_init();
    put_init(AX_MAIN_L, 20000, 0);
    put_init(AX_MAIN_R, 100, 0);
    /* limiar 10000, 2 quadros de soltura */
    const u16 list[] = {0x0000, HI(INIT), LO(INIT), 0x0012, 10000, 2, HI(TABLE), LO(TABLE), 0x000F};
    put_list(LIST, list, sizeof(list) / sizeof(list[0]));
    ax_mixer_command_list(LIST, sizeof(list) / sizeof(list[0]), AX_HASH);
    const AxMixBuffers *b = ax_mixer_buffers();
    EXPECT_TRUE(b->ch[AX_MAIN_L][0] == 10000 && b->ch[AX_MAIN_R][0] == 50);
    put_init(AX_MAIN_L, 100, 0); /* abaixo do limiar: soltura com a posição 2 - 1 = 1 */
    ax_mixer_command_list(LIST, sizeof(list) / sizeof(list[0]), AX_HASH);
    EXPECT_TRUE(b->ch[AX_MAIN_L][0] == 25 && b->ch[AX_MAIN_R][7] == 25);
}

TEST(more_continues_in_another_list) {
    EXPECT_TRUE(mem_init());
    ax_mixer_reset();
    clear_init();
    put_init(AX_MAIN_R, -5, 0);
    const u16 list2[] = {0x0000, HI(INIT), LO(INIT), 0x000F};
    put_list(LIST2, list2, sizeof(list2) / sizeof(list2[0]));
    const u16 list[] = {0x000A, 0x000D, HI(LIST2), LO(LIST2), 4};
    put_list(LIST, list, sizeof(list) / sizeof(list[0]));
    const u64 commands = ax_mixer_stats()->commands;
    ax_mixer_command_list(LIST, sizeof(list) / sizeof(list[0]), AX_HASH);
    EXPECT_TRUE(ax_mixer_buffers()->ch[AX_MAIN_R][9] == -5);
    EXPECT_EQ(ax_mixer_stats()->commands - commands, 3U); /* UNK_0A, MORE, SETUP */
}

int main(void) {
    RUN_TEST(setup_then_output_interleaves_r_l_and_saturates);
    RUN_TEST(aux_round_trip_and_upload_lrs);
    RUN_TEST(download_with_volume_and_opposite_lr);
    RUN_TEST(compressor_attack_then_release);
    RUN_TEST(more_continues_in_another_list);
    mem_shutdown();
    return am_test_finish();
}
