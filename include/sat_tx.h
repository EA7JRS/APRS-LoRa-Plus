/*
 * Modo SAT, fase 3: emision hacia el satelite con la frecuencia de subida precompensada por Doppler, y secuenciador de
 * conmutacion (preamplificador, amplificador / rele, PTT de un equipo externo).
 *   CW      -> el texto en Morse con la portadora del SX126x
 *   FSK     -> trama AX.25 UI "INDICATIVO>APLRT1:texto" en G3RUH
 *   LoRa    -> el texto como carga util
 * Nunca emite solo: cada emision la pide el usuario (menu o app) y solo si la habilita la WebUI, hay un indicativo real, el
 * satelite esta por encima de la elevacion minima, la subida cae en 430-440 MHz y ha pasado el intervalo minimo.
 *
 * Copyright (c) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
 */
#ifndef SAT_TX_H_
#define SAT_TX_H_

#include <stddef.h>
#include <stdint.h>
#include "sat_core.h"
#include "sat_rx.h"

namespace SATTX_Utils {

    void        begin();                                                    // deja el secuenciador en reposo (preamplificador activo, PA y PTT sueltos)
    SATRX_Utils::Kind classifyUp(const SatCore::Transmitter& t, const char** why = nullptr);   // como se podria emitir en la subida de ese transmisor
    bool        check(uint8_t txIndex, char* why, size_t n);                // true si se puede emitir ahora; si no, el motivo
    bool        send(uint8_t txIndex, const char* text);                   // emite (bloquea); false y message() si no se pudo
    const char* message();
    uint32_t    sinceLastS();                                               // segundos desde la ultima emision (0xFFFFFFFF = ninguna)
    uint32_t    count();
    double      lastHz();                                                   // frecuencia de la ultima emision (Hz)

}

#endif
