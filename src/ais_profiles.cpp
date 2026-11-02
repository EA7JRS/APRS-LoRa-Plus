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

// Intervalos de notificacion AIS (ITU-R M.1371-6, Anexo 1, A1-4 y A2-4.3):
//
//  Clase A (Cuadro 1)                                    Clase B "SO" (nominal)           Clase B "CS"
//  fondeado/amarrado, <= 3 kn ......... 3 min            <= 2 kn ........... 3 min        <= 2 kn ........ 3 min
//  0-14 kn ............................ 10 s             2-14 kn ........... 30 s         > 2 kn ......... 30 s
//  0-14 kn y cambiando de rumbo ....... 3 1/3 s          14-23 kn .......... 15 s         > 14 kn ........ 15 s
//  14-23 kn ........................... 6 s              14-23 kn y rumbo .. 5 s
//  14-23 kn y cambiando de rumbo ...... 2 s              > 23 kn ........... 5 s
//  > 23 kn ............................ 2 s
//
// Reglas del A2-4.3.1: un aumento de velocidad sube la tasa de inmediato; una bajada solo se aplica cuando ha durado
// 3 min (Clase A y Clase B "SO"). Cambio de rumbo: media del rumbo de los ultimos 30 s contra el rumbo actual, umbral
// de 5 grados, se mantiene hasta 20 s por debajo del umbral. Sin rumbo propio se usa el COG cuando la velocidad es
// mayor de 2 kn; por debajo, el rumbo no afecta a la tasa.
//
// El estado "fondeado/amarrado" de la Clase A lo da el usuario en un AIS real; aqui se considera amarrado un equipo parado.
// Los intervalos "modificados" por congestion del canal VHF (Clase B) no se aplican: no existe ese canal en LoRa.

#include "ais_profiles.h"
#include <math.h>

#ifndef AIS_MIN_INTERVAL_MS
    #define AIS_MIN_INTERVAL_MS     5000        // proteccion del canal LoRa: 0 = seguir la tabla AIS al pie de la letra
#endif
#ifndef AIS_MOORED_MAX_KMPH
    #define AIS_MOORED_MAX_KMPH     1.5f        // Clase A: por debajo se considera fondeado/amarrado
#endif

namespace {

    constexpr float     KMH_2KN             = 3.704f;       // 2 nudos
    constexpr float     KMH_14KN            = 25.928f;      // 14 nudos
    constexpr float     KMH_23KN            = 42.596f;      // 23 nudos
    constexpr float     COURSE_THRESHOLD    = 5.0f;
    constexpr float     GYRO_ROT_THRESHOLD  = 3.5f;         // >= 3,5 dps de giro en el IMU cuenta como maniobra (sin esperar al GPS)
    constexpr uint32_t  COURSE_WINDOW_MS    = 30000;
    constexpr uint32_t  COURSE_HOLD_MS      = 20000;
    constexpr uint32_t  SLOWDOWN_HOLD_MS    = 180000;
    constexpr uint8_t   MAX_SAMPLES         = 40;

    struct Sample {
        float       course;
        uint32_t    time;
    };

    Sample      samples[MAX_SAMPLES];
    uint8_t     sampleCount     = 0;
    bool        changing        = false;
    uint32_t    lastExceed      = 0;
    int8_t      appliedBand     = -1;
    bool        slowerPending   = false;
    uint32_t    slowerSince     = 0;
    uint8_t     lastMode        = 255;

    float wrap180(float degrees) {
        while (degrees > 180.0f)  degrees -= 360.0f;
        while (degrees < -180.0f) degrees += 360.0f;
        return degrees;
    }

    int8_t bandFor(uint8_t mode, float speed) {
        switch (mode) {
            case AIS_Profiles::CLASS_A:
                if (speed <= AIS_MOORED_MAX_KMPH) return 0;
                if (speed <= KMH_14KN) return 1;
                if (speed <= KMH_23KN) return 2;
                return 3;
            case AIS_Profiles::CLASS_B_SO:
                if (speed <= KMH_2KN) return 0;
                if (speed <= KMH_14KN) return 1;
                if (speed <= KMH_23KN) return 2;
                return 3;
            default:    // CLASS_B_CS
                if (speed <= KMH_2KN) return 0;
                if (speed <= KMH_14KN) return 1;
                return 2;
        }
    }

