/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Video Interface (0xCC002000). No M4 só o que o caminho até main lê: o seletor de clock (VI_CLOCK, 0x6C), consultado
 * por SISetSamplingRate. O resto (timings, XFB, interrupções de retrace) é do M5/M6 e continua am_fatal.
 * Fonte: YAGCD 5.2; Dolphin Core/HW/VideoInterface.{h,cpp} (Preset, tag 2609a). */
#ifndef AM_VI_H
#define AM_VI_H

#include "am/types.h"

#define VI_BASE      0x0C002000U
#define VI_REG_CLOCK 0x6CU

void vi_reset(bool ntsc);
u64 vi_read(u32 offset, u8 size);
void vi_write(u32 offset, u64 value, u8 size);

#endif /* AM_VI_H */
