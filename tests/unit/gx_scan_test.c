/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Leitura por cima do fluxo do GX pela thread da CPU (gx_fifo.c, scan_feed): o mesmo fluxo, alimentado de formas
 * diferentes, tem de disparar os mesmos eventos no mesmo byte. Um byte por vez usa só a máquina de estados por byte;
 * rajadas de 32 bytes (como o gather pipe) e pedaços irregulares usam também o caminho rápido do cabeçalho inteiro. */
#include <string.h>

#include "am/gx_fifo.h"
#include "am/log.h"
#include "am/platform.h"

#include "am_test.h"

#define STREAM_MAX 40000U
#define EVENT_MAX  1024U

typedef struct Event {
    u32 before; /* bytes entregues antes da chamada em que o evento saiu */
    u32 after;  /* bytes entregues até o fim dessa chamada */
    u16 value;  /* 0xFFFF = DrawDone; senão o valor do token */
    bool irq;
} Event;

static u8 s_stream[STREAM_MAX];
static u32 s_len;
static Event s_events[EVENT_MAX];
static u32 s_event_count;
static u32 s_before;
static u32 s_after;

static void on_done(void) {
    AM_ASSERT(s_event_count < EVENT_MAX, "eventos demais");
    s_events[s_event_count++] = (Event){s_before, s_after, 0xFFFFU, false};
}

static void on_token(u16 value, bool irq) {
    AM_ASSERT(s_event_count < EVENT_MAX, "eventos demais");
    s_events[s_event_count++] = (Event){s_before, s_after, value, irq};
}

static void put(const u8 *b, u32 n) {
    AM_ASSERT(s_len + n <= STREAM_MAX, "fluxo grande demais");
    memcpy(&s_stream[s_len], b, n);
    s_len += n;
}

/* CP de VCD/VAT (vértice de 17 bytes, como em gx_fifo_test.c) e depois comandos de todos os tamanhos de cabeçalho:
 * NOP, BP (5), XF (5 + palavras), desenho (3 + vértices), DrawDone e o par de tokens. O conteúdo dos vértices e das
 * palavras de XF é 0x61 (opcode de BP): se o leitor errar o tamanho de um comando, os eventos mudam de lugar. */
static void build_stream(void) {
    s_len = 0;
    static const u8 cp[] = {0x08, 0x50, 0x00, 0x00, 0x42, 0x00, 0x08, 0x60, 0x00,
                            0x00, 0x00, 0x01, 0x08, 0x70, 0x00, 0xE0, 0x00, 0x09};
    put(cp, sizeof(cp));
    for (u32 n = 0; n < 220U; n++) {
        const u8 nop = 0x00;
        put(&nop, 1);
        const u8 bp[] = {0x61, (u8)(0x40U + (n % 4U)), (u8)n, 0x12, 0x34}; /* registrador comum, sem evento */
        put(bp, sizeof(bp));
        const u32 words = 1U + (n % 3U);
        const u8 xf[] = {0x10, 0x00, (u8)(words - 1U), 0x10, 0x00};
        put(xf, sizeof(xf));
        u8 payload[12];
        memset(payload, 0x61, sizeof(payload));
        put(payload, words * 4U);
        const u32 verts = 1U + (n % 4U);
        const u8 draw[] = {0x90, 0x00, (u8)verts};
        put(draw, sizeof(draw));
        u8 vertex[17 * 4];
        memset(vertex, 0x61, sizeof(vertex));
        put(vertex, verts * 17U);
        if (n % 5U == 2U) {
            const u8 done[] = {0x61, 0x45, 0x00, 0x00, 0x02};
            put(done, sizeof(done));
        }
        if (n % 7U == 3U) {
            const u8 token[] = {0x61, 0x48, 0x00, (u8)(n >> 8), (u8)n, 0x61, 0x47, 0x00, (u8)(n >> 8), (u8)n};
            put(token, sizeof(token));
        }
    }
}

static void feed_chunks(u32 mode) {
    u32 seed = 12345U;
    u32 at = 0;
    while (at < s_len) {
        u32 n = 1U;
        if (mode == 32U) {
            n = 32U;
        } else if (mode == 0U) {
            seed = (seed * 1103515245U) + 12345U;
            n = 1U + ((seed >> 16) % 40U);
        }
        n = n < s_len - at ? n : s_len - at;
        s_before = at;
        s_after = at + n;
        gx_fifo_feed(&s_stream[at], n);
        at += n;
    }
}

static void run(u32 mode, GxFifoStats *out) {
    static const GxFifoThreadApi api = {platform_thread_start, platform_thread_join,  platform_lock_create,
                                        platform_lock_destroy, platform_lock,         platform_unlock,
                                        platform_cond_create,  platform_cond_destroy, platform_cond_wait,
                                        platform_cond_signal,  platform_ticks_ns};
    const GxFifoSink sink = {on_done, on_token};
    gx_fifo_reset(&sink);
    gx_fifo_set_render(false);
    s_event_count = 0;
    EXPECT_TRUE(gx_fifo_thread_start(&api, 0));
    feed_chunks(mode);
    gx_fifo_sync();
    *out = *gx_fifo_stats();
    gx_fifo_thread_stop();
}

TEST(events_fire_at_the_same_byte_with_any_chunking) {
    build_stream();
    GxFifoStats ref;
    run(1U, &ref); /* um byte por vez: a chamada do evento é o byte que o completa */
    const u32 expected = s_event_count;
    EXPECT_TRUE(expected > 50U);
    Event want[EVENT_MAX];
    memcpy(want, s_events, sizeof(Event) * expected);
    EXPECT_EQ(ref.draws, 220U);
    EXPECT_EQ(ref.bytes, s_len);
    static const u32 modes[] = {32U, 0U}; /* rajadas do gather pipe e pedaços irregulares de 1 a 40 bytes */
    for (u32 m = 0; m < 2U; m++) {
        GxFifoStats got;
        run(modes[m], &got);
        EXPECT_EQ(s_event_count, expected);
        for (u32 i = 0; i < expected && i < s_event_count; i++) {
            const u32 byte = want[i].before; /* índice do byte que completou o evento */
            EXPECT_TRUE(s_events[i].before <= byte && byte < s_events[i].after);
            EXPECT_EQ(s_events[i].value, want[i].value);
            EXPECT_EQ(s_events[i].irq, want[i].irq);
        }
        EXPECT_EQ(got.commands, ref.commands);
        EXPECT_EQ(got.draws, ref.draws);
        EXPECT_EQ(got.vertices, ref.vertices);
        EXPECT_EQ(got.bp_writes, ref.bp_writes);
        EXPECT_EQ(got.xf_words, ref.xf_words);
        EXPECT_EQ(got.draw_done, ref.draw_done);
        EXPECT_EQ(got.tokens, ref.tokens);
    }
}

int main(void) {
    RUN_TEST(events_fire_at_the_same_byte_with_any_chunking);
    return am_test_finish();
}
