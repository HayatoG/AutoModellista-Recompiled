/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/dsp_hle.h"

#include <stdint.h>
#include <string.h>

#include "am/log.h"
#include "am/mem.h"
#include "am/timebase.h"

#define DSP_HLE_MAIL_QUEUE  16U
#define DSP_HLE_IRQ_QUEUE   8U
#define DSP_HLE_CMDLIST_MAX 0x200U /* AXUCode::m_cmdlist: 512 palavras de 16 bits */

/* UCodes.h e AX.h. */
#define DSP_MAIL_ROM_READY  0x8071FEEDU
#define DSP_MAIL_INIT_READY 0x80544348U
#define DSP_TASK_MAIL_MASK  0xFFFF0000U
#define DSP_TASK_TO_DSP     0xCDD10000U
#define DSP_INIT            0xDCD10000U
#define DSP_RESUME          0xDCD10001U
#define DSP_YIELD           0xDCD10002U
#define MAIL_RESUME         0xCDD10000U
#define MAIL_NEW_UCODE      0xCDD10001U
#define MAIL_RESET          0xCDD10002U
#define MAIL_CONTINUE       0xCDD10003U
#define AX_MAIL_CMDLIST     0xBABE0000U
/* AXUCode::SignalWorkEnd: AX_EMPTY_COMMAND_LIST_CYCLES. */
#define AX_YIELD_CYCLES 2500U
#define AX_CMD_MORE     0x0DU
#define AX_CMD_END      0x0FU

typedef enum AxMailState {
    AX_WAIT_CMDLIST_SIZE,
    AX_WAIT_CMDLIST_ADDR,
    AX_WAIT_NEXT_TASK,
} AxMailState;

typedef struct DspMail {
    u32 mail;
    bool irq_on_pop; /* MailHandler: o pedido de interrupção vai junto do mail da frente da fila */
} DspMail;

static struct {
    DspHleKind kind;
    DspMail mails[DSP_HLE_MAIL_QUEUE];
    u32 mail_count;
    u32 last_mail;
    bool halted;
    u64 irq_due[DSP_HLE_IRQ_QUEUE];
    u32 irq_count;
    /* ROM: tarefa de boot (ROMUCode). */
    u32 next_parameter;
    u32 boot_ram;
    u32 boot_len;
    /* AX. */
    AxMailState ax_state;
    u16 cmdlist_size;
    bool upload_in_progress;
    u32 upload_step;
    u32 upload_iram_mram;
    u32 upload_iram_size;
    bool needs_resume_mail;
    DspHleStats stats;
} s_hle;

static void request_irq(u64 delay) {
    AM_ASSERT(s_hle.irq_count < DSP_HLE_IRQ_QUEUE, "fila de interrupções do DSP cheia");
    s_hle.irq_due[s_hle.irq_count++] = timebase_cycles(NULL) + delay;
}

/* CMailHandler::PushMail. */
static void push_mail(u32 mail, bool interrupt, u64 delay) {
    if (interrupt) {
        if (s_hle.mail_count == 0U) {
            request_irq(delay);
        } else {
            s_hle.mails[0].irq_on_pop = true;
        }
    }
    AM_ASSERT(s_hle.mail_count < DSP_HLE_MAIL_QUEUE, "fila de mail do DSP cheia (0x%08X)", mail);
    s_hle.mails[s_hle.mail_count++] = (DspMail){.mail = mail, .irq_on_pop = false};
    s_hle.stats.mails_from_dsp++;
}

/* DSPHLE::SetUCode: limpa a fila e o novo microcódigo se anuncia. */
static void set_ucode(DspHleKind kind, u32 crc) {
    s_hle.mail_count = 0;
    s_hle.kind = kind;
    s_hle.stats.ucode_crc = crc;
    s_hle.next_parameter = 0;
    s_hle.ax_state = AX_WAIT_CMDLIST_SIZE;
    s_hle.upload_in_progress = false;
    s_hle.upload_step = 0;
    switch (kind) {
    case DSP_HLE_KIND_ROM:
        push_mail(DSP_MAIL_ROM_READY, false, 0);
        break;
    case DSP_HLE_KIND_INIT:
        push_mail(DSP_MAIL_INIT_READY, false, 0);
        break;
    case DSP_HLE_KIND_AX:
        push_mail(DSP_INIT, true, 0); /* AXUCode::InitializeShared */
        break;
    }
}

void dsp_hle_reset(void) {
    memset(&s_hle, 0, sizeof(s_hle));
    s_hle.halted = true;
    set_ucode(DSP_HLE_KIND_ROM, DSP_HLE_UCODE_ROM);
}

void dsp_hle_set_halted(bool halted) {
    s_hle.halted = halted;
}

void dsp_hle_boot_rom(void) {
    set_ucode(DSP_HLE_KIND_ROM, DSP_HLE_UCODE_ROM);
}

void dsp_hle_boot_init(void) {
    set_ucode(DSP_HLE_KIND_INIT, DSP_HLE_UCODE_INIT);
}

