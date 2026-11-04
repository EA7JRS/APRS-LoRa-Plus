/* Copyright (C) 2026 Manuel - EA7JRS
 * Teclado Yaesu FH-2 (escalera de resistencias) leido por un pin ADC: el teclado va entre el pin y GND y el pin lleva una resistencia
 * (10 kohm) a 3V3. Cada tecla da una tension distinta; se calibran las teclas 1-5 desde la WebUI y aqui se reconocen por la mas cercana.
 * La parte de clasificacion no depende de Arduino (se prueba en PC).
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef KEYPAD_FH2_H_
#define KEYPAD_FH2_H_

#include <stdint.h>

#ifdef ARDUINO
    #include <Arduino.h>
    #include "sdkconfig.h"
#endif

namespace FH2 {

    constexpr int IDLE_MV   = 3000;     // sin tecla pulsada el pin queda cerca de 3V3
    constexpr int MIN_TOL   = 60;       // tolerancia minima en mV

    // Pines ADC1 (el ADC2 no funciona con el WiFi encendido, y la WebUI lo usa)
    inline bool adcPinOk(int pin) {
        #if defined(CONFIG_IDF_TARGET_ESP32S3)
            return pin >= 1 && pin <= 10;
        #elif defined(CONFIG_IDF_TARGET_ESP32C3)
            return pin >= 0 && pin <= 4;
        #elif defined(CONFIG_IDF_TARGET_ESP32)
            return pin >= 32 && pin <= 39;
        #else
            return pin >= 0;
        #endif
    }

    // Indice (0..n-1) de la tecla calibrada mas cercana a mv, o -1 si no hay tecla pulsada o no coincide con ninguna
    inline int classify(int mv, const int* cal, int n) {
        if (mv <= 0 || mv >= IDLE_MV) return -1;
        int best = -1, bestDiff = 1 << 30;
        for (int i = 0; i < n; i++) {
            if (cal[i] <= 0) continue;
            const int d = mv > cal[i] ? mv - cal[i] : cal[i] - mv;
            if (d < bestDiff) { bestDiff = d; best = i; }
        }
        if (best < 0) return -1;
        int minGap = 1 << 30;                                       // distancia a la calibracion vecina mas cercana
        for (int i = 0; i < n; i++) {
            if (i == best || cal[i] <= 0) continue;
            const int g = cal[best] > cal[i] ? cal[best] - cal[i] : cal[i] - cal[best];
            if (g < minGap) minGap = g;
        }
        int tol = minGap == (1 << 30) ? 300 : minGap / 2;
        if (tol < MIN_TOL) tol = MIN_TOL;
        return bestDiff <= tol ? best : -1;
    }

#ifdef ARDUINO
    // Media de 8 lecturas en mV
    inline int readMv(int pin) {
        pinMode(pin, INPUT);
        long sum = 0;
        for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(pin);
        return (int)(sum / 8);
    }
#endif

}

#endif
