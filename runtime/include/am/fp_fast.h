/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ponto flutuante do C gerado em linha (M9 parte 6). O cpu.c do DolRecomp emula o FPSCR do Gekko inteiro em cada
 * fadds/fmuls/fmadds/fcmp/fctiwz, por uma chamada cheia de ramos: ~14% do tempo da corrida, mais com mais carros. Este
 * header entra no lugar do cpu/cpu.h no C gerado (DOLRECOMP_CPU_HEADER, runtime/cmake/AmRecomp.cmake) e troca essas
 * chamadas por um caminho curto para o caso comum: operandos e resultado finitos, sem divisão por zero (com NI, sem
 * resultado subnormal). Nele o resultado e cada bit do FPSCR e do CR são os mesmos do cpu.c (as funções abaixo repetem a sequência de
 * binary_single, binary_double, ppc_fma, ppc_fcmp e ppc_fctiw para esse caso). Fora dele, nada muda de estado e a
 * chamada original faz tudo. */
#ifndef AM_FP_FAST_H
#define AM_FP_FAST_H

#include <math.h>
#include <string.h>

#include "cpu/cpu.h"

#define AM_FPSCR_FX 0x80000000U
#define AM_FPSCR_XX 0x02000000U
#define AM_FPSCR_FR 0x00040000U
#define AM_FPSCR_FI 0x00020000U
#define AM_FPSCR_NI 0x00000004U
#define AM_F64_EXP  0x7FF0000000000000ULL

enum { AM_FP_ADD, AM_FP_SUB, AM_FP_MUL, AM_FP_DIV };

static inline u64 am_f64_bits(f64 v) {
    u64 b;
    memcpy(&b, &v, sizeof(b));
    return b;
}

static inline f64 am_f64_value(u64 b) {
    f64 v;
    memcpy(&v, &b, sizeof(v));
    return v;
}

static inline bool am_f64_finite(f64 v) {
    return (am_f64_bits(v) & AM_F64_EXP) != AM_F64_EXP;
}

/* set_fp_exception(cpu, XX) do cpu.c, com o ppc_fpscr_updated (resumos VX e FEX) junto. */
static inline void am_fpscr_inexact(CPUState *cpu) {
    u32 f = cpu->fpscr;
    if ((f & AM_FPSCR_XX) == 0U) {
        f |= AM_FPSCR_FX;
    }
    f |= AM_FPSCR_XX;
    f = (f & ~0x20000000U) | ((f & 0x01F80700U) != 0U ? 0x20000000U : 0U);
    f = (f & ~0x40000000U) | ((((f >> 22) & f & 0x000000F8U) != 0U) ? 0x40000000U : 0U);
    cpu->fpscr = f;
}

static inline void am_set_fprf(CPUState *cpu, u32 value) {
    cpu->fpscr = (cpu->fpscr & ~(0x1FU << 12)) | ((value & 0x1FU) << 12);
}

static inline u32 am_classify_f32(f32 value) {
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    const u32 sign = bits >> 31;
    const u32 exponent = bits & 0x7F800000U;
    const u32 fraction = bits & 0x007FFFFFU;
    if (exponent == 0x7F800000U) {
        return fraction != 0U ? 0x11U : (sign != 0U ? 0x09U : 0x05U);
    }
    if (exponent == 0U) {
        return fraction != 0U ? (sign != 0U ? 0x18U : 0x14U) : (sign != 0U ? 0x12U : 0x02U);
    }
    return sign != 0U ? 0x08U : 0x04U;
}

static inline u32 am_classify_f64(f64 value) {
    const u64 bits = am_f64_bits(value);
    const u64 sign = bits >> 63;
    const u64 exponent = bits & AM_F64_EXP;
    const u64 fraction = bits & 0x000FFFFFFFFFFFFFULL;
    if (exponent == AM_F64_EXP) {
        return fraction != 0U ? 0x11U : (sign != 0U ? 0x09U : 0x05U);
    }
    if (exponent == 0U) {
        return fraction != 0U ? (sign != 0U ? 0x18U : 0x14U) : (sign != 0U ? 0x12U : 0x02U);
    }
    return sign != 0U ? 0x08U : 0x04U;
}

/* force_25_bit do cpu.c para c normal ou zero; subnormal (o caso com deslocamento) devolve false. */
static inline bool am_force_25_bit(f64 value, f64 *out) {
    const u64 bits = am_f64_bits(value);
    if ((bits & AM_F64_EXP) == 0U && (bits & 0x000FFFFFFFFFFFFFULL) != 0U) {
        return false;
    }
    *out = am_f64_value((bits & 0xFFFFFFFFF8000000ULL) + (bits & 0x0000000008000000ULL));
    return true;
}

