/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tradução para C de Source/Core/Core/HW/DSPHLE/UCodes/AX.cpp do Dolphin 2609a (GPL-2.0-or-later; Copyright Dolphin
 * Emulator Project): HandleCommandList, SetupProcessing, DownloadAndMixWithVolume, MixAUXSamples, UploadLRS,
 * SetMainLR, RunCompressor, OutputSamples, MixAUXBLR, SetOppositeLR e SendAUXAndMix. */
#include "am/ax_mixer.h"

#include <string.h>

#include "am/log.h"
#include "am/mem.h"

#define AX_CMDLIST_MAX  0x200U /* AXUCode::m_cmdlist: 512 palavras */
#define AX_UCODE_NO_LPF 0x4E8A8B21U

/* AX.h, CmdType. */
enum {
    CMD_SETUP = 0x00,
    CMD_DL_AND_VOL_MIX = 0x01,
    CMD_PB_ADDR = 0x02,
    CMD_PROCESS = 0x03,
    CMD_MIX_AUXA = 0x04,
    CMD_MIX_AUXB = 0x05,
    CMD_UPLOAD_LRS = 0x06,
    CMD_SET_LR = 0x07,
    CMD_UNK_08 = 0x08,
    CMD_MIX_AUXB_NOWRITE = 0x09,
    CMD_UNK_0A = 0x0A,
    CMD_UNK_0B = 0x0B,
    CMD_UNK_0C = 0x0C,
    CMD_MORE = 0x0D,
    CMD_OUTPUT = 0x0E,
    CMD_END = 0x0F,
    CMD_MIX_AUXB_LR = 0x10,
    CMD_SET_OPPOSITE_LR = 0x11,
    CMD_COMPRESSOR = 0x12,
    CMD_SEND_AUX_AND_MIX = 0x13,
};

#define N AX_SAMPLES_PER_FRAME

static struct {
    AxMixBuffers buf;
    u16 compressor_pos;
    AxMixerStats stats;
} s_mix;

void ax_mixer_reset(void) {
    memset(&s_mix.buf, 0, sizeof(s_mix.buf));
    s_mix.compressor_pos = 0;
}

const AxMixerStats *ax_mixer_stats(void) {
    return &s_mix.stats;
}

AxMixBuffers *ax_mixer_buffers(void) {
    return &s_mix.buf;
}

static GuestAddr ga(u32 phys) {
    return MEM_CACHED_BASE | (phys & 0x03FFFFFFU);
}

static s32 *ch(AxChannel c) {
    return s_mix.buf.ch[c];
}

/* Soma com estouro definido (o acumulador do Dolphin é int). */
static s32 add32(s32 a, s32 b) {
    return (s32)((u32)a + (u32)b);
}

/* CopyToEmuSwapped de um buffer de int: inteiros de 32 bits big-endian. */
static u32 upload(u32 addr, const s32 *src) {
    for (u32 i = 0; i < N; i++) {
        mem_write32(NULL, ga(addr + (i * 4U)), (u32)src[i]);
    }
    return addr + (N * 4U);
}

static s32 rd32(u32 addr, u32 i) {
    return (s32)mem_read32(NULL, ga(addr + (i * 4U)));
}

/* Lê N inteiros de `addr` e soma em `dst`; devolve o endereço seguinte. */
static u32 download_add(u32 addr, s32 *dst) {
    for (u32 i = 0; i < N; i++) {
        dst[i] = add32(dst[i], rd32(addr, i));
    }
    return addr + (N * 4U);
}

/* SetupProcessing -> InitMixingBuffers<5>: por buffer, valor inicial (hi, lo) e delta por amostra; 0 zera. */
static void setup(u32 init_addr) {
    for (u32 c = 0; c < AX_CHANNELS; c++) {
        const u32 at = init_addr + (c * 6U);
        const s32 value = (s32)(((u32)mem_read16(NULL, ga(at)) << 16) | mem_read16(NULL, ga(at + 2U)));
        const s16 delta = (s16)mem_read16(NULL, ga(at + 4U));
        for (u32 j = 0; j < N; j++) {
            s_mix.buf.ch[c][j] = value == 0 ? 0 : add32(value, (s32)j * delta);
        }
    }
}

/* DownloadAndMixWithVolume: os três grupos (principal, AUX A, AUX B) leem os mesmos L, R, S de `addr`, cada um com
 * o seu volume (1.15). */
static void download_and_mix(u32 addr, const u16 volumes[3]) {
    for (u32 g = 0; g < 3U; g++) {
        for (u32 j = 0; j < 3U; j++) {
            s32 *dst = ch((AxChannel)((g * 3U) + j));
            for (u32 k = 0; k < N; k++) {
                const s64 sample = (s64)rd32(addr, (j * N) + k) * volumes[g];
                dst[k] = add32(dst[k], (s32)(sample >> 15));
            }
        }
    }
}

