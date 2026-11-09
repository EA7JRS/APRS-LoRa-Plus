/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modo FM (voz) de la LilyGO T-TWR: la T-TWR como emisora de mano de 2 m / 70 cm. PTT lateral con el microfono de la placa, VFO
 * con el encoder, 32 memorias, desplazamiento de repetidor (y ARS), CTCSS / DCS / 1750 Hz, potencia, ancho, silenciador manual o
 * automatico, reduccion de ruido (Rev2.1), escaneo, ARTS y EAI (como Yaesu), hombre caido, tiempo maximo de emision, balizas APRS
 * en la frecuencia APRS durante la voz, QSY desde balizas APRS ("146.940MHz T100 -060") y la frecuencia del canal en la baliza
 * propia. La parte sin hardware esta en include/fm_core.h y include/fm_dsp.h.
 */

#ifndef FM_UTILS_H_
#define FM_UTILS_H_

#include <Arduino.h>
#include "fm_core.h"

namespace FM_Utils {

    enum Edit : uint8_t { ED_FREQ = 0, ED_TONE, ED_OFFSET, ED_SQUELCH, ED_COUNT };

    void     begin();
    void     loop();                                    // desde el bucle principal en el modo FM
    void     dial(int clicks);                          // encoder en la pagina FM (lo que se este editando)
    int      takeEncoder();                             // pulsos del encoder desde la ultima lectura
    void     activity();                                // boton / encoder / PTT: reinicia el "hombre caido"

    // estado
    const FmCore::Channel& channel();
    bool     memMode();
    int      memIndex();
    bool     memUsed(int i);
    String   memName(int i);
    long     stepHz();
    uint8_t  edit();
    void     setEdit(uint8_t e);
    bool     ptt();                                     // emitiendo voz
    bool     transmitting();                            // cualquier emision (voz, APRS, tonos)
    bool     busy();
    int      rssi();
    uint32_t txSeconds();
    int      squelch();                                 // nivel en uso
    const char* notice();                               // ultimo aviso ("" si no hay)

    // canal
    bool     setChannel(const FmCore::Channel& c);      // al VFO
    bool     setVfoHz(long hz);
    void     toggleVfoMem();
    bool     recall(int i);
    bool     store(int i, const char* name);            // el canal en uso a la memoria i
    bool     erase(int i);
    void     memStep(int dir);                          // memoria siguiente / anterior ocupada
    void     cycleStep();
    void     cycleShift();                              // simplex, -, +
    void     cycleToneMode();
    void     toneStep(int dir);
    void     offsetStep(int dir);
    void     togglePower();
    void     toggleWide();
    void     squelchStep(int dir);
    void     toggleAutoSquelch();
    void     cycleNoiseReduction();
    bool     qsy(const FmCore::Qsy& q);                 // sintoniza lo que anuncia una baliza APRS

    // funciones
    void     scanStart();
    void     scanStop();
    bool     scanning();
    bool     scanPaused();
    void     artsToggle();
    bool     artsOn();
    uint8_t  artsState();                               // FmCore::ARTS_*
    void     eaiStart();
    void     eaiStop();
    bool     eaiOn();
    uint32_t eaiRemainingS();
    void     toggleAprsBeacon();

    bool     aprsTransmit(const char* tnc2);           // baliza / mensaje APRS en la frecuencia APRS (espera si se esta hablando)
    size_t   qsySpec(char* out, size_t outMax);        // "145.650MHz T088 -060" del canal, para la baliza (0 si no toca)
}

#endif
