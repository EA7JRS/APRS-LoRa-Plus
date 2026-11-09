/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

// Barometro + GPS: presion a nivel del mar con la formula completa (no la aproximacion lineal), humedad coherente con la
// correccion de temperatura y altitud fusionada (filtro complementario).
//
// La referencia de presion al nivel del mar (QNH) se aprende lentamente de la altitud GPS cuando el fix es bueno
// (tau ~ 5 min); con ella la altitud sale del barometro, que es mucho mas estable que la altitud GPS a corto plazo,
// y a largo plazo sigue al GPS. Si el GPS falta mas de 20 min, la altitud barometrica se descarta.

#include "baro_utils.h"
#include <math.h>

namespace {

    constexpr float     LAPSE_RATE          = 0.0065f;      // K/m
    constexpr float     EXPONENT            = 5.257f;
    constexpr float     QNH_TAU_MS          = 300000.0f;
    constexpr float     ALT_TAU_MS          = 4000.0f;
    constexpr uint32_t  QNH_MAX_AGE_MS      = 1200000UL;
    constexpr float     MIN_QNH             = 900.0f;
    constexpr float     MAX_QNH             = 1090.0f;

    bool        manualQnh       = false;
    bool        haveQnh         = false;
    float       qnh             = 1013.25f;
    uint32_t    qnhTime         = 0;
    bool        haveAlt         = false;
    float       altSmooth       = 0.0f;
    uint32_t    lastFeed        = 0;
    bool        haveFeed        = false;
    bool        haveFallback    = false;
    float       fallbackAlt     = 0.0f;
    // QNH de internet (la app: OpenWeatherMap / IQAir, orden BLE "wxnet"): solo cuando no hay QNH manual ni aprendido del GPS
    // reciente; nunca se usa como presion de la estacion (balizas de meteorologia)
    constexpr uint32_t  NET_QNH_MAX_AGE_MS  = 7200000UL;
    bool        haveNet         = false;
    bool        usingNet        = false;
    float       netQnh          = 0.0f;
    uint32_t    netQnhTime      = 0;

    bool netFresh(uint32_t nowMs) { return haveNet && nowMs - netQnhTime <= NET_QNH_MAX_AGE_MS; }

    float saturationPressure(float tempC) {
        return 6.112f * expf(17.62f * tempC / (243.12f + tempC));
    }

}

namespace BARO_Utils {

    float seaLevelPressure(float stationHpa, float altitudeM, float tempC) {
        float base = 1.0f - (LAPSE_RATE * altitudeM) / (tempC + LAPSE_RATE * altitudeM + 273.15f);
        return stationHpa * powf(base, -EXPONENT);
    }

    float altitudeFromPressure(float stationHpa, float seaLevelHpa, float tempC) {
        return ((powf(seaLevelHpa / stationHpa, 1.0f / EXPONENT) - 1.0f) * (tempC + 273.15f)) / LAPSE_RATE;
    }

    // Mantiene el punto de rocio: la humedad relativa cambia al corregir la temperatura
    float correctHumidity(float relativeHumidity, float rawTempC, float correctedTempC) {
        float corrected = relativeHumidity * saturationPressure(rawTempC) / saturationPressure(correctedTempC);
        if (corrected < 0.0f)   corrected = 0.0f;
        if (corrected > 100.0f) corrected = 100.0f;
        return corrected;
    }

    void feed(float stationHpa, float tempC, bool gpsGood, float gpsAltitudeM, uint32_t nowMs) {
        if (isnan(stationHpa) || isnan(tempC) || stationHpa < 300.0f || stationHpa > 1100.0f) return;
        float dt = haveFeed ? (float)(nowMs - lastFeed) : 0.0f;
        if (dt > 60000.0f) dt = 60000.0f;
        lastFeed = nowMs;
        haveFeed = true;

        if (gpsGood && !manualQnh) {
            float sample = seaLevelPressure(stationHpa, gpsAltitudeM, tempC);
            if (sample > MIN_QNH && sample < MAX_QNH) {
                usingNet = false;                               // el GPS manda (corrige ademas el desfase del sensor)
                if (!haveQnh) {
                    qnh = sample;
                } else {
                    float step = (float)(nowMs - qnhTime);
                    if (step > 60000.0f) step = 60000.0f;
                    qnh += (sample - qnh) * (step / (QNH_TAU_MS + step));
                }
                haveQnh = true;
                qnhTime = nowMs;
            }
        }
        const bool gpsQnhFresh = haveQnh && !usingNet && !manualQnh && (nowMs - qnhTime) <= QNH_MAX_AGE_MS;
        if (!manualQnh && !gpsQnhFresh && netFresh(nowMs)) {
            if (!usingNet) haveAlt = false;
            qnh = netQnh; haveQnh = true; usingNet = true;
        } else if (usingNet && !netFresh(nowMs)) {
            usingNet = false; haveQnh = false;
        }
        if (haveQnh && (manualQnh || usingNet || (nowMs - qnhTime) <= QNH_MAX_AGE_MS)) {
            float instant = altitudeFromPressure(stationHpa, qnh, tempC);
            if (!haveAlt) {
                altSmooth = instant;
            } else {
                altSmooth += (instant - altSmooth) * (dt / (ALT_TAU_MS + dt));
            }
            haveAlt = true;
        } else {
            haveAlt = false;
        }
    }

    void setFallbackAltitude(float altitudeM) {
        haveFallback = true;
        fallbackAlt  = altitudeM;
    }

    float reducedPressure(float stationHpa, float tempC) {
        if (haveQnh && haveFeed && !usingNet && (manualQnh || (lastFeed - qnhTime) <= QNH_MAX_AGE_MS)) return qnh;
        if (haveFallback) return seaLevelPressure(stationHpa, fallbackAlt, tempC);
        return stationHpa;
    }

    bool altitudeValid() {
        return haveAlt;
    }

    float getAltitude() {
        return altSmooth;
    }

    void setManualQnh(float hpa) {
        if (isnan(hpa)) return;
        if (hpa < MIN_QNH) hpa = MIN_QNH;
        if (hpa > MAX_QNH) hpa = MAX_QNH;
        qnh = hpa;
        manualQnh = true;
        haveQnh = true;
        usingNet = false;
        haveAlt = false;                                        // la altitud se recalcula con el QNH nuevo en la proxima muestra
    }

    void clearManualQnh() {
        manualQnh = false;
        haveQnh = false;
        usingNet = false;                                        // se vuelve a aprender del GPS
        haveAlt = false;
    }

    bool isManualQnh() {
        return manualQnh;
    }

    void setNetQnh(float hpa, uint32_t nowMs) {
        if (isnan(hpa) || hpa <= MIN_QNH || hpa >= MAX_QNH) return;
        netQnh = hpa; netQnhTime = nowMs; haveNet = true;
        if (usingNet) qnh = hpa;
    }

    bool isNetQnh() {
        return usingNet && !manualQnh;
    }

    float getQnh() {
        return haveQnh ? qnh : 0.0f;
    }

    void reset() {
        manualQnh = false;
        haveQnh = haveAlt = haveFeed = haveFallback = haveNet = usingNet = false;
        qnh = 1013.25f;
        altSmooth = 0.0f;
    }

}
