/*
 * Nucleo de calculo de satelites (modo SAT): TLE -> posicion (SGP4), azimut / elevacion, Doppler y pasos.
 * C++ puro, sin dependencias de Arduino: se prueba en el PC (tools/test_sat_core.cpp).
 *
 * Copyright (c) 2026 Manuel - EA7JRS. GPL-3.0-or-later. El propagador SGP4 es de D. Vallado (licencia MIT, third_party/SGP4_LICENSE.txt).
 */
#ifndef SAT_CORE_H_
#define SAT_CORE_H_

#include <stdint.h>
#include "SGP4.h"

namespace SatCore {

    struct Observer {
        double latDeg;
        double lonDeg;                      // este positivo
        double altM;
    };

    struct Sat {
        char     name[25];
        int      noradId;
        double   epochUnix;                 // epoca del TLE (segundos Unix, UTC)
        elsetrec rec;                       // ~1,2 kB: solo se inicializa el satelite activo
        bool     valid;
    };

    struct Look {
        double azDeg;                       // 0 = norte, 90 = este
        double elDeg;
        double rangeKm;
        double rangeRateKmS;                // > 0 = se aleja
        double subLatDeg, subLonDeg, altKm; // punto subsatelite y altura
    };

    struct Pass {
        double aosUnix, losUnix, tcaUnix;
        double maxElDeg;
        double aosAzDeg, losAzDeg;
    };

    // Valida el checksum de las dos lineas y las convierte (opmode 'i' = SGP4 mejorado). false si no son un TLE valido.
    bool   parseTle(const char* name, const char* line1, const char* line2, Sat& out);
    double tleAgeDays(const Sat& s, double nowUnix);

    // Posicion y velocidad respecto al observador en el instante dado (segundos Unix UTC). false si el modelo falla.
    bool   look(Sat& s, const Observer& obs, double unixTime, Look& out);

    // Frecuencia recibida / a emitir con efecto Doppler (rangeRate en km/s, > 0 = se aleja). La bajada baja de frecuencia al alejarse.
    double dopplerDown(double freqHz, double rangeRateKmS);        // lo que oyes si el satelite emite en freqHz
    double dopplerUpPre(double freqHz, double rangeRateKmS);       // lo que hay que emitir para que el satelite reciba freqHz

    // Busqueda de pasos reanudable (para repartirla en el bucle principal): cada advance() procesa como mucho `steps` pasos de tiempo.
    struct PassSearch {
        double  from, to;                   // intervalo de busqueda
        double  minElDeg;
        double  t;                          // instante actual
        int     phase;                      // 0 = buscando AOS, 1 = dentro del paso, 2 = hecho con paso, 3 = hecho sin paso, 4 = error
        Pass    pass;
        double  prevEl;
        double  stepSec;
    };
    void   passSearchStart(PassSearch& ps, double fromUnix, double horizonSec, double minElDeg);
    int    passSearchAdvance(PassSearch& ps, Sat& s, const Observer& obs, int steps);      // devuelve ps.phase

    // ---- Archivo de datos (lo genera tools/make_sat_data.py y lo publica el CI en la rama satdata) ----
    // Formato de texto, un bloque por satelite:
    //   S|<norad>|<nombre>
    //   1 <linea 1 del TLE>
    //   2 <linea 2 del TLE>
    //   T|<subida_min Hz>|<subida_max Hz>|<bajada_min Hz>|<bajada_max Hz>|<modo>|<baudios>|<inversor 0/1>|<descripcion>
    // (0 = sin dato; las lineas que empiezan por # son comentarios)
    const int MAX_TX = 6;
    struct Transmitter {
        long upLo, upHi, dnLo, dnHi;        // Hz; un solo valor fijo si Lo == Hi; un transpondedor lineal si difieren
        char mode[10];
        int  baud;
        bool invert;
        char desc[26];
    };
    struct Block {
        int  noradId;
        char name[25];
        char line1[72], line2[72];
        Transmitter tx[MAX_TX];
        int  txCount;
    };
    // Lee un bloque que empieza en `text` (en una linea "S|...") y termina en la siguiente "S|" o en el fin; devuelve cuantos bytes consume (0 = no hay bloque)
    size_t parseBlock(const char* text, size_t len, Block& out);
    bool   parseTxLine(const char* line, Transmitter& t);
    bool   txIn70cm(const Transmitter& t);                                         // sube o baja dentro de 420-450 MHz
    long   txDownCenter(const Transmitter& t);
    long   txUpCenter(const Transmitter& t);

    // Utilidades
    double unixFromCivil(int year, int mon, int day, int hour, int minute, double sec);
    const char* cardinal(double azDeg);     // "N", "NE", ...

}

#endif
