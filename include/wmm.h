/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
 * Copyright (C) 2026 Manuel - EA7JRS (modificaciones y ampliaciones)
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

#ifndef WMM_H_
#define WMM_H_

#include <stdint.h>
#include <stddef.h>

// Declinacion magnetica con el World Magnetic Model (NOAA/BGS), armonicos esfericos de grado 12.
// Los coeficientes estan en wmm_coeffs.h (generado con tools/wmm_cof_to_header.py a partir del fichero .COF oficial).
// El modelo cubre 5 anos desde su epoca; fuera de ese margen el error crece y available() lo indica.
namespace WMM {

    struct Coeff {
        uint8_t n, m;
        float   g, h;       // nT en la epoca
        float   gDot, hDot; // nT/ano
    };

    // Hay tabla de coeficientes compilada
    bool    available();
    double  epoch();
    // Declinacion en grados (positiva al este). latitud geodesica, altitud en km sobre el elipsoide, anio decimal.
    bool    declination(double latDeg, double lonDeg, double altKm, double decimalYear, float& declDeg);
    // Igual, con una tabla propia (pruebas)
    bool    declinationWith(const Coeff* table, size_t count, double epochYear, double latDeg, double lonDeg, double altKm, double decimalYear, float& declDeg);

}

#endif
