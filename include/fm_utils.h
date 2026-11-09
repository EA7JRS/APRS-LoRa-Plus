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
    void     encoderBegin();                            // interrupciones del encoder (en todos los modos de la T-TWR)
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

    bool     aprsTransmit(const char* tnc2);           // baliza / mensaje APRS en la frecuencia APRS (espera si se esta hablando); con un satelite APRS a la vista, por el satelite

    // ---- Satelite: el VFO sigue al satelite elegido (modo SAT o pagina FM) ----
    // Bajada con Doppler (+ ajuste fino), subida precompensada, CTCSS del transmisor (archivo de datos) y canal de 25 kHz. Si la bajada no cae en
    // la banda del modulo (satelite de banda cruzada) se escucha la subida; si la subida no cae en una banda de aficionado del modulo, no se emite.
    // El PTT y el APRS por el satelite exigen sat.txEnabled (WebUI), un indicativo y el satelite por encima de sat.txMinEl.
    bool     satStart(int txIndex);                     // false + notice()
    void     satStop();
    bool     satOn();
    int      satTxIndex();
    bool     satNextTx(int dir);                        // transmisor siguiente que se pueda usar en esta banda
    bool     satUsable(int txIndex);                    // la bajada o la subida caen en la banda del modulo
    void     satFine(int deltaHz);                      // ajuste fino de la bajada (error del oscilador del satelite)
    int      satFineHz();
    long     satRxHz();                                 // frecuencia de escucha en uso
    long     satUpHz();                                 // subida con Doppler (0 = no se puede emitir en esta banda)
    int      satDopplerHz();                            // de la bajada
    bool     satDopplerOk();                            // false = sin hora o posicion: nominal
    bool     satListenUplink();                         // la bajada esta en otra banda: se escucha la subida
    bool     satAprs();                                 // el transmisor es un digipetidor APRS (ISS 145,825 MHz...)
    uint16_t satTone();                                 // CTCSS de la subida en uso (decimas de Hz; 0 = sin tono)
    bool     satCanTx(char* why, size_t n);             // se puede emitir ahora (y si no, por que)
    bool     satSendAprs(const char* info);             // trama APRS con este campo de informacion por el satelite (ruta sat.aprsPath)
    bool     satSendPosition(const char* comment);      // posicion (GPS / ultima / QTH del modo SAT) por el satelite
    uint32_t satSent();
    uint32_t satHeard();                                // tramas APRS oidas en el canal del satelite
    void     satOnFrame();                              // una trama oida mientras se sigue al satelite
    void     satExternalHz(long hz);                    // bajada ya corregida por un programa externo (GPredict por CAT): manda 15 s
    bool     uplinkInBand(long hz);                     // banda de aficionados del modulo (2 m o 70 cm de la region)
    size_t   qsySpec(char* out, size_t outMax);        // "145.650MHz T088 -060" del canal, para la baliza (0 si no toca)
}

#endif
