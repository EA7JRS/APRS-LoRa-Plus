/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clave PUBLICA (ECDSA P-256, punto sin comprimir de 65 bytes en hexadecimal: "04" + X + Y) con la que la T-TWR comprueba los codigos
 * de desbloqueo del rango de emision. La privada solo la tiene Manuel - EA7JRS (tools/tx_unlock_code.py genkey) y nunca va al
 * repositorio. Vacia = el firmware no se puede desbloquear.
 */
#ifndef TX_UNLOCK_KEY_H_
#define TX_UNLOCK_KEY_H_

#define TX_UNLOCK_PUBKEY ""

#endif
