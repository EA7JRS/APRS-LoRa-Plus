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

#ifndef AIS_PROFILES_H_
#define AIS_PROFILES_H_

#include <stdint.h>

// Perfiles de SmartBeacon basados en los intervalos de notificacion de ITU-R M.1371-6 (Cuadros 1 y 2 del Anexo 1)
namespace AIS_Profiles {

    enum Mode : uint8_t {
        CLASSIC     = 0,        // SmartBeacon normal (interpolacion entre velocidad lenta y rapida)
        CLASS_A     = 1,        // Clase A (Cuadro 1)
        CLASS_B_SO  = 2,        // Clase B "SO" (Cuadro 2, intervalo nominal)
        CLASS_B_CS  = 3         // Clase B "CS" (Cuadro 2)
    };

    // Llamar con cada actualizacion del GPS (~1 Hz). Devuelve el intervalo de transmision en ms.
    // yawRateDps (opcional): velocidad angular del giroscopio (grados/s) para detectar viradas al instante.
    uint32_t    updateInterval(uint8_t mode, float speedKmph, float courseDeg, bool courseValid, uint32_t nowMs, float yawRateDps = 0.0f);
    bool        isChangingCourse();
    int8_t      currentBand();      // -1 si aun no hay datos
    void        reset();

}

#endif