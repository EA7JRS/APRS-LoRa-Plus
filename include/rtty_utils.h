/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ tracker.
 * Licensed under the GNU General Public License, version 3 or later.
 *
 * Modo RTTY: baliza de radioteletipo (FSK) con el RTTYClient de RadioLib 8.0.x. Sentencia de telemetria tipo UKHAS
 * ($$CALL,contador,hora,lat,lon,alt*CRC) con la posicion del GPS, o texto libre. Solo transmite.
 */
#ifndef RTTY_UTILS_H_
#define RTTY_UTILS_H_

#include <Arduino.h>

namespace RTTY_Utils {

    void begin();                   // entra en el modo RTTY (deja la radio lista; no emite)
    void end();                     // sale del modo RTTY: restaura la radio del perfil
    bool sendBeacon();              // emite una baliza ahora (telemetria UKHAS o texto); false si falta posicion en modo telemetria
    const char* status();           // texto de estado para la pantalla / la app
    uint32_t sentCount();           // balizas emitidas en esta sesion
    const char* lastSentence();     // ultima sentencia emitida

}

#endif
