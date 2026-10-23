/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Serial Interface (0xCC006400): portas de controle. YAGCD 5.8; Dolphin Core/HW/SI/SI.cpp. */
#ifndef AM_SI_H
#define AM_SI_H

#include "am/types.h"

#define SI_BASE 0x0C006400U

#define SI_CHANNEL_COUNT 4U
#define SI_BUFFER_SIZE   128U

#define SI_REG_POLL   0x30U
#define SI_REG_COMCSR 0x34U
#define SI_REG_STATUS 0x38U
#define SI_REG_EXICLK 0x3CU
#define SI_REG_BUFFER 0x80U

/* SICOMCSR (Dolphin SI.h: USIComCSR). */
#define SI_CSR_TSTART     0x00000001U
#define SI_CSR_CHANNEL    0x00000006U
#define SI_CSR_INLNGTH    0x00007F00U
#define SI_CSR_OUTLNGTH   0x007F0000U
#define SI_CSR_RDSTINTMSK 0x08000000U
#define SI_CSR_RDSTINT    0x10000000U
#define SI_CSR_COMERR     0x20000000U
#define SI_CSR_TCINTMSK   0x40000000U
#define SI_CSR_TCINT      0x80000000U

/* Resposta de tipo de um controle padrão (Dolphin SI_Device.h: SI_GC_CONTROLLER = 0x09000000, 3 bytes). */
#define SI_TYPE_GC_CONTROLLER 0x09000000U

/* Valores do controle sem nada pressionado (Dolphin GCPadStatus: sticks no centro 0x80, gatilhos em 0;
 * SI_DeviceGCController::MapPadStatus põe PAD_USE_ORIGIN = 0x0080 nos botões). Modo 3 é o que o PADInit escolhe. */
#define SI_PAD_NEUTRAL_HI       0x00808080U
#define SI_PAD_NEUTRAL_LO_MODE3 0x80800000U

/* Aparelhos: controle padrão conectado e sem entrada na porta 1 (canal 0), portas 2-4 vazias, como no oráculo. */
void si_reset(void);
/* SerialInterfaceManager::UpdateDevices: o VI chama no meio-quadro agendado; cada canal entrega os dados do
 * aparelho (RDSTn) ou acusa ausência (NOREPn). */
void si_update_devices(void);
/* SIPOLL.X: linhas entre duas leituras do SI dentro do mesmo campo. */
u32 si_poll_x_lines(void);
u64 si_poll_count(void);

/* Botões do controle 1 (máscara do SDK/Dolphin: PAD_BUTTON_LEFT 0x0001 ... PAD_BUTTON_START 0x1000). Sem chamada, o
 * controle fica neutro. Só para roteiros de teste (--pad-script); entrada de verdade é o M7. */
#define SI_PAD_LEFT  0x0001U
#define SI_PAD_RIGHT 0x0002U
#define SI_PAD_DOWN  0x0004U
#define SI_PAD_UP    0x0008U
#define SI_PAD_Z     0x0010U
#define SI_PAD_A     0x0100U
#define SI_PAD_B     0x0200U
#define SI_PAD_X     0x0400U
#define SI_PAD_Y     0x0800U
#define SI_PAD_START 0x1000U
void si_set_pad_buttons(u16 buttons);
u64 si_read(u32 offset, u8 size);
void si_write(u32 offset, u64 value, u8 size);

#endif /* AM_SI_H */
