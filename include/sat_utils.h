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
        char     cat;                       // 'U' 70 cm, 'L' LoRa / LR-FHSS / TinyGS, 'G' geoestacionario
    };

    enum UpdateState : uint8_t { UPD_IDLE, UPD_WORKING, UPD_OK, UPD_ERROR };

    void           begin();                             // al entrar en el modo: carga el indice y el satelite elegido
    void           loop();                              // desde PAGES_Utils::update(): calculo en vivo y busqueda de pasos repartida
    bool           hasData();
    int            count();
    const Entry*   at(int index);
    int            selected();                          // indice del satelite elegido (-1 = ninguno)
    bool           select(int index);                   // lo carga y lo guarda en la configuracion
    bool           inCategory(const Entry& e);          // segun la categoria elegida (Config.sat.category: 0 todas, 1 70 cm, 2 LoRa / LR-FHSS / TinyGS, 3 geoestacionarios)
    int            step(int direction);                 // siguiente (+1) o anterior (-1) satelite de la categoria; -1 si no hay ninguno
    const char*    categoryName();
    int            nextCategory();                      // pasa a la categoria siguiente, elige el primer satelite de ella y la guarda
    bool           geo();                               // el satelite elegido es geoestacionario (sin pasos)
    const SatCore::Block* block();                      // TLE y transmisores del satelite elegido

    bool           haveTime();
    bool           haveQth();
    bool           observer(SatCore::Observer& out);    // posicion actual de la estacion (la misma que usa el modo SAT), para el rotor fuera del modo SAT
    const char*    qthSource();                         // "GPS" tracker, "FUS" fusion tracker + movil, "PHN" movil, "LAST" ultima conocida, "FIX" fija de la WebUI
    void           setPhonePosition(double latDeg, double lonDeg, double altM, float accuracyM);   // posicion del movil (app, orden BLE "pos")
    bool           live(SatCore::Look& out);            // posicion actual respecto a la estacion (false = sin hora, sin posicion o sin satelite)
    // Proximo paso (o el actual): false si aun no se ha calculado; searching = la busqueda sigue en marcha
    bool           nextPass(SatCore::Pass& out, bool& searching);
    double         tleAgeDays();
    const char*    dataStamp();                         // fecha del archivo ("2026-11-06T03:17Z") o ""

    // Doppler de un transmisor con la velocidad radial actual: lo que oyes (rxHz) y lo que debes emitir (txHz); 0 = el transmisor no tiene ese enlace
    void           doppler(const SatCore::Transmitter& t, double& rxHz, double& txHz);

    // Descarga del archivo de datos por WiFi (la red de Administracion > Firmware update)
    bool           updateStart();                       // false si ya hay una en curso
    // Subida del archivo de datos desde el movil por BLE (sin WiFi): begin -> chunk... -> end (valida, sustituye al actual y recarga la lista)
    bool           dataBegin(size_t expectedBytes);
    bool           dataChunk(const char* text, size_t n);
    bool           dataEnd();
    void           dataAbort();
    UpdateState    updateState();
    const char*    updateMessage();

}

#endif
