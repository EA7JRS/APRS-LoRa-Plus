/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Geodesia: distancias, demoras, desvio de ruta. Angulos en grados, distancias en millas nauticas.
#ifndef PLOTTER_GEO_H_
#define PLOTTER_GEO_H_

namespace plotter {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371008.8;          // radio medio (IUGG)
constexpr double kMetersPerNm = 1852.0;

inline double deg2rad(double d) { return d * kPi / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / kPi; }
double norm360(double deg);                           // 0..360
double norm180(double deg);                           // -180..180

struct LatLon { double lat; double lon; };

// Circulo maximo sobre la esfera (haversine)
double gcDistanceNm(LatLon a, LatLon b);
double gcInitialBearing(LatLon a, LatLon b);          // verdadero, 0..360
LatLon gcDestination(LatLon from, double bearingDeg, double distNm);

// Loxodromica (rumbo constante)
double rhumbDistanceNm(LatLon a, LatLon b);
double rhumbBearing(LatLon a, LatLon b);

// Elipsoide WGS84 (Vincenty inverso). false si no converge (puntos casi antipodas).
bool vincentyInverse(LatLon a, LatLon b, double& distM, double& initialBearingDeg);

// Desvio de ruta respecto al tramo a->b: xteNm > 0 a estribor del tramo; alongNm = avance sobre el tramo
void crossTrack(LatLon a, LatLon b, LatLon p, double& xteNm, double& alongNm);

}  // namespace plotter
#endif
