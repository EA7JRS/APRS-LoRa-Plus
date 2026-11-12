/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Sol y luna. Formulas de NOAA (serie de Fourier): precision de alrededor de un minuto en latitudes medias.
// Pendiente: algoritmos de Meeus para el almanaque (fase 4).
#ifndef PLOTTER_ASTRO_H_
#define PLOTTER_ASTRO_H_

namespace plotter {

int dayOfYear(int y, int m, int d);
double julianDay(int y, int m, int d, double utcHours);   // dia juliano (UTC)

// Paso del sol por la altura altDeg (-0.833 orto/ocaso, -6 civil, -12 nautico, -18 astronomico).
// Minutos UTC del dia; pueden salir < 0 o > 1440 segun la longitud. false: no la cruza ese dia (dia o noche polar).
bool sunTimes(int y, int m, int d, double lat, double lon, double altDeg, double& riseUtcMin, double& setUtcMin);
double solarNoonUtcMin(int y, int m, int d, double lon);

// Altura y azimut del sol (grados; azimut desde el norte verdadero; Meeus, ~0,01 grados, sin refraccion)
void sunPosition(int y, int m, int d, double utcHours, double lat, double lon, double& altDeg, double& azDeg);

// Edad de la luna en dias (0 = nueva) con el mes sinodico medio, y fraccion iluminada 0..1
double moonAgeDays(int y, int m, int d, double utcHours);
double moonIllumination(double ageDays);
// Fase en 8 partes (0 nueva, 2 cuarto creciente, 4 llena, 6 cuarto menguante) y su nombre
int moonPhase8(double ageDays);
const char* moonPhaseNameEs(int phase8);
const char* moonPhaseNameEn(int phase8);

// Luna (Meeus, cap. 47, terminos principales: ~0,05 grados). Altura y azimut topocentricos (con el paralaje, que en la
// luna llega a 1 grado), sin refraccion; distancia en km. Para apuntar una antena (rebote lunar) sumar refractionDeg().
void moonPosition(int y, int m, int d, double utcHours, double lat, double lon, double& altDeg, double& azDeg, double& distKm);
// Salida y puesta de la luna ese dia UTC (minutos UTC, 0..1440); hasRise / hasSet = false si no ocurre ese dia
void moonTimes(int y, int m, int d, double lat, double lon, double& riseUtcMin, bool& hasRise, double& setUtcMin, bool& hasSet);

// Refraccion atmosferica media (grados, formula de Saemundsson) para una altura verdadera: altura aparente = alt + esto
double refractionDeg(double trueAltDeg);
// Fecha y hora UTC desde el tiempo Unix
void unixToUtc(long long unixS, int& y, int& m, int& d, double& utcHours);

}  // namespace plotter
#endif