static inline bool am_binary_ok(f64 a, f64 b, int op, f64 *r) {
    if (!am_f64_finite(a) || !am_f64_finite(b)) {
        return false;
    }
    switch (op) {
    case AM_FP_ADD:
        *r = a + b;
        break;
    case AM_FP_SUB:
        *r = a - b;
        break;
    case AM_FP_MUL:
        *r = a * b;
        break;
    default:
        if (b == 0.0) {
            return false;
        }
        *r = a / b;
        break;
    }
    return am_f64_finite(*r);
}

/* binary_single: arredonda para f32, FI/FR/XX como set_fi_fr, f32 em fpr e ps1, FPRF. Com NI (o jogo liga), o
 * force_single zera o que fica abaixo do menor normal do f32; aqui só entram zero e |r| >= 2^-126, onde ele é o
 * arredondamento comum. */
static inline __attribute__((always_inline)) bool am_single_fast(CPUState *cpu, u8 d, f64 a, f64 b, int op) {
    f64 r;
    if (!am_binary_ok(a, b, op, &r)) {
        return false;
    }
    if ((cpu->fpscr & AM_FPSCR_NI) != 0U && r != 0.0 &&
        (am_f64_bits(r) & 0x7FFFFFFFFFFFFFFFULL) < 0x3810000000000000ULL) {
        return false;
    }
    const f32 rounded = (f32)r;
    if (!isfinite(rounded)) {
        return false;
    }
    const f64 rd = (f64)rounded;
    cpu->fpscr &= ~(AM_FPSCR_FI | AM_FPSCR_FR);
    if (r != rd) {
        am_fpscr_inexact(cpu);
        cpu->fpscr |= AM_FPSCR_FI;
        if (fabs(rd) > fabs(r)) {
            cpu->fpscr |= AM_FPSCR_FR;
        }
    }
    cpu->fpr[d] = rd;
    cpu->ps1[d] = rd;
    am_set_fprf(cpu, am_classify_f32(rounded));
    return true;
}

/* binary_double: o resultado como está, FI/FR limpos só na multiplicação, FPRF. Com NI, só sem subnormal (o
 * force_double zeraria). */
static inline __attribute__((always_inline)) bool am_double_fast(CPUState *cpu, u8 d, f64 a, f64 b, int op) {
    f64 r;
    if (!am_binary_ok(a, b, op, &r)) {
        return false;
    }
    if ((cpu->fpscr & AM_FPSCR_NI) != 0U && (am_f64_bits(r) & AM_F64_EXP) == 0U && r != 0.0) {
        return false;
    }
    cpu->fpr[d] = r;
    if (op == AM_FP_MUL) {
        cpu->fpscr &= ~(AM_FPSCR_FI | AM_FPSCR_FR);
    }
    am_set_fprf(cpu, am_classify_f64(r));
    return true;
}

static inline void am_fadds(CPUState *cpu, u8 d, u8 a, u8 b) {
    if (!am_single_fast(cpu, d, cpu->fpr[a], cpu->fpr[b], AM_FP_ADD)) {
        ppc_fadds(cpu, d, a, b);
    }
}

static inline void am_fsubs(CPUState *cpu, u8 d, u8 a, u8 b) {
    if (!am_single_fast(cpu, d, cpu->fpr[a], cpu->fpr[b], AM_FP_SUB)) {
        ppc_fsubs(cpu, d, a, b);
    }
}

static inline void am_fmuls(CPUState *cpu, u8 d, u8 a, u8 c) {
    f64 c25;
    if (!am_force_25_bit(cpu->fpr[c], &c25) || !am_single_fast(cpu, d, cpu->fpr[a], c25, AM_FP_MUL)) {
        ppc_fmuls(cpu, d, a, c);
    }
}

static inline void am_fdivs(CPUState *cpu, u8 d, u8 a, u8 b) {
    if (!am_single_fast(cpu, d, cpu->fpr[a], cpu->fpr[b], AM_FP_DIV)) {
        ppc_fdivs(cpu, d, a, b);
    }
}

static inline void am_fadd(CPUState *cpu, u8 d, u8 a, u8 b) {
    if (!am_double_fast(cpu, d, cpu->fpr[a], cpu->fpr[b], AM_FP_ADD)) {
        ppc_fadd(cpu, d, a, b);
    }
}

static inline void am_fsub(CPUState *cpu, u8 d, u8 a, u8 b) {
    if (!am_double_fast(cpu, d, cpu->fpr[a], cpu->fpr[b], AM_FP_SUB)) {
        ppc_fsub(cpu, d, a, b);
    }
}

