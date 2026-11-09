/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tiempo de internet para la pagina TELEMETRY. Ver include/net_wx.h
 */

#include "net_wx.h"

namespace NET_WX {

    static Data cur;
    static bool have = false;
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    static const uint32_t MAX_AGE_S = 7200;

    void set(const Data& d) {
        portENTER_CRITICAL(&mux);
        cur = d;
        cur.rxMs = millis() ? millis() : 1;
        have = true;
        portEXIT_CRITICAL(&mux);
    }

    uint32_t ageS() {
        if (!have) return 0xFFFFFFFFUL;
        return cur.ageAtRxS + (millis() - cur.rxMs) / 1000;
    }

    bool get(Data& out) {
        if (!have || ageS() > MAX_AGE_S) return false;
        portENTER_CRITICAL(&mux);
        out = cur;
        portEXIT_CRITICAL(&mux);
        return true;
    }

    const char* owmAqiName(int a) {
        switch (a) { case 1: return "Good"; case 2: return "Fair"; case 3: return "Moderate"; case 4: return "Poor"; case 5: return "V.poor"; default: return ""; }
    }

    const char* usAqiName(int a) {
        if (a < 0) return "";
        if (a <= 50) return "Good";
        if (a <= 100) return "Moderate";
        if (a <= 150) return "USG";
        if (a <= 200) return "Unhealthy";
        if (a <= 300) return "V.unhealthy";
        return "Hazardous";
    }
}
