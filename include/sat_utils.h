/*
 * Modo SAT (fase 1): lista de satelites con enlaces en 70 cm, pasos, azimut / elevacion y Doppler en vivo.
 * Los TLE y las frecuencias salen de un archivo (sats.txt) que se descarga por WiFi desde la rama "satdata" del repositorio
 * (lo genera el CI cada dia: tools/make_sat_data.py) y se guarda en SPIFFS. El calculo esta en sat_core.cpp (se prueba en el PC).
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef SAT_UTILS_H_
#define SAT_UTILS_H_

#include <Arduino.h>
#include "sat_core.h"

namespace SAT_Utils {

    struct Entry {
        uint32_t offset;                    // posicion del bloque en el archivo
        uint16_t length;
        int32_t  noradId;
        char     name[22];
        uint8_t  txCount;
    };

    enum UpdateState : uint8_t { UPD_IDLE, UPD_WORKING, UPD_OK, UPD_ERROR };

    void           begin();                             // al entrar en el modo: carga el indice y el satelite elegido
    void           loop();                              // desde PAGES_Utils::update(): calculo en vivo y busqueda de pasos repartida
    bool           hasData();
    int            count();
    const Entry*   at(int index);
    int            selected();                          // indice del satelite elegido (-1 = ninguno)
    bool           select(int index);                   // lo carga y lo guarda en la configuracion
    const SatCore::Block* block();                      // TLE y transmisores del satelite elegido

    bool           haveTime();
    bool           haveQth();
    bool           live(SatCore::Look& out);            // posicion actual respecto a la estacion (false = sin hora, sin posicion o sin satelite)
    // Proximo paso (o el actual): false si aun no se ha calculado; searching = la busqueda sigue en marcha
    bool           nextPass(SatCore::Pass& out, bool& searching);
    double         tleAgeDays();
    const char*    dataStamp();                         // fecha del archivo ("2026-11-06T03:17Z") o ""

    // Doppler de un transmisor con la velocidad radial actual: lo que oyes (rxHz) y lo que debes emitir (txHz); 0 = el transmisor no tiene ese enlace
    void           doppler(const SatCore::Transmitter& t, double& rxHz, double& txHz);

    // Descarga del archivo de datos por WiFi (la red de Administracion > Firmware update)
    bool           updateStart();                       // false si ya hay una en curso
    UpdateState    updateState();
    const char*    updateMessage();

}

#endif
