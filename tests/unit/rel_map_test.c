/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Janela virtual dos RELs (core/rel_map.c), tradução no dispatcher, HLE de OSLinkFixed/OSUnlink (os/os_link.c) e
 * ganchos (core/hook.c), com um REL sintético de versão 3 (cabeçalho escrito aqui, nenhum byte do jogo). */
#include <string.h>

#include "am/dispatch.h"
#include "am/guest_cpu.h"
#include "am/hook.h"
#include "am/mem.h"
#include "am/os_link.h"
#include "am/rel_map.h"

#include "am_test.h"

#define VIRT       0x81800000U
#define REAL       0x80400000U
#define LINK_FIXED 0x80001100U
#define LINK       0x80001200U
#define UNLINK     0x80001300U
#define RET        0x80001404U

static u8 s_rel[0x180];

static void put32(u32 off, u32 v) {
    s_rel[off] = (u8)(v >> 24);
    s_rel[off + 1U] = (u8)(v >> 16);
    s_rel[off + 2U] = (u8)(v >> 8);
    s_rel[off + 3U] = (u8)v;
}

/* Módulo id 7: texto em 0x100 (0x40 bytes, executável), .bss de 0x20, prolog em texto+0x10, fixSize 0x140. */
static void build_rel(void) {
    memset(s_rel, 0, sizeof(s_rel));
    put32(0x00, 7);         /* id */
    put32(0x0C, 3);         /* seções */
    put32(0x10, 0x4C);      /* tabela de seções */
    put32(0x1C, 3);         /* versão */
    put32(0x20, 0x20);      /* .bss */
    put32(0x24, 0x140);     /* relocações */
    put32(0x28, 0x140);     /* imports */
    s_rel[0x30] = 1;        /* seção do prolog */
    put32(0x34, 0x10);      /* prolog */
    put32(0x40, 8);         /* alinhamento */
    put32(0x44, 8);         /* alinhamento do .bss */
    put32(0x48, 0x140);     /* fixSize */
    put32(0x4C + 8, 0x101); /* seção 1: texto em 0x100, executável */
    put32(0x4C + 12, 0x40);
    put32(0x4C + 20, 0x20); /* seção 2: .bss */
    for (u32 i = 0x100; i < 0x140; i++) {
        s_rel[i] = (u8)i;
    }
}

static bool load_rel(const char *name, const u8 **data, size_t *size, void *user) {
    (void)user;
    if (strcmp(name, "synth") != 0) {
        return false;
    }
    *data = s_rel;
    *size = sizeof(s_rel);
    return true;
}

static OsLinkEvent s_last_event;

static void on_event(const OsLinkEvent *e, void *user) {
    (void)user;
    s_last_event = *e;
}

static int s_rel_code_calls;
static GuestAddr s_rel_code_pc;

static void rel_code(CPUState *cpu) {
    s_rel_code_calls++;
    s_rel_code_pc = cpu->pc;
    cpu->pc = cpu->lr;
}

TEST(map_resolve_and_reverse) {
    rel_map_reset();
    EXPECT_TRUE(rel_map_add(1, VIRT, 0x1000, REAL));
    EXPECT_TRUE(!rel_map_add(2, VIRT + 0x800U, 0x1000, REAL + 0x10000U)); /* sobrepõe o módulo 1 */
    EXPECT_TRUE(!rel_map_add(3, 0x80500000U, 0x100, REAL));               /* fora da janela */
    u32 phys = 0;
    EXPECT_TRUE(rel_map_resolve(0x01800010U, 4, &phys));
    EXPECT_EQ(phys, 0x00400010U);
    EXPECT_TRUE(!rel_map_resolve(0x01800FFEU, 4, &phys)); /* atravessa o fim */
    GuestAddr v = 0;
    EXPECT_TRUE(rel_map_real_to_virtual(REAL + 0x20U, &v));
    EXPECT_EQ(v, VIRT + 0x20U);
    /* A memória enxerga os mesmos bytes pelos dois endereços, inclusive pelo espelho sem cache e por DMA (físico). */
    mem_write32(NULL, VIRT + 0x40U, 0x12345678U);
    EXPECT_EQ(mem_read32(NULL, REAL + 0x40U), 0x12345678U);
    EXPECT_EQ(mem_read32(NULL, 0xC1800040U), 0x12345678U);
    EXPECT_NOT_NULL(mem_host_ptr(VIRT, 0x1000));
    EXPECT_NULL(mem_host_ptr(VIRT, 0x1001));
    /* Substituir o mesmo id (o módulo foi recarregado em outro lugar). */
    EXPECT_TRUE(rel_map_add(1, VIRT, 0x1000, REAL + 0x2000U));
    EXPECT_EQ(rel_map_count(), 1U);
    EXPECT_EQ(mem_read32(NULL, VIRT + 0x40U), mem_read32(NULL, REAL + 0x2040U));
    EXPECT_TRUE(rel_map_shrink(1, 0x100));
    EXPECT_TRUE(!rel_map_resolve(0x01800200U, 4, &phys));
}

/* Bug do M5: menu.rel e game.rel ocupam o mesmo endereço real um depois do outro. Sem tirar o menu da tradução
 * real -> virtual no OSUnlink, um salto para o game.rel pelo endereço real caía no código do menu, e o OSUnlink do
 * menu01 recebia a base virtual do menu00. */
