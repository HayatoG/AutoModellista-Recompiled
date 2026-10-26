/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/idle_loop.h"

#include <string.h>

#include "am/guest_cpu.h"
#include "am/mem.h"

#define OP_LWZ   32U
#define OP_LBZ   34U
#define OP_LHZ   40U
#define OP_CMPLI 10U
#define OP_CMPI  11U
#define OP_BC    16U

typedef struct IdleLoop {
    GuestAddr addr;
    u8 rd;     /* registrador carregado e comparado */
    u8 ra;     /* base do load (0 = endereço absoluto) */
    u8 size;   /* bytes do load */
    u8 bit;    /* bit do CR0 que o bc testa: 0 LT, 1 GT, 2 EQ */
    bool sign; /* cmpi (com sinal) ou cmpli */
    bool when; /* o bc volta quando o bit vale isto */
    s16 disp;
    u32 imm;
} IdleLoop;

static struct {
    IdleLoop loops[IDLE_LOOP_MAX];
    u32 count;
} s_idle;

static u32 be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* lwz/lbz/lhz rD,d(rA); cmpli/cmpi cr0,rD,imm; bc 12|4,cr0[bit],.-8 (Gekko User's Manual, cap. 12). */
static bool decode(const u8 *code, GuestAddr addr, IdleLoop *out) {
    const u32 ld = be32(code), cmp = be32(code + 4), bc = be32(code + 8);
    const u32 op = ld >> 26;
    if (op != OP_LWZ && op != OP_LBZ && op != OP_LHZ) {
        return false;
    }
    const u8 rd = (u8)((ld >> 21) & 31U), ra = (u8)((ld >> 16) & 31U);
    if (ra == rd && ra != 0U) {
        return false; /* o endereço mudaria a cada volta */
    }
    const u32 cop = cmp >> 26;
    if ((cop != OP_CMPLI && cop != OP_CMPI) || ((cmp >> 21) & 31U) != 0U || ((cmp >> 16) & 31U) != rd) {
        return false; /* crfD = 0, L = 0, comparando o registrador carregado */
    }
    const u32 bo = (bc >> 21) & 0x1EU; /* sem o bit de previsão */
    const u32 bi = (bc >> 16) & 31U;
    if ((bc >> 26) != OP_BC || (bo != 12U && bo != 4U) || bi > 2U || (bc & 0xFFFFU) != 0xFFF8U) {
        return false; /* AA = LK = 0, destino = o load */
    }
    *out = (IdleLoop){.addr = addr,
                      .rd = rd,
                      .ra = ra,
                      .size = (u8)(op == OP_LWZ ? 4U : op == OP_LHZ ? 2U : 1U),
                      .bit = (u8)bi,
                      .sign = cop == OP_CMPI,
                      .when = bo == 12U,
                      .disp = (s16)(ld & 0xFFFFU),
                      .imm = cop == OP_CMPI ? (u32)(s32)(s16)(cmp & 0xFFFFU) : (cmp & 0xFFFFU)};
    return true;
}

void idle_loop_clear(void) {
    memset(&s_idle, 0, sizeof(s_idle));
}

u32 idle_loop_scan(const HookCodeRange *ranges, u32 count) {
    idle_loop_clear();
    for (u32 r = 0; r < count; r++) {
        for (u32 off = 0; off + 12U <= ranges[r].size && s_idle.count < IDLE_LOOP_MAX; off += 4U) {
            IdleLoop loop;
            if (decode(&ranges[r].data[off], ranges[r].addr + off, &loop)) {
                s_idle.loops[s_idle.count++] = loop;
            }
        }
    }
    return s_idle.count;
}

static const IdleLoop *find(GuestAddr pc) {
    for (u32 i = 0; i < s_idle.count; i++) {
        if (s_idle.loops[i].addr == pc) {
            return &s_idle.loops[i];
        }
    }
    return NULL;
}

bool idle_loop_spins(const CPUState *cpu) {
    const IdleLoop *l = find(cpu->pc);
    if (l == NULL) {
        return false;
    }
    const GuestAddr ea = (l->ra != 0U ? cpu->gpr[l->ra] : 0U) + (u32)(s32)l->disp;
    /* Só MEM1 (0x80... ou 0xC0..., fora da janela dos RELs): MMIO pode mudar com o tempo ou ter efeito na leitura. */
    if ((ea >> 30) < 2U || (ea & 0x3FFFFFFFU) > MEM_MEM1_SIZE - l->size || (ea & (l->size - 1U)) != 0U) {
        return false;
    }
    const u32 v = l->size == 4U ? mem_read32(NULL, ea) : l->size == 2U ? mem_read16(NULL, ea) : mem_read8(NULL, ea);
    if (cpu->gpr[l->rd] != v) {
        return false; /* a fatia mudaria o registrador: não é a repetição da anterior */
    }
    const bool lt = l->sign ? (s32)v < (s32)l->imm : v < l->imm;
    const bool gt = l->sign ? (s32)v > (s32)l->imm : v > l->imm;
    const bool bits[3] = {lt, gt, v == l->imm};
    return bits[l->bit] == l->when;
}
