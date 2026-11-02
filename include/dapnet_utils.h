/*
 * Modo DAPNET: receptor de buscapersonas POCSAG (solo recepcion). Recibe los bloques de bytes del SX126x en FSK, los pasa por el
 * decodificador de pocsag.h, guarda los ultimos mensajes (propios, RIC 8 de identificacion de transmisores y, si se activa, todos)
 * y pone el reloj con la hora que difunde la red cuando no hay hora GPS.
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef DAPNET_UTILS_H_
#define DAPNET_UTILS_H_

#include <Arduino.h>
#include "pocsag.h"

namespace DAPNET_Utils {

    const uint8_t HISTORY = 10;

    struct Entry {
        uint32_t ric;
        uint8_t  func;
        bool     own;                       // el RIC esta en la lista propia
        bool     corrupt;
        uint32_t atMs;                      // millis() de la recepcion
        int      rssi;
        char     text[Pocsag::MAX_TEXT + 1];
    };

    void     reset();                                           // al arrancar el modo: borra el estado del decodificador y relee los RIC propios
    void     feed(const uint8_t* data, size_t len, int rssi);   // bloque de bytes del chip
    void     clear();                                           // borra el historial
    uint8_t  count();                                           // mensajes guardados (0 a HISTORY)
    const Entry* at(uint8_t indexFromNewest);                   // 0 = el mas reciente
    uint32_t received();                                        // contador creciente de mensajes guardados (para detectar uno nuevo)
    bool     lastWasOwn();                                      // el ultimo mensaje guardado es para un RIC propio
    int      lastRssi();
    uint32_t batches();
    uint32_t messages();                                        // todos los decodificados (guardados o no)
    uint32_t badWords();
    uint32_t lastSyncMs();                                      // millis() del ultimo sincronismo (0 = ninguno)
    bool     inverted();
    uint8_t  ownCount();                                        // RIC propios configurados
    // Hora recibida por la red (0 = ninguna). utc: el RIC la da en UTC. text: ultima hora tal como llego
    uint32_t lastTimeMs();
    bool     lastTimeWasUtc();
    const char* lastTimeText();
    bool     timeApplied();                                     // se uso para poner el reloj

}

#endif
