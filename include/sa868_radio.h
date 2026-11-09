/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Radio FM SA868 de la LilyGO T-TWR Plus (Rev2.0 y Rev2.1) con el modem AFSK 1200 del ESP32-S3 (APRS analogico).
 *
 *  - Firmware del modulo: el de fabrica de NiceRF (ordenes AT+DMO...) o el abierto de OpenRTX (sa8x8-fw: AT+POKE/PEEK a los
 *    registros del AT1846S). Se detecta solo.
 *  - Revision de la placa: la Rev2.1 alimenta el modulo desde la bateria y la Rev2.0 desde el DC3 de la PMU; si el modulo contesta
 *    sin DC3 es una Rev2.1. Se puede fijar en la configuracion (sa868.revision).
 *  - Modos: APRS AFSK 1200 (modo 0), APRS FSK 9600 G3RUH (modo 1), DAPNET (modo 5, recepcion POCSAG), M17 (modo 10) y FM (modo 11,
 *    voz: src/fm_utils.cpp). En 9600 y M17: audio plano (filtros de voz
 *    puenteados), canal de 25 kHz y ADC a 48 kHz. M17: el APRS va en paquetes M17 de protocolo APRS; se oyen ademas los SMS y los
 *    indicativos y posiciones GNSS de las emisiones de voz (include/m17_modem.h).
 *  - Audio: emision por PDM (I2S0, modo DAC) en GPIO18 hacia la entrada de microfono del modulo (MIC_CH_SEL en alto); recepcion por
 *    el ADC1 (GPIO1) en modo continuo a 13200 Hz (48000 Hz en 9600 y M17) en una tarea propia.
 */

#ifndef SA868_RADIO_H_
#define SA868_RADIO_H_

#include <Arduino.h>
#include "fm_core.h"

namespace SA868 {

    enum class Firmware : uint8_t { NONE = 0, NICERF = 1, OPENRTX = 2 };

    bool     begin();                       // alimentacion, deteccion, configuracion y recepcion en marcha
    bool     ready();
    Firmware firmware();
    const char* firmwareName();
    int      revision();                    // 20 o 21 (0 si no hay modulo)
    bool     uhf();                         // banda del modulo (direccion de la pantalla: 0x3C VHF, 0x3D UHF)
    bool     applyConfig();                 // frecuencias, canal, silenciador, volumen y filtros de Config.sa868
    bool     setFrequencies(long rxHz, long txHz);
    bool     transmit(const char* tnc2);    // bloquea mientras emite (TXDELAY + trama + cola)
    bool     popFrame(String& tnc2, int& rssi);
    bool     channelBusy();                 // silenciador abierto (Rev2.1) o datos AFSK en el aire
    int      rssi();                        // dBm (solo con el firmware de OpenRTX; 0 si no se sabe)
    float    audioLevel();                  // nivel de pico de la entrada de audio (0..1)
    uint32_t framesOk();
    bool     mode9600();                    // arrancado en APRS FSK 9600 (G3RUH, audio plano)
    bool     modeM17();                     // arrancado en M17
    const char* modemName();                // "AFSK 1200", "G3RUH 9600" o "M17"
    long     rxFreqHz();                    // frecuencias en uso (las del modo: sa868, perfil FSK en 9600, sa868.m17FreqHz en M17, CAT)
    long     txFreqHz();
    long     m17FreqHz();                   // la de M17 (la de la configuracion o la sugerida para la banda del modulo)
    bool     m17Sms(const char* dst, const char* text);    // SMS M17 (protocolo 0x05) a un indicativo o a "@ALL"; bloquea mientras emite
    uint32_t m17Lsfs();                     // M17: cabeceras (LSF) oidas
    uint32_t m17CrcErrors();

    // DAPNET (POCSAG de banda base por el audio plano; 144,8625 MHz en VHF, 439,9875 MHz en UHF)
    bool     modeDapnet();
    long     dapnetFreqHz();
    size_t   popDapnet(uint8_t* out, size_t max);   // bytes demodulados (bit de mayor peso primero) para DAPNET_Utils::feed

    // CW (modo MORSE): A1A (portadora con el PTT) o MCW (tono); escucha por el silenciamiento del ruido de FM
    long     cwFreqHz(bool beacon);
    bool     cwSelect(bool beacon);
    void     cwKey(bool on);
    void     cwEnd();
    bool     cwRxBegin();
    float    cwRxLevel();
    void     cwRxEnd();

    // FM (voz). Ver src/fm_utils.cpp
    bool     modeFm();
    bool     fmApply(const FmCore::Channel& c, int squelch);  // frecuencias, tonos, ancho, potencia y silenciador
    const FmCore::Channel& fmChannel();
    bool     fmToneSupported(const FmCore::Channel& c);     // DCS solo con NiceRF; CTCSS de NiceRF: los 38 de su tabla
    bool     fmLastToneOk();
    int      fmSquelchLevel();              // nivel en uso (el automatico lo cambia en la Rev2.0)
    void     fmPoll();                      // cada 50 ms desde el bucle principal
    bool     fmBusy();                      // silenciador abierto
    int      fmRssi();                      // dBm (solo con OpenRTX; 0 si no)
    bool     fmVoice(bool on);              // PTT de voz con el microfono de la placa
    bool     fmVoiceActive();
    bool     fmTransmitting();
    bool     fmPlayTones(const uint16_t* hz, const uint16_t* ms, size_t n);   // tonos del ESP32 (0 Hz = portadora)
    bool     fmCwId(const char* text, int wpm);
    bool     fmAprsTransmit(const char* tnc2);  // APRS AFSK en la frecuencia APRS y vuelta al canal
    bool     fmSpeakerFromEsp();            // Rev2.1: el altavoz lo alimenta el ESP32 (silenciador por software / reduccion de ruido)
    void     sleep();
    void     wake();
    bool     validFreq(long hz);
    long     ppmCorrectionHz(long hz);      // correccion global (Config.radioPpm1000) que se suma al programar el modulo
    bool     carrier(bool on);              // portadora sin modular en la frecuencia de emision en uso (calibracion)            // dentro de la banda del modulo (134..174 MHz VHF, 400..480 MHz UHF)
}

#endif