/* MixAUXSamples: manda o AUX à CPU (se write_addr) e soma em L, R, S o que a CPU devolveu. */
static void mix_aux(u32 aux, u32 write_addr, u32 read_addr) {
    const AxChannel first = aux == 0U ? AX_AUXA_L : AX_AUXB_L;
    if (write_addr != 0U) {
        for (u32 j = 0; j < 3U; j++) {
            write_addr = upload(write_addr, ch((AxChannel)(first + j)));
        }
    }
    read_addr = download_add(read_addr, ch(AX_MAIN_L));
    read_addr = download_add(read_addr, ch(AX_MAIN_R));
    (void)download_add(read_addr, ch(AX_MAIN_S));
}

static void upload_lrs(u32 dst) {
    dst = upload(dst, ch(AX_MAIN_L));
    dst = upload(dst, ch(AX_MAIN_R));
    (void)upload(dst, ch(AX_MAIN_S));
}

/* SetMainLR (sinal igual) e SetOppositeLR (esquerda invertida); surround zerado. */
static void set_lr(u32 src, bool opposite) {
    for (u32 i = 0; i < N; i++) {
        const s32 v = rd32(src, i);
        ch(AX_MAIN_L)[i] = opposite ? (s32)(0U - (u32)v) : v;
        ch(AX_MAIN_R)[i] = v;
        ch(AX_MAIN_S)[i] = 0;
    }
}

static u32 abs32(s32 v) {
    return v < 0 ? 0U - (u32)v : (u32)v;
}

/* RunCompressor: se L ou R passou do limiar, aplica uma rampa de ataque escolhida pela posição anterior; depois,
 * `release_frames` quadros de soltura (as rampas de soltura vêm depois das 11 de ataque na tabela). */
static void compressor(u16 threshold, u16 release_frames, u32 table_addr) {
    bool triggered = false;
    for (u32 i = 0; i < N && !triggered; i++) {
        triggered = abs32(ch(AX_MAIN_L)[i]) > threshold || abs32(ch(AX_MAIN_R)[i]) > threshold;
    }
    const u32 frame_bytes = N * 2U;
    u32 offset = 0;
    if (triggered) {
        offset = s_mix.compressor_pos * frame_bytes;
        s_mix.compressor_pos = release_frames;
    } else if (s_mix.compressor_pos != 0U) {
        s_mix.compressor_pos--;
        offset = (11U + s_mix.compressor_pos) * frame_bytes;
    } else {
        return;
    }
    for (u32 i = 0; i < N; i++) {
        const s64 coef = mem_read16(NULL, ga(table_addr + offset + (i * 2U)));
        ch(AX_MAIN_L)[i] = (s32)(((s64)ch(AX_MAIN_L)[i] * coef) >> 15);
        ch(AX_MAIN_R)[i] = (s32)(((s64)ch(AX_MAIN_R)[i] * coef) >> 15);
    }
}

static s16 clamp16(s32 v) {
    if (v < -0x8000) {
        return -0x8000;
    }
    return (s16)(v > 0x7FFF ? 0x7FFF : v);
}

/* OutputSamples: surround (int) em `surround_addr`; L/R saturados em 16 bits, intercalados R, L, big-endian. */
static void output(u32 lr_addr, u32 surround_addr) {
    (void)upload(surround_addr, ch(AX_MAIN_S));
    for (u32 i = 0; i < N; i++) {
        const s32 l = ch(AX_MAIN_L)[i];
        const s32 r = ch(AX_MAIN_R)[i];
        if (l != clamp16(l) || r != clamp16(r)) {
            s_mix.stats.clipped++;
        }
        mem_write16(NULL, ga(lr_addr + (i * 4U)), (u16)clamp16(r));
        mem_write16(NULL, ga(lr_addr + (i * 4U) + 2U), (u16)clamp16(l));
    }
    s_mix.stats.outputs++;
}

/* MixAUXBLR: manda AUX B L/R à CPU; o que volta substitui AUX B L/R e soma em L/R. */
static void mix_auxb_lr(u32 ul_addr, u32 dl_addr) {
    ul_addr = upload(ul_addr, ch(AX_AUXB_L));
    (void)upload(ul_addr, ch(AX_AUXB_R));
    for (u32 side = 0; side < 2U; side++) {
        s32 *aux = ch(side == 0U ? AX_AUXB_L : AX_AUXB_R);
        s32 *main = ch(side == 0U ? AX_MAIN_L : AX_MAIN_R);
        for (u32 i = 0; i < N; i++) {
            const s32 v = rd32(dl_addr, (side * N) + i);
            aux[i] = v;
            main[i] = add32(main[i], v);
        }
    }
}

/* SendAUXAndMix: AUX A L/R/S e AUX B S para a CPU; soma o que vem em L, R, AUX B L e AUX B R. */
static void send_aux_and_mix(const u32 a[6]) {
    u32 up = upload(a[0], ch(AX_AUXA_L));
    up = upload(up, ch(AX_AUXA_R));
    (void)upload(up, ch(AX_AUXA_S));
    (void)upload(a[1], ch(AX_AUXB_S));
    (void)download_add(a[2], ch(AX_MAIN_L));
    (void)download_add(a[3], ch(AX_MAIN_R));
    (void)download_add(a[4], ch(AX_AUXB_L));
    (void)download_add(a[5], ch(AX_AUXB_R));
}

