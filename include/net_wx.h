/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tiempo y calidad del aire de internet para la pagina TELEMETRY: la app Android (orden BLE "wxnet") o el navegador de la WebUI
 * (POST /api/wxnet, claves en Telemetry > Internet weather) los descargan de OpenWeatherMap / IQAir AirVisual en la posicion del
 * tracker. Se muestran como datos de internet ("NET" y la fuente), nunca como un sensor del tracker, y no van en las balizas APRS de
 * meteorologia. Caducan a las 2 h. La presion es ademas el QNH del altimetro sin GPS y, con su tendencia de 3 h (observaciones con
 * hora), entra en el pronostico de TELEMETRY cuando el tracker no tiene barometro (y la humedad, cuando no tiene higrometro).
 */

#ifndef NET_WX_H_
#define NET_WX_H_

#include <Arduino.h>
#include <ArduinoJson.h>

namespace NET_WX {

    struct Data {
        char     src[12];               // "OWM", "IQAir", "OWM+IQAir"
        char     place[21];
        char     desc[21];
        char     pollutant[7];          // contaminante principal (IQAir): "PM2.5"
        bool     hasT, hasH, hasP, hasWind, hasGust, hasClouds, hasRain, hasPm25, hasPm10;
        float    tempC, humidity, pressureHpa, windMs, gustMs, rainMmH, pm25, pm10;
        int16_t  windDeg;               // -1 = sin dato
        int8_t   clouds;
        int8_t   aqiOwm;                // 1..5 (0 = sin dato)
        int16_t  aqiUs;                 // 0..500 (-1 = sin dato)
        uint32_t ageAtRxS;              // antiguedad de la observacion al recibirla
        uint32_t rxMs;                  // millis() al recibirla
    };

    void        set(const Data& d);
    bool        fromJson(JsonVariantConst doc, Data& d);    // campos de la orden "wxnet" (src, pl, ds, mp, t, h, p, ws, wg, wd, r, cl, aq, aqus, pm25, pm10, age)
    void        apply(const Data& d);                       // set() + QNH del altimetro
    bool        delta3h(float& hpa);                        // tendencia de la presion de internet (observaciones de 1 h a 3,5 h)
    // Pronostico con lo de internet (presion, tendencia, temperatura y humedad), el mismo de TELEMETRY; false sin presion
    bool        forecast(char* out, size_t n, float* delta3hOut = nullptr);
    bool        get(Data& out);         // false si no hay datos o tienen mas de 2 h
    uint32_t    ageS();                 // antiguedad de la observacion ahora
    const char* owmAqiName(int a);      // 1 Good .. 5 Very poor
    const char* usAqiName(int a);       // Good, Moderate, USG, Unhealthy, V.unhealthy, Hazardous
}

#endif