    uint32_t intervalFor(uint8_t mode, int8_t band, bool courseChange) {
        switch (mode) {
            case AIS_Profiles::CLASS_A:
                switch (band) {
                    case 0:  return 180000;
                    case 1:  return courseChange ? 3333 : 10000;
                    case 2:  return courseChange ? 2000 : 6000;
                    default: return 2000;
                }
            case AIS_Profiles::CLASS_B_SO:
                switch (band) {
                    case 0:  return 180000;
                    case 1:  return 30000;
                    case 2:  return courseChange ? 5000 : 15000;
                    default: return 5000;
                }
            default:    // CLASS_B_CS
                switch (band) {
                    case 0:  return 180000;
                    case 1:  return 30000;
                    default: return 15000;
                }
        }
    }

    void updateCourseChange(uint8_t mode, float speed, float course, bool valid, uint32_t now, float yawRateDps) {
        bool eligible = valid && speed > KMH_2KN && mode != AIS_Profiles::CLASS_B_CS;
        if (!eligible) {
            sampleCount = 0;
            changing    = false;
            return;
        }
        // descartar muestras de mas de 30 s y anadir la actual
        uint8_t kept = 0;
        for (uint8_t i = 0; i < sampleCount; i++) {
            if (now - samples[i].time <= COURSE_WINDOW_MS) samples[kept++] = samples[i];
        }
        sampleCount = kept;
        if (sampleCount == MAX_SAMPLES) {
            for (uint8_t i = 1; i < MAX_SAMPLES; i++) samples[i - 1] = samples[i];
            sampleCount--;
        }
        samples[sampleCount++] = {course, now};

        float sumCos = 0, sumSin = 0;
        for (uint8_t i = 0; i < sampleCount; i++) {
            float radians = samples[i].course * (float)M_PI / 180.0f;
            sumCos += cosf(radians);
            sumSin += sinf(radians);
        }
        float mean = atan2f(sumSin, sumCos) * 180.0f / (float)M_PI;
        bool gpsTurn  = fabsf(wrap180(course - mean)) > COURSE_THRESHOLD;
        bool gyroTurn = fabsf(yawRateDps) >= GYRO_ROT_THRESHOLD;
        if (gpsTurn || gyroTurn) {
            changing   = true;
            lastExceed = now ? now : 1;
        } else if (changing && (now - lastExceed) > COURSE_HOLD_MS) {
            changing = false;
        }
    }

}

namespace AIS_Profiles {

    void reset() {
        sampleCount   = 0;
        changing      = false;
        appliedBand   = -1;
        slowerPending = false;
    }

    uint32_t updateInterval(uint8_t mode, float speedKmph, float courseDeg, bool courseValid, uint32_t nowMs, float yawRateDps) {
        if (mode != lastMode) {
            reset();
            lastMode = mode;
        }
        updateCourseChange(mode, speedKmph, courseDeg, courseValid, nowMs, yawRateDps);

        int8_t newBand = bandFor(mode, speedKmph);
        if (appliedBand < 0 || newBand > appliedBand) {                 // mas velocidad: se aplica de inmediato
            appliedBand   = newBand;
            slowerPending = false;
        } else if (newBand < appliedBand) {
            if (mode == CLASS_B_CS) {                                   // la regla de los 3 min solo es para Clase A y B "SO"
                appliedBand = newBand;
            } else {
                if (!slowerPending) {
                    slowerPending = true;
                    slowerSince   = nowMs;
                }
                if (nowMs - slowerSince >= SLOWDOWN_HOLD_MS) {
                    appliedBand   = newBand;
                    slowerPending = false;
                }
            }
        } else {
            slowerPending = false;
        }

        uint32_t interval = intervalFor(mode, appliedBand, changing);
        uint32_t minimum = (uint32_t) AIS_MIN_INTERVAL_MS;
        return interval < minimum ? minimum : interval;
    }

    bool isChangingCourse() {
        return changing;
    }

    int8_t currentBand() {
        return appliedBand;
    }

}
