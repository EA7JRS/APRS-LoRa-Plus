/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Navegacion astronomica: lo que da un almanaque nautico, calculado a bordo. Angulo horario en Greenwich (AHG) y
// declinacion del Sol, AHG de Aries y angulo sidereo (AHS) y declinacion de las 57 estrellas de navegacion y la Polar,
// con precision de almanaque (0,1'). Sol: serie VSOP87 recortada (Meeus, "Astronomical Algorithms", ap. III), nutacion y
// aberracion. Estrellas: movimiento propio, precesion, nutacion y aberracion anual (Meeus, cap. 21 a 23).
// Contrastado con PyEphem y con un almanaque nautico de 2026 (test/host). Sin Arduino.
#pragma once

namespace plotter {

struct NavStar {
    const char* name;
    float mag;
    double raHours, decDeg;        // J2000
    double pmRaMas, pmDecMas;      // movimiento propio, milisegundos de arco por anio (el de AR, ya por cos dec)
};

int navStarCount();                              // 58: las 57 de navegacion por orden alfabetico y la Polar
int starCount();                                 // todas: las 58 y, detras, otras 99 brillantes de los almanaques (catalogo HYG / Hipparcos)
const NavStar& navStar(int i);                   // 0 .. starCount() - 1
int navStarFind(const char* name);               // sin distinguir mayusculas; -1 si no esta

// jd: dia juliano en UTC (julianDay de astro.h). Angulos en grados: AHG y AHS de 0 a 360 hacia el oeste, dec + al norte.
double ariesGha(double jd);                      // AHG de Aries (tiempo sidereo aparente en Greenwich)
void sunGhaDec(double jd, double& ghaDeg, double& decDeg);
double sunSemiDiameterArcmin(double jd);
double equationOfTimeMin(double jd);             // tiempo verdadero - tiempo medio, en minutos
void starShaDec(const NavStar& s, double jd, double& shaDeg, double& decDeg);   // lugar aparente de la fecha

// Luna (Meeus, cap. 47, tablas completas; ~0,25' frente a las efemerides del JPL): AHG, declinacion y distancia
// geocentricas. Paralaje horizontal y semidiametro con moonHpArcmin / moonSdArcmin (54' a 61' y 14,7' a 16,7').
void moonGhaDec(double jd, double& ghaDeg, double& decDeg, double& distKm);
double moonHpArcmin(double distKm);
double moonSdArcmin(double distKm);

// Planetas de navegacion (VSOP87 recortada, tiempo de luz, aberracion y nutacion; ~0,06' frente a PyEphem).
enum class Planet : unsigned char { Venus, Mars, Jupiter, Saturn };
const char* planetNameEs(Planet p);
const char* planetNameEn(Planet p);
void planetGhaDec(Planet p, double jd, double& ghaDeg, double& decDeg, double& distAu);
double planetHpArcmin(double distAu);            // paralaje horizontal: 0,1' a 0,5' en Venus y Marte cercanos

// Altura verdadera y azimut de un astro (AHG y declinacion) desde una posicion; lon + al este
void bodyAltAz(double ghaDeg, double decDeg, double lat, double lon, double& altDeg, double& azDeg);

// ---- Correcciones de la altura de sextante y recta de altura ----
double dipArcmin(double eyeHeightM);             // depresion del horizonte (1,76' x raiz de la elevacion del ojo en metros)
double refractionArcmin(double apparentAltDeg);  // refraccion media (Bennett; 10 C y 1010 hPa), para la altura aparente

enum class Limb : unsigned char { Center, Lower, Upper };   // estrellas: Center

// Altura observada (verdadera) desde la de sextante. indexErrArcmin: error de indice (+ si el sextante marca de mas);
// semiDiameterArcmin y parallaxArcmin: del Sol 15,7' a 16,3' y 0,15'; de una estrella, 0 y 0.
double observedAltitude(double sextantAltDeg, double indexErrArcmin, double eyeHeightM, Limb limb,
                        double semiDiameterArcmin, double parallaxArcmin);

struct Sight {
    double hcDeg = 0, znDeg = 0;     // altura calculada y azimut verdadero desde la posicion de estima
    double interceptNm = 0;          // determinante: + hacia el astro, - en contra
    double lat = 0, lon = 0;         // punto determinante (por donde pasa la recta, perpendicular al azimut)
};
Sight reduceSight(double estLat, double estLon, double ghaDeg, double decDeg, double observedAltDeg);

// Traslada una recta por lo navegado entre dos posiciones (la del barco al tomarla y la de ahora): mismo azimut, punto
// determinante movido ese rumbo y distancia (loxodromica).
Sight advanceSight(const Sight& s, double fromLat, double fromLon, double toLat, double toLon);

// Situacion por dos o mas rectas (minimos cuadrados alrededor de la estima). false si son casi paralelas.
bool fixFromSights(double estLat, double estLon, const Sight* sights, int n, double& lat, double& lon);

// Latitud con una sola altura observada: sin Ho = sin(lat) sin(dec) + cos(lat) cos(dec) cos(LHA), resuelta en la latitud
// (de las dos soluciones, la mas cercana a la de estima). Con LHA 0 es la latitud por la meridiana; con la Polar, la
// latitud por la Polar; fuera del meridiano depende de la longitud de estima (LHA = AHG + longitud). false sin solucion.
bool latitudeFromAltitude(double hoDeg, double decDeg, double lhaDeg, double latEstDeg, double& latDeg);
// Paso por el meridiano superior de la longitud lonDeg: el instante (dia juliano) mas cercano a jd0 en que el horario
// local es 0. gha(jd, ctx) da el AHG del astro en grados. Tambien sirve para la Luna y los planetas (se itera).
double meridianPassageJd(double jd0, double lonDeg, double (*gha)(double jd, void* ctx), void* ctx);

// Astros que se pueden observar ahora: Sol, Luna, los cuatro planetas y las estrellas entre minAlt y maxAlt (altura
// verdadera, grados), ordenados por azimut. kind: 0 Sol, 1 Luna, 2 planeta (index = Planet), 3 estrella (index de navStar).
struct SkyItem {
    unsigned char kind;
    short index;
    float alt, az, mag;            // mag: la del catalogo en estrellas; aproximada en el resto
};
int visibleBodies(double jd, double lat, double lon, double minAlt, double maxAlt, SkyItem* out, int max);
// Tres estrellas para una buena situacion: las que mejor reparten el horizonte (mayor separacion minima de azimut,
// lo ideal 120 grados), y entre parecidas las mas brillantes. Solo estrellas de 15 a 75 grados. false si no hay tres.
bool bestStarTriple(const SkyItem* items, int n, int idx[3]);

}  // namespace plotter
