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

#ifndef BARO_UTILS_H_
#define BARO_UTILS_H_

#include <stdint.h>

namespace BARO_Utils {

    // Formulas (presion en hPa, altitud en m, temperatura en grados C)
    float   seaLevelPressure(float stationHpa, float altitudeM, float tempC);
    float   altitudeFromPressure(float stationHpa, float seaLevelHpa, float tempC);
    float   correctHumidity(float relativeHumidity, float rawTempC, float correctedTempC);

    // Fusion barometro + GPS
    void    feed(float stationHpa, float tempC, bool gpsGood, float gpsAltitudeM, uint32_t nowMs);
    void    setFallbackAltitude(float altitudeM);
    float   reducedPressure(float stationHpa, float tempC);     // presion a nivel del mar con la mejor altitud disponible
    bool    altitudeValid();
    float   getAltitude();                                      // altitud fusionada (m)
    // QNH manual (ajuste contra un METAR o una estacion cercana): no se aprende del GPS ni caduca hasta volver a automatico
    void    setManualQnh(float hpa);                            // se limita a 900..1090 hPa
    void    clearManualQnh();                                   // vuelve a aprender el QNH del GPS
    bool    isManualQnh();
    // QNH de internet (presion al nivel del mar de OpenWeatherMap / IQAir que manda la app): se usa si no hay QNH manual ni uno
    // aprendido del GPS en los ultimos 20 min; caduca a las 2 h
    void    setNetQnh(float hpa, uint32_t nowMs);
    bool    isNetQnh();
    float   getQnh();                                           // QNH en uso; 0 si aun no se conoce
    void    reset();

}

#endif
