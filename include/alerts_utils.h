/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Avisos oficiales en el tracker: meteorologicos (AEMET, MeteoAlarm, NWS), tsunamis, terremotos (USGS / EMSC) y tiempo
 * espacial (SWPC). Los descarga la app Android con el internet del movil y los manda por BLE (ordenes "alert", "alertdel",
 * "alertclr", "spacewx"); la T-TWR Plus ademas decodifica la NOAA Weather Radio (SAME) en FM. Lista, caducidad, ventana
 * emergente y zumbador por nivel (Display > alertPopup / alertBuzz). El modelo y los lectores son los del nucleo del plotter
 * (lib/aprs_formulas: alerts, same).
 */

#ifndef ALERTS_UTILS_H_
#define ALERTS_UTILS_H_

#include <Arduino.h>
#include <ArduinoJson.h>
#include "alerts.h"

namespace ALERTS {

    void        loop();                                         // caducidad y SAME de la radio
    bool        fromJson(JsonVariantConst doc, String& error);  // orden "alert": id, s, lv, ev, ar, tx, on, ex, la, lo
    bool        remove(const char* id);
    void        clear(const char* src);                         // codigo de fuente ("aemet", "nws"...) o "" = todas
    bool        spaceFromJson(JsonVariantConst doc);            // orden "spacewx": g, r, s, gm, kp[], kt, sfi, bz, bt, ws, xr
    void        onSame(const char* header);                     // cabecera SAME confirmada (T-TWR)
    void        add(const plotter::Alert& a, bool notify);

    const plotter::AlertList& list();
    const plotter::SpaceWx&   space();
    bool        hasSpace();                                     // datos de menos de 6 h
    uint32_t    spaceAgeS();
    uint16_t    version();                                      // cambia con la lista (para Display y la app)

    // Ventana emergente del aviso mas grave sin ver
    const plotter::Alert* popup();
    void        popupClose();                                   // cerrado: no vuelve a salir (salvo si sube de nivel)
    bool        sounding();
    void        mute();
    uint32_t    sameBursts();                                   // rafagas SAME leidas (estado)
}

#endif
