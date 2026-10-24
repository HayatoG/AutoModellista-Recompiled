/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/rel_map.h"

#include <string.h>

#include "am/mem.h"

static struct {
    RelMapping map[REL_MAP_MAX];
    u32 count;
    u32 last; /* último acerto: os acessos de um módulo vêm em rajadas */
} s_rel;

void rel_map_reset(void) {
    memset(&s_rel, 0, sizeof(s_rel));
}

static u32 phys_of(GuestAddr addr) {
    return addr & 0x3FFFFFFFU;
}

static RelMapping *find_id(u32 id) {
    for (u32 i = 0; i < s_rel.count; i++) {
        if (s_rel.map[i].id == id) {
            return &s_rel.map[i];
        }
    }
    return NULL;
}

bool rel_map_add(u32 id, GuestAddr virt, u32 size, GuestAddr real) {
    const u32 vp = phys_of(virt);
    const u32 rp = phys_of(real);
    if (size == 0U || vp < REL_MAP_PHYS_START || vp > REL_MAP_PHYS_END - size || rp > MEM_MEM1_SIZE - size ||
        (virt & 0xC0000000U) != MEM_CACHED_BASE) {
        return false;
    }
    for (u32 i = 0; i < s_rel.count; i++) {
        const RelMapping *m = &s_rel.map[i];
        const u32 mp = phys_of(m->virt);
        if (m->id != id && vp < mp + m->size && mp < vp + size) {
            return false;
        }
    }
    RelMapping *slot = find_id(id);
    if (slot == NULL) {
        if (s_rel.count == REL_MAP_MAX) {
            return false;
        }
        slot = &s_rel.map[s_rel.count++];
    }
    *slot = (RelMapping){.id = id, .virt = virt, .real = real, .size = size, .retired = false};
    return true;
}

bool rel_map_retire(u32 id) {
    RelMapping *m = find_id(id);
    if (m == NULL) {
        return false;
    }
    m->retired = true;
    return true;
}

bool rel_map_shrink(u32 id, u32 size) {
    RelMapping *m = find_id(id);
    if (m == NULL || size == 0U || size > m->size) {
        return false;
    }
    m->size = size;
    return true;
}

bool rel_map_resolve(u32 phys, u32 size, u32 *real_phys) {
    for (u32 n = 0; n < s_rel.count; n++) {
        const u32 i = (s_rel.last + n) % s_rel.count;
        const RelMapping *m = &s_rel.map[i];
        const u32 vp = phys_of(m->virt);
        if (phys >= vp && phys - vp < m->size && m->size - (phys - vp) >= size) {
            s_rel.last = i;
            *real_phys = phys_of(m->real) + (phys - vp);
            return true;
        }
    }
    return false;
}

bool rel_map_real_to_virtual(GuestAddr real, GuestAddr *virt) {
    const u32 rp = phys_of(real);
    for (u32 i = 0; i < s_rel.count; i++) {
        const RelMapping *m = &s_rel.map[i];
        const u32 mp = phys_of(m->real);
        if (!m->retired && rp >= mp && rp - mp < m->size) {
            *virt = m->virt + (rp - mp);
            return true;
        }
    }
    return false;
}

u32 rel_map_count(void) {
    return s_rel.count;
}

const RelMapping *rel_map_at(u32 index) {
    return index < s_rel.count ? &s_rel.map[index] : NULL;
}