DspHleKind dsp_hle_kind(void) {
    return s_hle.kind;
}

const DspHleStats *dsp_hle_stats(void) {
    return &s_hle.stats;
}

/* Common/Hash.cpp, HashEctor: xor de cada byte e rotação de 3 bits à esquerda. */
u32 dsp_hle_hash_ector(GuestAddr addr, u32 size) {
    u32 crc = 0;
    for (u32 i = 0; i < size; i++) {
        crc ^= mem_read8(NULL, addr + i);
        crc = (crc << 3) | (crc >> 29);
    }
    return crc;
}

/* UCodeFactory: só os AX do GameCube (UCodes.cpp) são aceitos. */
static bool is_known_ax(u32 crc) {
    static const u32 s_ax[] = {0x3ad3b7acU, 0x3daf59b9U, 0x4e8a8b21U, 0x07f88145U, 0xe2136399U};
    for (size_t i = 0; i < sizeof(s_ax) / sizeof(s_ax[0]); i++) {
        if (s_ax[i] == crc) {
            return true;
        }
    }
    return false;
}

static void switch_by_hash(GuestAddr ram, u32 len, const char *how) {
    const GuestAddr addr = MEM_CACHED_BASE | (ram & 0x03FFFFFFU);
    const u32 crc = dsp_hle_hash_ector(addr, len);
    s_hle.stats.boots++;
    s_hle.stats.last_boot_addr = addr;
    s_hle.stats.last_boot_len = len;
    AM_LOG_INFO("DSP: %s do microcódigo de 0x%08X (%u bytes), hash Ector 0x%08X", how, addr, len, crc);
    if (!is_known_ax(crc)) {
        am_fatal("DSP: microcódigo de hash 0x%08X (0x%08X, %u bytes) não é um AX conhecido; só AX tem HLE (o "
                 "Dolphin também só reconhece pelo hash)",
                 crc, addr, len);
    }
    set_ucode(DSP_HLE_KIND_AX, crc);
}

/* ROMUCode::HandleMail. */
static void rom_mail(u32 mail) {
    if (s_hle.next_parameter == 0U) {
        if ((mail & 0xFFFF0000U) != 0x80F30000U) {
            push_mail(0xFEEE0000U | (mail & 0xFFFFU), false, 0);
        } else {
            s_hle.next_parameter = mail;
        }
        return;
    }
    switch (s_hle.next_parameter) {
    case 0x80F3A001U:
        s_hle.boot_ram = mail;
        break;
    case 0x80F3A002U:
        s_hle.boot_len = mail & 0xFFFFU;
        break;
    case 0x80F3D001U:
        switch_by_hash(s_hle.boot_ram, s_hle.boot_len, "boot pela ROM");
        return;
    default: /* 0x80F3B002 (DRAM) e 0x80F3C002 (destino na IRAM): não mudam o HLE */
        break;
    }
    s_hle.next_parameter = 0;
}

/* AXUCode::HandleCommandList sem efeitos: percorre até CMD_END para conferir o formato (argumentos por comando). */
static void ax_walk_cmdlist(GuestAddr addr, u32 words) {
    static const u8 s_args[0x14] = {2, 5, 2, 0, 4, 4, 2, 2, 10, 2, 0, 0, 0, 3, 4, 0, 4, 2, 4, 12};
    if (words >= DSP_HLE_CMDLIST_MAX) {
        am_fatal("DSP AX: lista de comandos de %u palavras em 0x%08X (máximo %u)", words, addr, DSP_HLE_CMDLIST_MAX);
    }
    u32 i = 0;
    for (u32 guard = 0; guard < 64U; guard++) {
        const u32 cmd = mem_read16(NULL, addr + (i * 2U));
        i++;
        if (cmd == AX_CMD_END) {
            s_hle.stats.command_words += i;
            return;
        }
        if (cmd >= sizeof(s_args)) {
            am_fatal("DSP AX: comando 0x%04X desconhecido na lista em 0x%08X (palavra %u)", cmd, addr, i - 1U);
        }
        if (cmd == AX_CMD_MORE) {
            const u32 hi = mem_read16(NULL, addr + (i * 2U));
            const u32 lo = mem_read16(NULL, addr + (i * 2U) + 2U);
            s_hle.stats.command_words += i + 3U;
            addr = MEM_CACHED_BASE | (((hi << 16) | lo) & 0x03FFFFFFU);
            i = 0;
            continue;
        }
        i += s_args[cmd];
    }
    am_fatal("DSP AX: lista de comandos em 0x%08X sem CMD_END em 64 comandos", addr);
}

