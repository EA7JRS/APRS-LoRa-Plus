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

#ifndef MAG_UTILS_H_
#define MAG_UTILS_H_

namespace MAG_Utils {

    void    setup();
    void    update();
    bool    isReady();
    bool    isCalibrated();
    bool    isHeadingValid();   // calibrada, placa horizontal y campo coherente con la calibracion
    float   heading();      // grados 0-360, solo válido si isCalibrated()
    void    feedGpsCourse(float courseDeg, float speedKmph, bool gpsGood);     // autoalineacion con el rumbo GPS
    bool    isAligned();                                                        // ya hay desfase aprendido (el rumbo sale referido al norte verdadero)
    float   alignmentOffset();                                                  // grados aprendidos (0 si no hay)
    void    resetAlignment();

}

#endif
