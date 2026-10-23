/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Memory Interface (0xCC004000): proteção de memória e contadores. YAGCD 5.5; Dolphin Core/HW/MemoryInterface.cpp. */
#ifndef AM_MI_H
#define AM_MI_H

#include "am/types.h"

#define MI_BASE 0x0C004000U

void mi_reset(void);
u64 mi_read(u32 offset, u8 size);
void mi_write(u32 offset, u64 value, u8 size);

#endif /* AM_MI_H */
