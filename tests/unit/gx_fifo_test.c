/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Leitor do fluxo do GX (gfx/gx_fifo.c) e o caminho gather pipe -> FIFO da CPU (PI) -> CP -> PE. */
#include <string.h>

#include "am/bus.h"
#include "am/cp.h"
#include "am/gather_pipe.h"
#include "am/guest_cpu.h"
#include "am/gx_fifo.h"
#include "am/mem.h"
#include "am/pe.h"
#include "am/pi.h"

#include "am_test.h"

static u32 s_done;
static u16 s_token;
static bool s_token_irq;

static void on_done(void) {
    s_done++;
}

static void on_token(u16 value, bool irq) {
    s_token = value;
    s_token_irq = irq;
}

static void feed_bytes(const u8 *b, u32 n) {
    for (u32 i = 0; i < n; i++) {
        gx_fifo_feed(&b[i], 1); /* um byte por vez: comandos atravessam chamadas */
    }
}

TEST(vertex_sizes_from_vcd_and_vat) {
    const GxFifoSink sink = {on_done, on_token};
    gx_fifo_reset(&sink);
    /* VCD_LO: posição direta (bits 9-10 = 1), cor0 índice de 8 bits (bits 13-14 = 2); VCD_HI: tex0 direta.
     * VAT_A[0]: posição xyz f32 (elems 1, formato 4), tex0 st s16 (elems 1, formato 3). */
    const u8 cmds[] = {0x08, 0x50, 0x00, 0x00, 0x42, 0x00, 0x08, 0x60, 0x00,
                       0x00, 0x00, 0x01, 0x08, 0x70, 0x00, 0xE0, 0x00, 0x09};
    feed_bytes(cmds, sizeof(cmds));
    EXPECT_EQ(gx_fifo_vertex_size(0), 12U + 1U + 4U);
    /* Desenho de 3 vértices (triângulos, VAT 0), depois BP de fim de desenho e token com interrupção. */
    u8 draw[3 + (3 * 17)] = {0x90, 0x00, 0x03};
    memset(&draw[3], 0x61, sizeof(draw) - 3U); /* conteúdo dos vértices não pode ser lido como comando */
    feed_bytes(draw, sizeof(draw));
    const u8 bp[] = {0x61, 0x45, 0x00, 0x00, 0x02, 0x61, 0x48, 0x00, 0x12, 0x34, 0x00};
    feed_bytes(bp, sizeof(bp));
    EXPECT_EQ(gx_fifo_stats()->draws, 1U);
    EXPECT_EQ(gx_fifo_stats()->vertices, 3U);
    EXPECT_EQ(s_done, 1U);
    EXPECT_EQ(s_token, 0x1234U);
    EXPECT_TRUE(s_token_irq);
    EXPECT_EQ(gx_fifo_stats()->commands, 7U); /* 3 CP + desenho + 2 BP + NOP */
    /* XF de 2 palavras: 0x10, cabeçalho com (n-1) nos bits 16-19, 8 bytes de dados. */
    const u8 xf[] = {0x10, 0x00, 0x01, 0x10, 0x00, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61};
    feed_bytes(xf, sizeof(xf));
    EXPECT_EQ(gx_fifo_stats()->xf_words, 2U);
    EXPECT_EQ(gx_fifo_stats()->bp_writes, 2U);
}

TEST(display_list_is_parsed_from_memory) {
    const GxFifoSink sink = {on_done, on_token};
    gx_fifo_reset(&sink);
    s_done = 0;
    const u8 dl[32] = {0x61, 0x45, 0x00, 0x00, 0x02}; /* resto: NOPs */
    for (u32 i = 0; i < sizeof(dl); i++) {
        mem_write8(NULL, 0x80100000U + i, dl[i]);
    }
    const u8 call[] = {0x40, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20};
    feed_bytes(call, sizeof(call));
    EXPECT_EQ(gx_fifo_stats()->display_lists, 1U);
    EXPECT_EQ(s_done, 1U);
}

TEST(gather_pipe_to_pe_finish_interrupt) {
    bus_reset();
    pi_write(PI_REG_FIFO_BASE, 0x00200000U, 4);
    pi_write(PI_REG_FIFO_END, 0x00200020U, 4); /* dois blocos: o terceiro volta à base */
    pi_write(PI_REG_FIFO_WPTR, 0x00200000U, 4);
    bus_write(0xCC000020U, 0x0000U, 2); /* CP_FIFO_BASE LO/HI */
    bus_write(0xCC000022U, 0x0020U, 2);
    bus_write(0xCC000024U, 0x0020U, 2); /* CP_FIFO_END */
    bus_write(0xCC000026U, 0x0020U, 2);
    bus_write(0xCC000034U, 0x0000U, 2); /* CP_FIFO_WPTR */
    bus_write(0xCC000036U, 0x0020U, 2);
    bus_write(0xCC000002U, CP_CR_READ_ENABLE | CP_CR_LINK, 2);
    bus_write(0xCC00100AU, PE_CTRL_FINISH_ENABLE, 2);
    pi_write(PI_REG_INTMR, PI_INT_PE_FINISH, 4);
    bus_write(0xCC008000U, 0x61U, 1); /* GXSetDrawDone: BP 0x45 = 2 */
    bus_write(0xCC008000U, 0x45000002U, 4);
    EXPECT_EQ(gather_pipe_pending(), 5U);
    EXPECT_TRUE(!pi_external_pending()); /* só 5 bytes: ainda no pipe */
    for (u32 i = 0; i < 27U; i++) {
        bus_write(0xCC008000U, 0, 1); /* GXFlush completa o bloco com NOPs */
    }
    EXPECT_EQ(gather_pipe_pending(), 0U);
    EXPECT_EQ(mem_read8(NULL, 0x80200000U), 0x61U); /* o bloco foi para a FIFO na memória */
    EXPECT_EQ(pi_fifo_write_pointer(), 0x00200020U);
    EXPECT_TRUE(pi_external_pending());
    EXPECT_EQ(bus_read(0xCC00100AU, 2) & PE_CTRL_FINISH, PE_CTRL_FINISH);
    bus_write(0xCC00100AU, PE_CTRL_FINISH_ENABLE | PE_CTRL_FINISH, 2); /* 1 apaga */
    EXPECT_TRUE(!pi_external_pending());
    for (u32 i = 0; i < 64U; i++) {
        bus_write(0xCC008000U, 0, 1);
    }
    EXPECT_EQ(pi_fifo_write_pointer(), 0x00200020U); /* 0x200020 (fim) -> base -> 0x200020 */
    EXPECT_EQ(cp_bursts(), 3U);
    EXPECT_EQ(bus_read(0xCC000000U, 2) & (CP_SR_READ_IDLE | CP_SR_COMMAND_IDLE), 0x0CU);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(vertex_sizes_from_vcd_and_vat);
    RUN_TEST(display_list_is_parsed_from_memory);
    RUN_TEST(gather_pipe_to_pe_finish_interrupt);
    mem_shutdown();
    return am_test_finish();
}
