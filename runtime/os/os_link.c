/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/os_link.h"

#include <stdio.h>
#include <string.h>

#include "am/guest_cpu.h"
#include "am/hook.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/rel_map.h"
#include "am/timebase.h"

/* OSModuleHeader (SDK OSModule.h; os mesmos campos que tools/DolRecomp/src/frontend/container/rel.c lê). */
#define REL_ID           0x00U
#define REL_NUM_SECTIONS 0x0CU
#define REL_SECTION_INFO 0x10U
#define REL_VERSION      0x1CU
#define REL_BSS_SIZE     0x20U
#define REL_PROLOG_SEC   0x30U
#define REL_PROLOG       0x34U
#define REL_BSS_ALIGN    0x44U
#define REL_FIX_SIZE     0x48U
#define REL_HEADER_V3    0x4CU

typedef struct PendingLink {
    bool active;
    GuestAddr return_pc;
    GuestAddr sp;
    u32 module; /* índice em s_link.modules */
    u32 final_size;
} PendingLink;

static struct {
    OsLinkConfig config;
    OsLinkModule modules[OS_LINK_MAX_MODULES];
    u32 module_count;
    OsLinkEvent events[OS_LINK_MAX_EVENTS];
    u32 event_count;
    PendingLink pending;
    GuestAddr returns[4];
    u32 return_count;
} s_link;

#define OS_LINK_MAX_RETURNS 4U

static u32 be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u32 align_up(u32 v, u32 a) {
    return a <= 1U ? v : (v + a - 1U) & ~(a - 1U);
}

static const OsLinkModule *module_by_id(u32 id, u32 *index) {
    for (u32 i = 0; i < s_link.module_count; i++) {
        if (s_link.modules[i].id == id) {
            *index = i;
            return &s_link.modules[i];
        }
    }
    return NULL;
}

static void record(const OsLinkModule *m, bool unlink, GuestAddr real, GuestAddr virt, GuestAddr bss_real,
                   GuestAddr bss_virt, u32 size) {
    if (s_link.event_count == OS_LINK_MAX_EVENTS) {
        am_fatal("OSLink: mais de %u ligações/desligações; aumente OS_LINK_MAX_EVENTS", OS_LINK_MAX_EVENTS);
    }
    OsLinkEvent *e = &s_link.events[s_link.event_count++];
    *e = (OsLinkEvent){.id = m->id,
                       .unlink = unlink,
                       .real = real,
                       .virt = virt,
                       .bss_real = bss_real,
                       .bss_virt = bss_virt,
                       .size = size,
                       .cycle = timebase_cycles(NULL)};
    (void)snprintf(e->name, sizeof(e->name), "%s", m->name);
    if (s_link.config.on_event != NULL) {
        s_link.config.on_event(e, s_link.config.user);
    }
}

/* O módulo em R ainda não foi ligado e é o .rel do disco, byte a byte. */
static void verify_image(const OsLinkModule *m, GuestAddr real, const u8 *file, size_t file_size) {
    for (size_t i = 0; i < file_size; i++) {
        const u8 b = mem_read8(NULL, real + (u32)i);
        if (b != file[i]) {
            am_fatal("OSLink: módulo %s em 0x%08X difere de files/module/%s.rel no byte 0x%zX (0x%02X, arquivo 0x%02X)",
                     m->name, real, m->name, i, b, file[i]);
        }
    }
}

/* Base virtual: o primeiro trecho do gerado começa na primeira seção executável (DolRecomp: base + deslocamento). */
static GuestAddr virtual_base(const OsLinkModule *m, const u8 *file, size_t file_size) {
    const u32 count = be32(&file[REL_NUM_SECTIONS]);
    const u32 table = be32(&file[REL_SECTION_INFO]);
    for (u32 i = 0; i < count && (size_t)table + ((size_t)i * 8U) + 8U <= file_size; i++) {
        const u32 off = be32(&file[table + (i * 8U)]);
        if ((off & 1U) != 0U) {
            return m->code_start - (off & ~1U);
        }
    }
    am_fatal("OSLink: módulo %s sem seção executável no cabeçalho", m->name);
}

static void check_prolog(const OsLinkModule *m, const u8 *file, GuestAddr virt) {
    const u32 sec = file[REL_PROLOG_SEC];
    const u32 table = be32(&file[REL_SECTION_INFO]);
    const GuestAddr prolog = virt + (be32(&file[table + (sec * 8U)]) & ~1U) + be32(&file[REL_PROLOG]);
    if (sec != 0U && prolog != m->prolog) {
        am_fatal("OSLink: módulo %s: prolog do cabeçalho na base 0x%08X é 0x%08X, o C gerado diz 0x%08X", m->name, virt,
                 prolog, m->prolog);
    }
}

static HookAction hook_return(CPUState *cpu, void *user);

/* O retorno do Link só é visto se houver gancho no endereço de retorno (um por sítio de chamada). */
static void register_return(GuestAddr lr) {
    for (u32 i = 0; i < s_link.return_count; i++) {
        if (s_link.returns[i] == lr) {
            return;
        }
    }
    if (s_link.return_count == OS_LINK_MAX_RETURNS || !hook_register(lr, "OSLink (retorno)", hook_return, NULL)) {
        am_fatal("OSLink: sem espaço para o gancho de retorno em 0x%08X", lr);
    }
    s_link.returns[s_link.return_count++] = lr;
}

