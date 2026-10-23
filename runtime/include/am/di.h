/* SPDX-License-Identifier: GPL-3.0-or-later */
/* DVD Interface (0xCC006000). YAGCD 5.7; Dolphin Core/HW/DVD/DVDInterface.cpp. */
#ifndef AM_DI_H
#define AM_DI_H

#include "am/types.h"

#define DI_BASE 0x0C006000U

#define DI_REG_SR      0x00U
#define DI_REG_CVR     0x04U
#define DI_REG_CMDBUF0 0x08U
#define DI_REG_CMDBUF1 0x0CU
#define DI_REG_CMDBUF2 0x10U
#define DI_REG_MAR     0x14U
#define DI_REG_LENGTH  0x18U
#define DI_REG_CR      0x1CU
#define DI_REG_IMMBUF  0x20U
#define DI_REG_CFG     0x24U

/* DISR (Dolphin DVDInterface.h: UDISR). */
#define DI_SR_BREAK      0x01U
#define DI_SR_DEINTMASK  0x02U
#define DI_SR_DEINT      0x04U
#define DI_SR_TCINTMASK  0x08U
#define DI_SR_TCINT      0x10U
#define DI_SR_BRKINTMASK 0x20U
#define DI_SR_BRKINT     0x40U
/* DICVR. */
#define DI_CVR_CVR        0x01U
#define DI_CVR_CVRINTMASK 0x02U
#define DI_CVR_CVRINT     0x04U

#define DI_CMD_INQUIRY 0x12U

typedef struct DiStats {
    u64 commands;
    u64 reads;
    u64 read_bytes;
    u64 seeks;
    u64 stop_motor;
    u64 audio_buffer_config;
} DiStats;

/* Chamado a cada comando de leitura (deslocamento no disco, bytes, destino físico), para o log de eventos. */
typedef void (*DiReadObserver)(u64 offset, u32 length, u32 dst);

void di_reset(void);
void di_set_read_observer(DiReadObserver observer);
const DiStats *di_stats(void);
/* Ciclo em que o comando em andamento termina, ou UINT64_MAX. */
u64 di_next_event_cycle(void);
/* Conclui comandos cujo prazo passou (o chassi chama a cada volta). */
void di_update(void);
u64 di_read(u32 offset, u8 size);
void di_write(u32 offset, u64 value, u8 size);

#endif /* AM_DI_H */
