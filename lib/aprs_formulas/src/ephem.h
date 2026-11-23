/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Sol, Luna y Tierra: lo que se deduce de las efemerides de celnav (Meeus) y de las constantes fisicas de los tres cuerpos.
// Fase verdadera de la Luna (no la del mes sinodico medio), proximas fases, perigeo y apogeo, eclipses, indice de marea de
// equilibrio, rebote lunar (EME), distancia e irradiancia del Sol, estaciones, perihelio, rotacion de Carrington, indice
// UV de cielo despejado, horizonte optico y de radio, y gravedad normal. Sin Arduino. Pruebas en test/host.
// Constantes (IAU 2015 / WGS84): radio medio de la Luna 1737,4 km; excentricidad media de su orbita 0,0549; inclinacion
// 5,145 grados; meses sinodico 29,530589 d, anomalistico 27,554550 d y draconico 27,212221 d; albedo de radar 0,065;
// radio del Sol 695 700 km; constante solar 1361 W/m2; 1 UA = 149 597 870,7 km; elipsoide a = 6 378 137 m, 1/f = 298,257223563.
#pragma once

namespace plotter {

constexpr double kAuKm          = 149597870.7;
constexpr double kMoonRadiusKm  = 1737.4;
constexpr double kMoonMeanKm    = 384400.0;
constexpr double kSunRadiusKm   = 695700.0;
constexpr double kSolarConstant = 1361.0;          // W/m2 a 1 UA
constexpr double kSynodicMonth  = 29.530588853;
constexpr double kLightKmS      = 299792.458;

// ---- Luna ----
struct MoonPhase {
    double elongDeg;        // longitud ecliptica Luna - Sol, 0..360 (0 nueva, 90 cuarto creciente, 180 llena, 270 menguante)
    double illum;           // fraccion iluminada del disco, 0..1 (con el angulo de fase verdadero)
    double ageDays;         // dias desde la ultima luna nueva verdadera
    int    phase8;          // 0 nueva .. 4 llena .. 7 menguante (por la elongacion, octavos de 45 grados)
    bool   waxing;
};
MoonPhase moonPhaseTrue(double jd);
// Instante (dia juliano UTC) de la proxima fase principal despues de jd: quarter 0 nueva, 1 creciente, 2 llena, 3 menguante
double nextMoonPhase(double jd, int quarter);
// Proximo perigeo (true) o apogeo (false) despues de jd y su distancia geocentrica en km
double nextMoonApsis(double jd, bool perigee, double& distKm);
double moonAngularDiameterArcmin(double distKm);

// Eclipses. De Luna: se ve desde cualquier lugar con la Luna sobre el horizonte. De Sol: solo se dice que lo hay en algun
// lugar de la Tierra y de que tipo es en la linea central; las circunstancias locales no se calculan.
enum class EclipseKind : unsigned char { None, LunarPenumbral, LunarPartial, LunarTotal, SolarPartial, SolarAnnular, SolarTotal };
struct Eclipse {
    EclipseKind kind = EclipseKind::None;
    double jd = 0;          // oposicion o conjuncion en longitud (a menos de unos minutos del maximo)
    double magnitude = 0;   // Luna: magnitud umbral (penumbral en los penumbrales); Sol: gamma (distancia del eje de sombra al centro, radios terrestres)
};
Eclipse eclipseAtSyzygy(double jdSyzygy, bool fullMoon);
// Proximo eclipse de Luna (lunar = true) o de Sol despues de jd, mirando hasta maxLunations lunaciones (None si no hay)
Eclipse nextEclipse(double jd, bool lunar, int maxLunations = 14);
const char* eclipseNameEs(EclipseKind k);
const char* eclipseNameEn(EclipseKind k);

// Marea de equilibrio: fuerza generadora de marea de la Luna y el Sol juntos frente a la de sicigia media (= 100).
// Depende de la fase (vivas en nueva y llena, muertas en cuartos) y de las distancias (perigeo: mas). Orienta sobre el
// coeficiente del dia; no es una prediccion de altura ni de hora para ningun puerto.
double tideForceIndex(double jd);

// Rebote lunar (EME) desde lat / lon para la frecuencia freqHz: perdida del trayecto de ida y vuelta (dB, ecuacion de radar
// con albedo 0,065), retardo del eco (s), Doppler del propio eco (Hz) y distancia topocentrica (km)
struct EmeInfo { double pathLossDb, echoDelayS, dopplerHz, rangeKm; };
EmeInfo emeInfo(double jd, double lat, double lon, double freqHz);

// ---- Sol ----
double sunDistanceAu(double jd);
double sunAngularDiameterArcmin(double jd);
double solarIrradianceToa(double jd);                          // W/m2 fuera de la atmosfera, perpendicular al Sol
// Indice UV de cielo despejado (Allaart et al., 2004): altura del sol sobre el horizonte, distancia en UA y ozono en DU
double clearSkyUvIndex(double sunAltDeg, double distAu, double ozoneDu = 300.0);
double carringtonRotation(double jd);                          // numero de rotacion sinodica del Sol (27,2753 d), con decimales
// Proximo equinoccio o solsticio despues de jd; season: 0 marzo, 1 junio, 2 septiembre, 3 diciembre
double nextSeason(double jd, int& season);
// Proximo perihelio (true) o afelio (false) de la Tierra y su distancia en UA
double nextEarthApsis(double jd, bool perihelion, double& distAu);

// ---- Tierra ----
double horizonKm(double heightM);                              // horizonte geometrico (sin refraccion)
double radioHorizonKm(double heightM);                         // con la refraccion normal de VHF / UHF (radio 4/3)
double radioRangeKm(double h1M, double h2M);                   // alcance de radio entre dos antenas sobre mar o llano
double opticalRangeNm(double eyeM, double lightM);             // alcance geografico de una luz (millas, 2,03 x raiz de la altura)
double normalGravity(double latDeg, double heightM);           // m/s2 (WGS84, Somigliana + aire libre)
double earthRadiusAtM(double latDeg);                          // radio geocentrico del elipsoide WGS84 en esa latitud (m)

// Dia juliano UTC -> fecha y hora
void jdToUtc(double jd, int& y, int& m, int& d, double& utcHours);

}  // namespace plotter
