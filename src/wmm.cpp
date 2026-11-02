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

#include <math.h>
#include "wmm.h"
#include "wmm_coeffs.h"

namespace WMM {

    namespace {
        const int       MAX_N       = 12;
        const double    A_KM        = 6378.137;             // WGS84
        const double    F           = 1.0 / 298.257223563;
        const double    RE_KM       = 6371.2;               // radio de referencia geomagnetico
        const double    PI_VAL      = 3.14159265358979323846;
        const double    DEG         = PI_VAL / 180.0;
    }

    bool available() { return WMM_COUNT > 0; }
    double epoch() { return WMM_EPOCH; }

    bool declinationWith(const Coeff* table, size_t count, double epochYear, double latDeg, double lonDeg, double altKm, double decimalYear, float& declDeg) {
        if (table == nullptr || count == 0) return false;
        if (isnan(latDeg) || isnan(lonDeg) || latDeg < -90.0 || latDeg > 90.0 || lonDeg < -180.0 || lonDeg > 180.0) return false;
        if (isnan(altKm) || altKm < -10.0 || altKm > 100.0) altKm = 0.0;

        // geodesica -> geocentrica
        double phi = latDeg * DEG;
        double lambda = lonDeg * DEG;
        double e2 = F * (2.0 - F);
        double sinPhi = sin(phi), cosPhi = cos(phi);
        double rc = A_KM / sqrt(1.0 - e2 * sinPhi * sinPhi);
        double p = (rc + altKm) * cosPhi;
        double z = (rc * (1.0 - e2) + altKm) * sinPhi;
        double r = sqrt(p * p + z * z);
        if (r < 1.0) return false;

        double sinPhiGc = z / r;
        if (sinPhiGc > 1.0)  sinPhiGc = 1.0;
        if (sinPhiGc < -1.0) sinPhiGc = -1.0;
        double phiGc = asin(sinPhiGc);
        double theta = (PI_VAL / 2.0) - phiGc;              // colatitud
        double sinT = sin(theta), cosT = cos(theta);
        if (sinT < 1e-9) sinT = 1e-9;                       // en los polos la declinacion no esta definida

        // polinomios de Legendre con semi-normalizacion de Schmidt y sus derivadas respecto a theta
        double P[MAX_N + 1][MAX_N + 1]  = {{0.0}};
        double dP[MAX_N + 1][MAX_N + 1] = {{0.0}};
        P[0][0] = 1.0;
        dP[0][0] = 0.0;

        for (int n = 1; n <= MAX_N; n++) {
            for (int m = 0; m <= n; m++) {
                if (n == m) {
                    double k = (n == 1) ? 1.0 : sqrt((2.0 * n - 1.0) / (2.0 * n));
                    P[n][n]  = k * sinT * P[n - 1][n - 1];
                    dP[n][n] = k * (cosT * P[n - 1][n - 1] + sinT * dP[n - 1][n - 1]);
                } else if (n == m + 1) {
                    double k = sqrt(2.0 * n - 1.0);
                    P[n][m]  = k * cosT * P[n - 1][m];
                    dP[n][m] = k * (cosT * dP[n - 1][m] - sinT * P[n - 1][m]);
                } else {
                    double d = sqrt((double)n * n - (double)m * m);
                    double k = sqrt(((double)n - 1.0 + m) * ((double)n - 1.0 - m));
                    P[n][m]  = ((2.0 * n - 1.0) * cosT * P[n - 1][m] - k * P[n - 2][m]) / d;
                    dP[n][m] = ((2.0 * n - 1.0) * (cosT * dP[n - 1][m] - sinT * P[n - 1][m]) - k * dP[n - 2][m]) / d;
                }
            }
        }

        // Precalcular potencias radiales (RE_KM / r)^(n + 2) y armonicos longitudinales cos(m * lambda), sin(m * lambda)
        double ratio = RE_KM / r;
        double radial[MAX_N + 1];
        radial[0] = ratio * ratio;
        for (int n = 1; n <= MAX_N; n++) {
            radial[n] = radial[n - 1] * ratio;
        }

        double cosM[MAX_N + 1], sinM[MAX_N + 1];
        cosM[0] = 1.0;
        sinM[0] = 0.0;
        cosM[1] = cos(lambda);
        sinM[1] = sin(lambda);
        for (int m = 2; m <= MAX_N; m++) {
            cosM[m] = cosM[m - 1] * cosM[1] - sinM[m - 1] * sinM[1];
            sinM[m] = sinM[m - 1] * cosM[1] + cosM[m - 1] * sinM[1];
        }

        double dt = isnan(decimalYear) ? 1.5 : (decimalYear - epochYear);
        if (dt < -1.0) dt = -1.0;
        if (dt > 6.0)  dt = 6.0;                            // acotar variacion secular a la ventana util del modelo

        double xGc = 0.0, yGc = 0.0, zGc = 0.0;
        for (size_t i = 0; i < count; i++) {
            int n = table[i].n, m = table[i].m;
            if (n < 1 || n > MAX_N || m < 0 || m > n) continue;
            double g = table[i].g + table[i].gDot * dt;
            double h = table[i].h + table[i].hDot * dt;
            double rad = radial[n];
            double cm = cosM[m], sm = sinM[m];
            double t = g * cm + h * sm;
            xGc += rad * t * dP[n][m];
            yGc += rad * m * (g * sm - h * cm) * P[n][m] / sinT;
            zGc -= rad * (n + 1) * t * P[n][m];
        }

        // geocentrica -> geodesica
        double psi = phiGc - phi;
        double x = xGc * cos(psi) - zGc * sin(psi);
        double y = yGc;
        declDeg = (float)(atan2(y, x) / DEG);
        return true;
    }

    bool declination(double latDeg, double lonDeg, double altKm, double decimalYear, float& declDeg) {
        return declinationWith(WMM_TABLE, WMM_COUNT, WMM_EPOCH, latDeg, lonDeg, altKm, decimalYear, declDeg);
    }

}