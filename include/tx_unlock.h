/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Desbloqueo del rango de emision de la T-TWR (por defecto solo las bandas de aficionado de la region IARU donde se esta):
 * con el codigo de desbloqueo de ESTE equipo la radio emite en toda la banda del modulo (136-174 / 400-480 MHz), para usos con
 * licencia propia (servicios, MARS...). El codigo es la firma ECDSA P-256 (SHA-256) de "APRS-LoRa-Plus TX unlock " + MAC, 64 bytes
 * en 128 cifras hexadecimales (r y s): solo la puede hacer quien tiene la clave privada (Manuel - EA7JRS, tools/tx_unlock_code.py)
 * y el firmware solo lleva la publica (include/tx_unlock_key.h), asi que leer el binario no sirve para sacar codigos. Sin clave publica
 * no se puede desbloquear. El estado se guarda en la NVS (no en la configuracion: ni la WebUI ni cfgset lo cambian). 5 intentos
 * fallidos bloquean 10 minutos.
 */

#ifndef TX_UNLOCK_H_
#define TX_UNLOCK_H_

#include <Arduino.h>

namespace TX_Unlock {
    bool        available();                // el firmware lleva la clave publica
    bool        unlocked();
    const char* deviceId();                 // MAC de 12 cifras hexadecimales (la que va en el codigo)
    // true si el codigo es el de este equipo (queda desbloqueado y se guarda); why con el motivo si no
    bool        tryUnlock(const char* code, const char** why);
    void        lock();                     // vuelve a los limites de aficionado
}

#endif
