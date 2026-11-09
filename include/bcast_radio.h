/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Radio comercial (FM 87,5-108 MHz; con el Si4735 tambien onda media, corta y larga) con un receptor externo en el I2C de la
 * placa (el de la pantalla): RDA5807M (0x11, sin pin de reinicio), Si4703 (0x10, necesita el pin de reinicio y SDIO bajo al
 * reiniciar para entrar en modo I2C) o Si4735 (0x11 / 0x63, con pin de reinicio). Se detecta solo al arrancar si esta activada
 * en la WebUI. El audio sale por la clavija del modulo: el tracker solo lo controla. Sintonia, busqueda, volumen, silencio,
 * mono, 10 presintonias, RDS (nombre de la emisora, RadioText y hora: include/rds.h), RSSI / SNR y estereo. Pagina BCAST,
 * ordenes BLE "bcast*" y WebUI.
 */

#ifndef BCAST_RADIO_H_
#define BCAST_RADIO_H_

#include <Arduino.h>

namespace BCAST {

    enum Chip : uint8_t { CHIP_NONE = 0, CHIP_RDA5807 = 1, CHIP_SI4703 = 2, CHIP_SI4735 = 3 };
    enum Band : uint8_t { BAND_FM = 0, BAND_MW = 1, BAND_SW = 2, BAND_LW = 3, BAND_COUNT = 4 };

    bool        begin();                    // deteccion (si Config.bcast.enabled); antes que nada use el I2C tras la pantalla
    void        loop();                     // RDS y estado, cada 100 ms
    Chip        chip();
    const char* chipName();
    const char* bandName(uint8_t band);
    bool        bandAvailable(uint8_t band);
    bool        isOn();
    void        power(bool on);
    bool        tune(uint32_t khz);         // FM: 87500..108000 (o desde 76000); AM: kHz
    void        step(int clicks);           // pasos del canal (FM 100 kHz, MW 9 / 10, SW 5, LW 9)
    void        seek(int dir);
    bool        setBand(uint8_t band);
    uint8_t     band();
    void        setVolume(int v);           // 0..15 (el Si4735 va de 0 a 63: se escala)
    int         volume();
    void        setMute(bool m);
    bool        muted();
    void        setMono(bool m);
    uint32_t    freqKHz();
    int         rssi();                     // dBuV
    int         snr();                      // dB (0 si el chip no lo da)
    bool        stereo();
    bool        seeking();
    const char* stationName();              // RDS (8 caracteres, vacio si no hay)
    void        radioText(char* out, size_t n);
    bool        rdsClock(int& y, int& mo, int& d, int& h, int& mi, int& offsetHalfHours);
    uint16_t    rdsPi();
    // presintonias (10): kHz, 0 = libre
    bool        presetStore(int i);
    bool        presetRecall(int i);
    bool        presetErase(int i);
    int         presetNext(int dir);        // la siguiente ocupada (-1 si no hay)
    int         presetIndexNow();           // la que coincide con lo sintonizado (-1)
}

#endif
