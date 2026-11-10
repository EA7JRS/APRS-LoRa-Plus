/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tiempo de internet para la pagina TELEMETRY. Ver include/net_wx.h
 */

#include "net_wx.h"
#include "press_trend.h"
#include "pages_core.h"
#include "baro_utils.h"

namespace NET_WX {

    static Data cur;
    static bool have = false;
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    static const uint32_t MAX_AGE_S = 7200;
    static TimedPressureTrend trend;

    void set(const Data& d) {
        const uint32_t now = millis() ? millis() : 1;
        portENTER_CRITICAL(&mux);
        cur = d;
        cur.rxMs = now;
        have = true;
        if (d.hasP) trend.add(d.pressureHpa, now / 1000u - (d.ageAtRxS < now / 1000u ? d.ageAtRxS : 0));   // hora de la observacion
        portEXIT_CRITICAL(&mux);
    }

    bool fromJson(JsonVariantConst doc, Data& d) {
        memset(&d, 0, sizeof(d));
        snprintf(d.src, sizeof(d.src), "%s", (const char*)(doc["src"] | "NET"));
        snprintf(d.place, sizeof(d.place), "%s", (const char*)(doc["pl"] | ""));
        snprintf(d.desc, sizeof(d.desc), "%s", (const char*)(doc["ds"] | ""));
        snprintf(d.pollutant, sizeof(d.pollutant), "%s", (const char*)(doc["mp"] | ""));
        if ((d.hasT = !doc["t"].isNull()))       d.tempC = doc["t"] | 0.0f;
        if ((d.hasH = !doc["h"].isNull()))       d.humidity = doc["h"] | 0.0f;
        if ((d.hasP = !doc["p"].isNull()))       d.pressureHpa = doc["p"] | 0.0f;
        if ((d.hasWind = !doc["ws"].isNull()))   d.windMs = doc["ws"] | 0.0f;
        if ((d.hasGust = !doc["wg"].isNull()))   d.gustMs = doc["wg"] | 0.0f;
        if ((d.hasRain = !doc["r"].isNull()))    d.rainMmH = doc["r"] | 0.0f;
        if ((d.hasPm25 = !doc["pm25"].isNull())) d.pm25 = doc["pm25"] | 0.0f;
        if ((d.hasPm10 = !doc["pm10"].isNull())) d.pm10 = doc["pm10"] | 0.0f;
        d.windDeg = doc["wd"].isNull() ? -1 : (int16_t)constrain((int)(doc["wd"] | 0), 0, 360);
        d.hasClouds = !doc["cl"].isNull();
        d.clouds = (int8_t)constrain((int)(doc["cl"] | 0), 0, 100);
        d.aqiOwm = (int8_t)constrain((int)(doc["aq"] | 0), 0, 5);
        d.aqiUs = doc["aqus"].isNull() ? -1 : (int16_t)constrain((int)(doc["aqus"] | 0), 0, 999);
        d.ageAtRxS = (uint32_t)constrain((long)(doc["age"] | 0L), 0L, 86400L);
        if (d.hasP && !(d.pressureHpa > 800.0f && d.pressureHpa < 1100.0f)) d.hasP = false;
        if (d.hasH && !(d.humidity >= 0.0f && d.humidity <= 100.0f)) d.hasH = false;
        return d.hasT || d.hasH || d.hasP || d.hasWind || d.aqiOwm > 0 || d.aqiUs >= 0;
    }

    void apply(const Data& d) {
        set(d);
        if (d.hasP && d.ageAtRxS < MAX_AGE_S) BARO_Utils::setNetQnh(d.pressureHpa, millis() - d.ageAtRxS * 1000UL);   // QNH del altimetro sin GPS
    }

    bool delta3h(float& hpa) {
        portENTER_CRITICAL(&mux);
        const bool ok = have && trend.delta3h(hpa);
        portEXIT_CRITICAL(&mux);
        return ok;
    }

    bool forecast(char* out, size_t n, float* delta3hOut) {
        Data d;
        if (!get(d) || !d.hasP) return false;
        float delta = 0;
        const bool hasDelta = delta3h(delta);
        const float hum = d.hasH && d.hasT ? d.humidity : -1.0f;
        const float dew = hum >= 0 ? PAGES_Core::dewPointC(d.tempC, d.humidity) : 0.0f;
        const PAGES_Core::Trend tr = hasDelta ? PAGES_Core::PressureTrend::classifyDelta(delta) : PAGES_Core::TREND_UNKNOWN;
        PAGES_Core::forecastText(d.pressureHpa, hasDelta, tr, d.hasT ? d.tempC : 15.0f, hum, dew, out, n, hasDelta ? delta : NAN);
        if (delta3hOut) *delta3hOut = hasDelta ? delta : NAN;
        return true;
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