typedef struct CmdList {
    u32 addr;
    u32 idx;
} CmdList;

static u16 arg16(CmdList *l) {
    return mem_read16(NULL, ga(l->addr + (l->idx++ * 2U)));
}

static u32 arg32(CmdList *l) {
    const u32 hi = arg16(l);
    return (hi << 16) | arg16(l);
}

/* Comandos que trocam buffers com a CPU ou fecham o quadro; devolve false se `cmd` não é um deles. */
static bool run_exchange(CmdList *l, u16 cmd) {
    switch (cmd) {
    case CMD_MIX_AUXA:
    case CMD_MIX_AUXB: {
        const u32 w = arg32(l);
        mix_aux(cmd - CMD_MIX_AUXA, w, arg32(l));
        return true;
    }
    case CMD_MIX_AUXB_NOWRITE:
        mix_aux(1, 0, arg32(l));
        return true;
    case CMD_MIX_AUXB_LR: {
        const u32 ul = arg32(l);
        mix_auxb_lr(ul, arg32(l));
        return true;
    }
    case CMD_SEND_AUX_AND_MIX: {
        u32 a[6];
        for (u32 i = 0; i < 6U; i++) {
            a[i] = arg32(l);
        }
        send_aux_and_mix(a);
        return true;
    }
    case CMD_COMPRESSOR: {
        const u16 threshold = arg16(l);
        const u16 frames = arg16(l);
        compressor(threshold, frames, arg32(l));
        return true;
    }
    case CMD_OUTPUT: {
        const u32 surround = arg32(l);
        output(arg32(l), surround);
        return true;
    }
    default:
        return false;
    }
}

static void run_more(CmdList *l) {
    const u32 addr = arg32(l);
    const u16 size = arg16(l);
    if (size >= AX_CMDLIST_MAX) {
        am_fatal("AX: CMD_MORE com lista de %u palavras em 0x%08X (máximo %u)", size, addr, AX_CMDLIST_MAX);
    }
    *l = (CmdList){.addr = addr, .idx = 0};
}

/* Um comando; devolve false em CMD_END. */
static bool run_command(CmdList *l, u16 cmd, u32 *pb_addr) {
    switch (cmd) {
    case CMD_SETUP:
        setup(arg32(l));
        return true;
    case CMD_DL_AND_VOL_MIX: {
        const u32 addr = arg32(l);
        u16 v[3];
        for (u32 i = 0; i < 3U; i++) {
            v[i] = arg16(l); /* um por vez: a ordem de avaliação de um inicializador não é definida */
        }
        download_and_mix(addr, v);
        return true;
    }
    case CMD_PB_ADDR:
        *pb_addr = arg32(l);
        return true;
    case CMD_PROCESS:
        ax_voice_process_list(*pb_addr, &s_mix.buf);
        return true;
    case CMD_UPLOAD_LRS:
        upload_lrs(arg32(l));
        return true;
    case CMD_SET_LR:
    case CMD_SET_OPPOSITE_LR:
        set_lr(arg32(l), cmd == CMD_SET_OPPOSITE_LR);
        return true;
    case CMD_UNK_08:
        l->idx += 10U; /* o Dolphin também só pula */
        return true;
    case CMD_UNK_0A:
    case CMD_UNK_0B:
    case CMD_UNK_0C:
        return true;
    case CMD_MORE:
        run_more(l);
        return true;
    case CMD_END:
        return false;
    default:
        if (!run_exchange(l, cmd)) {
            am_fatal("AX: comando 0x%04X desconhecido na lista em 0x%08X (palavra %u)", cmd, l->addr, l->idx - 1U);
        }
        return true;
    }
}

void ax_mixer_command_list(u32 addr, u32 words, u32 crc) {
    if (crc == AX_UCODE_NO_LPF) {
        am_fatal("AX: microcódigo 0x%08X usa o PB sem filtro passa-baixa, não implementado", crc);
    }
    if (words >= AX_CMDLIST_MAX) {
        am_fatal("AX: lista de comandos de %u palavras em 0x%08X (máximo %u)", words, addr, AX_CMDLIST_MAX);
    }
    s_mix.stats.lists++;
    CmdList l = {.addr = addr, .idx = 0};
    u32 pb_addr = 0;
    for (u32 guard = 0; guard < AX_CMDLIST_MAX; guard++) {
        const u16 cmd = arg16(&l);
        if (cmd < 32U) {
            s_mix.stats.used_mask |= 1U << cmd;
        }
        if (!run_command(&l, cmd, &pb_addr)) {
            return;
        }
        s_mix.stats.commands++;
    }
    am_fatal("AX: lista de comandos em 0x%08X sem CMD_END em %u comandos", addr, AX_CMDLIST_MAX);
}
