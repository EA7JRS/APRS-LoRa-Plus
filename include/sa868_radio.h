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
 *  - Modos: APRS AFSK 1200 (modo 0) y APRS FSK 9600 G3RUH (modo 1: filtros de voz puenteados, canal de 25 kHz, ADC a 48 kHz).
 *  - Audio: emision por PDM (I2S0, modo DAC) en GPIO18 hacia la entrada de microfono del modulo (MIC_CH_SEL en alto); recepcion por
 *    el ADC1 (GPIO1) en modo continuo a 13200 Hz (48000 Hz en 9600) en una tarea propia.
 */

#ifndef SA868_RADIO_H_
#define SA868_RADIO_H_

#include <Arduino.h>

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
    void     sleep();
    void     wake();
    bool     validFreq(long hz);            // dentro de la banda del modulo (134..174 MHz VHF, 400..480 MHz UHF)
}

#endif