static HookAction on_link(CPUState *cpu, bool fixed) {
    const GuestAddr real = cpu->gpr[3];
    const GuestAddr bss_real = cpu->gpr[4];
    const u32 id = mem_read32(NULL, real + REL_ID);
    u32 index = 0;
    const OsLinkModule *m = module_by_id(id, &index);
    if (m == NULL) {
        am_fatal("OSLink: módulo id %u em 0x%08X não tem C gerado (test.rel? ver RECOMPILATION_REPORT §4)", id, real);
    }
    if (!fixed) {
        am_fatal("OSLink: %s ligado com OSLink (não fixo, .bss separado em 0x%08X): não suportado, todos os RELs do "
                 "jogo são versão 3 e usam OSLinkFixed",
                 m->name, bss_real);
    }
    const u8 *file = NULL;
    size_t file_size = 0;
    if (s_link.config.load_rel == NULL || !s_link.config.load_rel(m->name, &file, &file_size, s_link.config.user) ||
        file_size < REL_HEADER_V3 || be32(&file[REL_VERSION]) < 3U) {
        am_fatal("OSLink: sem files/module/%s.rel (versão 3) para conferir o módulo em 0x%08X", m->name, real);
    }
    verify_image(m, real, file, file_size);
    const GuestAddr virt = virtual_base(m, file, file_size);
    check_prolog(m, file, virt);
    const u32 bss_size = be32(&file[REL_BSS_SIZE]);
    const u32 bss_off =
        align_up(be32(&file[REL_FIX_SIZE]), be32(&file[REL_BSS_ALIGN]) != 0U ? be32(&file[REL_BSS_ALIGN]) : 4U);
    const u32 linked_size = bss_off + bss_size;
    const u32 window = linked_size > (u32)file_size ? linked_size : (u32)file_size;
    if (!rel_map_add(id, virt, window, real)) {
        am_fatal("OSLink: janela [0x%08X, +0x%X) -> 0x%08X do módulo %s não cabe ou sobrepõe outro módulo", virt,
                 window, real, m->name);
    }
    cpu->gpr[3] = virt;
    cpu->gpr[4] = virt + bss_off;
    record(m, false, real, virt, bss_real, virt + bss_off, linked_size);
    AM_LOG_INFO("OSLink: módulo %s (id %u) no endereço real 0x%08X <-> base virtual 0x%08X; .bss real 0x%08X -> "
                "virtual 0x%08X (%u bytes)",
                m->name, id, real, virt, bss_real, virt + bss_off, bss_size);
    s_link.pending = (PendingLink){
        .active = true, .return_pc = cpu->lr, .sp = cpu->gpr[1], .module = index, .final_size = linked_size};
    register_return(cpu->lr);
    return HOOK_CONTINUE;
}

static HookAction hook_link(CPUState *cpu, void *user) {
    (void)user;
    return on_link(cpu, false);
}

static HookAction hook_link_fixed(CPUState *cpu, void *user) {
    (void)user;
    return on_link(cpu, true);
}

/* Qualquer despacho: se é o retorno do Link pendente (mesmo pc e mesma pilha), confere e encolhe a janela. */
static HookAction hook_return(CPUState *cpu, void *user) {
    (void)user;
    PendingLink *p = &s_link.pending;
    if (!p->active || cpu->pc != p->return_pc || cpu->gpr[1] != p->sp) {
        return HOOK_CONTINUE;
    }
    p->active = false;
    const OsLinkModule *m = &s_link.modules[p->module];
    if (cpu->gpr[3] != 1U) {
        am_fatal("OSLink: Link do SDK devolveu %u para o módulo %s (alinhamento ou versão recusados)", cpu->gpr[3],
                 m->name);
    }
    AM_ASSERT(rel_map_shrink(m->id, p->final_size), "OSLink: encolher a janela de %s", m->name);
    return HOOK_CONTINUE;
}

static HookAction hook_unlink(CPUState *cpu, void *user) {
    (void)user;
    const GuestAddr real = cpu->gpr[3];
    GuestAddr virt = 0;
    const u32 id = mem_read32(NULL, real + REL_ID);
    u32 index = 0;
    const OsLinkModule *m = module_by_id(id, &index);
    if (m == NULL || !rel_map_real_to_virtual(real, &virt)) {
        am_fatal("OSUnlink: 0x%08X (id %u) não é um módulo ligado", real, id);
    }
    cpu->gpr[3] = virt;
    record(m, true, real, virt, 0, 0, 0);
    AM_LOG_INFO("OSUnlink: módulo %s (id %u) real 0x%08X, base virtual 0x%08X", m->name, id, real, virt);
    return HOOK_CONTINUE;
}

bool os_link_init(const OsLinkConfig *config) {
    memset(&s_link, 0, sizeof(s_link));
    if (config->module_count > OS_LINK_MAX_MODULES) {
        return false;
    }
    s_link.config = *config;
    memcpy(s_link.modules, config->modules, config->module_count * sizeof(OsLinkModule));
    s_link.module_count = config->module_count;
    s_link.config.modules = s_link.modules;
    return hook_register(config->os_link, "OSLink", hook_link, NULL) &&
           hook_register(config->os_link_fixed, "OSLinkFixed", hook_link_fixed, NULL) &&
           hook_register(config->os_unlink, "OSUnlink", hook_unlink, NULL);
}

u32 os_link_event_count(void) {
    return s_link.event_count;
}

const OsLinkEvent *os_link_event(u32 index) {
    return index < s_link.event_count ? &s_link.events[index] : NULL;
}
