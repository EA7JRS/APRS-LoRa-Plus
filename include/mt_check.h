/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of LoRa APRS Tracker.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// APRS LoRa+ MAPRS transponder: comprobacion del distintivo maritimo del MMSI en MarineTraffic (busqueda por MMSI de su API,
// con la clave del usuario), por la WiFi de casa / del puerto (WebUI > Connectivity > WiFi). Lo pide la WebUI (/api/mtcheck).
// El MARS de la UIT no tiene API: se confirma a mano en la WebUI. Solo en el entorno del MAPRS.
#ifndef MT_CHECK_H_
#define MT_CHECK_H_

#include <Arduino.h>

namespace MT_Check {
    bool    start();                // false si ya hay una consulta en marcha o falta MMSI / distintivo / clave
    void    loop();                 // desde el bucle principal: guarda el resultado en la configuracion
    String  statusJson();           // {"busy":..,"msg":"..","ok":..,"src":..,"warn":..}
}

#endif