static inline void am_fmul(CPUState *cpu, u8 d, u8 a, u8 c) {
    if (!am_double_fast(cpu, d, cpu->fpr[a], cpu->fpr[c], AM_FP_MUL)) {
        ppc_fmul(cpu, d, a, c);
    }
}

static inline void am_fdiv(CPUState *cpu, u8 d, u8 a, u8 b) {
    if (!am_double_fast(cpu, d, cpu->fpr[a], cpu->fpr[b], AM_FP_DIV)) {
        ppc_fdiv(cpu, d, a, b);
    }
}

/* correct_single_fma_tie do cpu.c. */
static inline f64 am_single_fma_tie(f64 a, f64 c, f64 addend, f64 result) {
    const u64 bits = am_f64_bits(result);
    if ((bits & 0x000000001FFFFFFFULL) != 0x0000000010000000ULL) {
        return result;
    }
    const f64 a_prime = addend - result;
    const f64 b_prime = result + a_prime;
    const f64 product_error = fma(a, c, a_prime);
    const f64 error = product_error + (addend - b_prime);
    if (error == 0.0) {
        return result;
    }
    return am_f64_value((error > 0.0) == (result > 0.0) ? bits + 1U : bits - 1U);
}

/* ppc_fma sem NaN nem infinito em lugar nenhum: o FPSCR só recebe o FPRF. */
static inline bool am_fma(CPUState *cpu, f64 a, f64 c, f64 b, bool single, bool subtract, bool negative,
                          f64 *output) {
    if (!am_f64_finite(a) || !am_f64_finite(b) || !am_f64_finite(c)) {
        return ppc_fma(cpu, a, c, b, single, subtract, negative, output);
    }
    const f64 addend = subtract ? -b : b;
    f64 result;
    if (!single) {
        result = fma(a, c, addend);
    } else {
        f64 c25;
        if (!am_force_25_bit(c, &c25) || !am_f64_finite(c25)) {
            return ppc_fma(cpu, a, c, b, single, subtract, negative, output);
        }
        result = fma(a, c25, addend);
        if ((cpu->fpscr & 3U) == 0U) {
            result = am_single_fma_tie(a, c25, addend, result);
        }
        result = (f64)(f32)result;
    }
    if (!am_f64_finite(result)) {
        return ppc_fma(cpu, a, c, b, single, subtract, negative, output);
    }
    if (negative) {
        result = -result;
    }
    am_set_fprf(cpu, single ? am_classify_f32((f32)result) : am_classify_f64(result));
    *output = result;
    return true;
}

static inline void am_fcmp(CPUState *cpu, u8 crfd, f64 a, f64 b, bool ordered) {
    if (isnan(a) || isnan(b)) {
        ppc_fcmp(cpu, crfd, a, b, ordered);
        return;
    }
    const u32 compare = a < b ? 8U : (a > b ? 4U : 2U);
    cpu->fpscr = (cpu->fpscr & ~(0xFU << 12)) | (compare << 12);
    const u32 shift = 4U * (7U - crfd);
    cpu->cr = (cpu->cr & ~(0xFU << shift)) | (compare << shift);
}

/* ppc_fctiw para fctiwz com o valor dentro do int32 (o cpu.c usa 0x4000/0x2000 aqui, não FI/FR: copiado igual). */
static inline bool am_fctiw(CPUState *cpu, f64 value, bool toward_zero, u64 *output) {
    if (!toward_zero || isnan(value)) {
        return ppc_fctiw(cpu, value, toward_zero, output);
    }
    const f64 rounded = trunc(value);
    if (rounded >= 2147483648.0 || rounded < -2147483648.0) {
        return ppc_fctiw(cpu, value, toward_zero, output);
    }
    const u32 result = (u32)(s32)rounded;
    cpu->fpscr &= ~0x00006000U;
    if (rounded != value) {
        am_fpscr_inexact(cpu);
        cpu->fpscr |= 0x00004000U;
        if (fabs(rounded) > fabs(value)) {
            cpu->fpscr |= 0x00002000U;
        }
    }
    *output = 0xFFF8000000000000ULL | result | ((result == 0U && signbit(value)) ? 0x100000000ULL : 0ULL);
    return true;
}

#define ppc_fadds am_fadds
#define ppc_fsubs am_fsubs
#define ppc_fmuls am_fmuls
#define ppc_fdivs am_fdivs
#define ppc_fadd  am_fadd
#define ppc_fsub  am_fsub
#define ppc_fmul  am_fmul
#define ppc_fdiv  am_fdiv
#define ppc_fma   am_fma
#define ppc_fcmp  am_fcmp
#define ppc_fctiw am_fctiw

#endif /* AM_FP_FAST_H */