/* UCodeInterface::PrepareBootUCode: dez mails descrevem o próximo microcódigo. */
static void ax_prepare_boot(u32 mail) {
    if (s_hle.upload_step == 3U) {
        s_hle.upload_iram_mram = mail;
    } else if (s_hle.upload_step == 4U) {
        s_hle.upload_iram_size = mail & 0xFFFFU;
    }
    if (++s_hle.upload_step == 10U) {
        s_hle.upload_step = 0;
        s_hle.upload_in_progress = false;
        s_hle.needs_resume_mail = true;
        switch_by_hash(s_hle.upload_iram_mram, s_hle.upload_iram_size, "troca (MAIL_NEW_UCODE)");
    }
}

/* AXUCode::HandleMail. */
static void ax_mail(u32 mail) {
    if (s_hle.upload_in_progress) {
        ax_prepare_boot(mail);
        return;
    }
    switch (s_hle.ax_state) {
    case AX_WAIT_CMDLIST_SIZE:
        if ((mail & DSP_TASK_MAIL_MASK) == AX_MAIL_CMDLIST) {
            s_hle.cmdlist_size = (u16)mail;
            s_hle.ax_state = AX_WAIT_CMDLIST_ADDR;
        } else {
            AM_LOG_WARN("DSP AX: mail 0x%08X onde se esperava 0xBABExxxx (Dolphin só registra)", mail);
        }
        return;
    case AX_WAIT_CMDLIST_ADDR:
        ax_walk_cmdlist(MEM_CACHED_BASE | (mail & 0x03FFFFFFU), s_hle.cmdlist_size);
        s_hle.stats.command_lists++;
        push_mail(DSP_YIELD, true, AX_YIELD_CYCLES);
        s_hle.ax_state = AX_WAIT_NEXT_TASK;
        return;
    case AX_WAIT_NEXT_TASK:
        break;
    }
    mail = DSP_TASK_TO_DSP | (mail & ~DSP_TASK_MAIL_MASK); /* o microcódigo não confere o prefixo */
    switch (mail) {
    case MAIL_RESUME:
        push_mail(DSP_RESUME, true, 0);
        s_hle.ax_state = AX_WAIT_CMDLIST_SIZE;
        return;
    case MAIL_NEW_UCODE:
        s_hle.upload_in_progress = true;
        s_hle.ax_state = AX_WAIT_CMDLIST_SIZE;
        return;
    case MAIL_RESET:
        set_ucode(DSP_HLE_KIND_ROM, DSP_HLE_UCODE_ROM);
        return;
    case MAIL_CONTINUE:
        s_hle.ax_state = AX_WAIT_CMDLIST_SIZE;
        return;
    default:
        AM_LOG_WARN("DSP AX: mail de tarefa 0x%08X desconhecido (Dolphin só registra)", mail);
        return;
    }
}

void dsp_hle_mail_to_dsp(u32 mail) {
    s_hle.stats.mails_to_dsp++;
    AM_LOG_DEBUG("DSP: CPU -> DSP 0x%08X", mail);
    switch (s_hle.kind) {
    case DSP_HLE_KIND_ROM:
        rom_mail(mail);
        return;
    case DSP_HLE_KIND_INIT:
        return; /* INITUCode::HandleMail não faz nada */
    case DSP_HLE_KIND_AX:
        ax_mail(mail);
        return;
    }
}

u16 dsp_hle_read_mail_high(void) {
    if (!s_hle.halted && s_hle.mail_count > 0U) {
        s_hle.last_mail = s_hle.mails[0].mail;
    }
    return (u16)(s_hle.last_mail >> 16);
}

u16 dsp_hle_read_mail_low(void) {
    if (!s_hle.halted && s_hle.mail_count > 0U) {
        const DspMail front = s_hle.mails[0];
        s_hle.last_mail = front.mail;
        memmove(&s_hle.mails[0], &s_hle.mails[1], (s_hle.mail_count - 1U) * sizeof(DspMail));
        s_hle.mail_count--;
        if (front.irq_on_pop) {
            request_irq(0);
        }
    }
    s_hle.last_mail &= ~0x80000000U; /* o bit 31 apaga depois da leitura da metade baixa */
    return (u16)s_hle.last_mail;
}

/* AXUCode::Update (a cada ms no Dolphin): depois de uma troca de microcódigo o AX avisa que voltou. */
static void update_resume(void) {
    if (s_hle.kind == DSP_HLE_KIND_AX && s_hle.needs_resume_mail) {
        s_hle.needs_resume_mail = false;
        push_mail(DSP_RESUME, true, 0);
    }
}

bool dsp_hle_take_interrupt(u64 now) {
    update_resume();
    for (u32 i = 0; i < s_hle.irq_count; i++) {
        if (s_hle.irq_due[i] <= now) {
            memmove(&s_hle.irq_due[i], &s_hle.irq_due[i + 1U], (s_hle.irq_count - i - 1U) * sizeof(u64));
            s_hle.irq_count--;
            return true;
        }
    }
    return false;
}

u64 dsp_hle_next_event(void) {
    u64 next = UINT64_MAX;
    for (u32 i = 0; i < s_hle.irq_count; i++) {
        next = s_hle.irq_due[i] < next ? s_hle.irq_due[i] : next;
    }
    return next;
}