TEST(unlinked_module_leaves_reverse_map) {
    rel_map_reset();
    EXPECT_TRUE(rel_map_add(2, VIRT + 0x70000U, 0x1000, REAL));
    EXPECT_TRUE(rel_map_retire(2));
    EXPECT_TRUE(rel_map_add(1, VIRT, 0x1000, REAL));
    GuestAddr v = 0;
    EXPECT_TRUE(rel_map_real_to_virtual(REAL + 0x20U, &v));
    EXPECT_EQ(v, VIRT + 0x20U);
    u32 phys = 0; /* a janela virtual antiga ainda resolve (o SDK termina o Unlink por ela) */
    EXPECT_TRUE(rel_map_resolve(0x01870010U, 4, &phys));
    EXPECT_TRUE(rel_map_add(2, VIRT + 0x70000U, 0x1000, REAL + 0x4000U)); /* religado: volta à tradução */
    EXPECT_TRUE(rel_map_real_to_virtual(REAL + 0x4000U, &v));
    EXPECT_EQ(v, VIRT + 0x70000U);
    EXPECT_TRUE(!rel_map_retire(9));
}

TEST(link_fixed_rewrites_arguments_and_shrinks_on_return) {
    rel_map_reset();
    hook_clear();
    build_rel();
    for (u32 i = 0; i < sizeof(s_rel); i++) {
        mem_write8(NULL, REAL + i, s_rel[i]);
    }
    const OsLinkModule modules[1] = {{"synth", 7, VIRT + 0x100U, VIRT + 0x110U}};
    const OsLinkConfig cfg = {LINK, LINK_FIXED, UNLINK, modules, 1, load_rel, NULL, on_event};
    EXPECT_TRUE(os_link_init(&cfg));
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    cpu.pc = LINK_FIXED;
    cpu.gpr[3] = REAL;
    cpu.gpr[4] = REAL + 0x140U; /* o carregador do jogo passa o .bss logo depois do fixSize */
    cpu.gpr[1] = 0x80F00000U;
    cpu.lr = RET;
    EXPECT_EQ(hook_run(&cpu), HOOK_CONTINUE);
    EXPECT_EQ(cpu.gpr[3], VIRT);
    EXPECT_EQ(cpu.gpr[4], VIRT + 0x140U);
    EXPECT_EQ(os_link_event_count(), 1U);
    EXPECT_EQ(s_last_event.real, REAL);
    EXPECT_EQ(s_last_event.virt, VIRT);
    EXPECT_STREQ(s_last_event.name, "synth");
    /* O Link do SDK grava pelo endereço virtual; os bytes vão para o real. */
    mem_write32(NULL, VIRT + 0x34U, VIRT + 0x110U);
    EXPECT_EQ(mem_read32(NULL, REAL + 0x34U), VIRT + 0x110U);
    /* Retorno com TRUE: a janela encolhe para fixSize + .bss. */
    cpu.pc = RET;
    cpu.gpr[3] = 1;
    EXPECT_EQ(hook_run(&cpu), HOOK_CONTINUE);
    EXPECT_EQ(rel_map_at(0)->size, 0x160U);
    /* Salto pelo endereço real: o dispatcher traduz para o virtual. */
    dispatch_clear();
    EXPECT_EQ(dispatch_register(VIRT + 0x100U, VIRT + 0x140U, rel_code), DISPATCH_OK);
    cpu.pc = REAL + 0x104U;
    cpu.lr = 0x80001000U;
    dispatch_step(&cpu);
    EXPECT_EQ(s_rel_code_calls, 1);
    EXPECT_EQ(s_rel_code_pc, VIRT + 0x104U);
    /* OSUnlink(real) recebe o virtual. */
    cpu.pc = UNLINK;
    cpu.gpr[3] = REAL;
    EXPECT_EQ(hook_run(&cpu), HOOK_CONTINUE);
    EXPECT_EQ(cpu.gpr[3], VIRT);
    EXPECT_TRUE(s_last_event.unlink);
    EXPECT_EQ(os_link_event_count(), 2U);
}

static HookAction stop_hook(CPUState *cpu, void *user) {
    (void)cpu;
    (*(int *)user)++;
    return HOOK_STOP;
}

static void chunk_a(CPUState *cpu) {
    (void)cpu;
}

static void chunk_b(CPUState *cpu) {
    (void)cpu;
}

TEST(hooks_and_reachability) {
    hook_clear();
    dispatch_clear();
    int hits = 0;
    EXPECT_TRUE(hook_register(0x80002000U, "alvo", stop_hook, &hits));
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    cpu.pc = 0x80002004U;
    EXPECT_EQ(hook_run(&cpu), HOOK_CONTINUE);
    cpu.pc = 0x80002000U;
    EXPECT_EQ(hook_run(&cpu), HOOK_STOP);
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(hook_hits(0x80002000U), 1U);
    /* bl 0x80002000 de 0x80001F00 (+0x100): no mesmo trecho não passaria pelo chassi. */
    static const u8 s_code[4] = {0x48, 0x00, 0x01, 0x01};
    const HookCodeRange range = {0x80001F00U, 4, s_code};
    EXPECT_EQ(dispatch_register(0x80001000U, 0x80003000U, chunk_a), DISPATCH_OK);
    EXPECT_TRUE(!hook_check_reachable(&range, 1));
    dispatch_clear();
    EXPECT_EQ(dispatch_register(0x80001000U, 0x80002000U, chunk_a), DISPATCH_OK);
    EXPECT_EQ(dispatch_register(0x80002000U, 0x80003000U, chunk_b), DISPATCH_OK);
    EXPECT_TRUE(hook_check_reachable(&range, 1));
    dispatch_clear();
    hook_clear();
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(map_resolve_and_reverse);
    RUN_TEST(unlinked_module_leaves_reverse_map);
    RUN_TEST(link_fixed_rewrites_arguments_and_shrinks_on_return);
    RUN_TEST(hooks_and_reachability);
    mem_shutdown();
    return am_test_finish();
}
