/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Meteorologia: punto de rocio, sensacion termica (viento y calor), humedad desde el rocio, presion al nivel del mar.
#ifndef PLOTTER_WXCALC_H_
#define PLOTTER_WXCALC_H_

namespace plotter {

// Punto de rocio (C) con la formula de Magnus (Alduchov y Eskridge, 1996: error < 0,4 C entre -40 y 50 C).
// rh en %, 1..100; fuera de rango devuelve NAN.
double dewPointC(double tempC, double rhPct);
// Humedad relativa (%) a partir de la temperatura y el punto de rocio
double relHumidityPct(double tempC, double dewC);
// Indice de enfriamiento por viento (C), formula de Environment Canada / NWS de 2001.
// Solo vale con tempC <= 10 y viento > 4,8 km/h; fuera de eso devuelve tempC.
double windChillC(double tempC, double windKmh);
// Indice de calor (C), regresion de Rothfusz (NWS). Solo con tempC >= 27 y rh >= 40; fuera, tempC.
double heatIndexC(double tempC, double rhPct);
// Sensacion termica: enfriamiento por viento si hace frio, indice de calor si hace calor; si no, la temperatura.
// rhPct < 0 = sin humedad; windKmh < 0 = sin viento.
double feelsLikeC(double tempC, double rhPct, double windKmh);
// Presion reducida al nivel del mar (hPa) desde la de la estacion, su altitud (m) y la temperatura (formula hipsometrica)
double seaLevelPressureHpa(double stationHpa, double altM, double tempC);

}  // namespace plotter
#endif
