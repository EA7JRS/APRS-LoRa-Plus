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

#ifndef IMU_UTILS_H_
#define IMU_UTILS_H_

#include <Arduino.h>

// Tiempo mínimo de sueño del GPS en modo eco cuando el IMU confirma que el equipo está quieto
#ifndef IMU_STATIONARY_WAKE_MS
    #define IMU_STATIONARY_WAKE_MS      300000UL
#endif

namespace IMU_Utils {

    void  setup();
    void  update();
    bool  isReady();
    bool  movedSince(uint32_t sinceMs);     // hubo movimiento sostenido después de sinceMs
    bool  movingNow();                      // hubo movimiento sostenido en los últimos segundos
    bool  isFlat();                         // placa casi horizontal (true si no hay IMU: se asume horizontal)

    void  getAccelG(float& x, float& y, float& z);
    void  getGyroDps(float& x, float& y, float& z);
    float getAngularSpeedDps();             // magnitud de velocidad angular corregida (dps)
    float getYawRateDps();                  // velocidad de giro en eje Z (dps) para asistencia AIS/SmartBeacon
    float getTemperatureC();                // temperatura interna del QMI8658 (°C)

}

#endif