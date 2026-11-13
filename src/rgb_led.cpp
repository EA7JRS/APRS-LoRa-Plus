/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <Arduino.h>
#include "board_pinout.h"
#include "configuration.h"
#include "rgb_led.h"
#ifdef HAS_APRS_PAGES
    #include "pages_utils.h"
#endif
#ifdef HAS_SA868
    #include "sa868_radio.h"
    #include "fm_utils.h"
#endif

extern Configuration Config;
extern bool          messageLed;

namespace RGB_LED {

#ifdef TTWR_PIXEL_PIN

    static uint32_t shown = 0xFFFFFFFF;          // color en el LED (0x00RRGGBB); 0xFFFFFFFF = sin escribir aun
    static bool     txOn = false;
    static uint32_t rxAt = 0, lastMs = 0;

    static void put(uint8_t r, uint8_t g, uint8_t b) {
        static const uint8_t SCALE[4] = {0, 12, 48, 160};                     // apagado, bajo, medio, alto (el WS2812 a 255 deslumbra)
        const uint8_t k = SCALE[Config.display.rgbLed > 3 ? 3 : Config.display.rgbLed];
        r = (uint8_t)(r * k / 255); g = (uint8_t)(g * k / 255); b = (uint8_t)(b * k / 255);
        const uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        if (c == shown) return;
        shown = c;
        rgbLedWrite(TTWR_PIXEL_PIN, r, g, b);
    }

    static void update() {
        const uint32_t now = millis();
        bool alarm = false, alarmOn = false;
        #ifdef HAS_APRS_PAGES
            alarm = PAGES_Utils::alarmLed(alarmOn);
        #endif
        bool fmTx = false, fmBusy = false;
        #ifdef HAS_SA868
            if (SA868::modeFm()) { fmTx = FM_Utils::ptt(); fmBusy = FM_Utils::busy(); }
        #endif
        if (txOn || fmTx)                       put(255, 0, 0);                               // emision
        else if (alarm)                         alarmOn ? put(255, 0, 0) : put(0, 0, 0);      // alarma de emergencia
        else if (now - rxAt < 150)              put(0, 255, 0);                               // trama recibida
        else if (fmBusy)                        put(0, 120, 0);                               // FM: silenciador abierto
        else if (messageLed && (now / 1000) % 5 == 0) put(0, 0, 255);                        // mensajes sin leer: 1 s cada 5 s
        else                                    put(0, 0, 0);
    }

    void setup() { put(0, 0, 0); }

    void loop() {
        const uint32_t now = millis();
        if (now - lastMs < 20) return;
        lastMs = now;
        update();
    }

    void tx(bool on) { txOn = on; update(); }
    void rx()        { rxAt = millis(); if (rxAt == 0) rxAt = 1; update(); }

#else
    void setup() {}
    void loop() {}
    void tx(bool) {}
    void rx() {}
#endif

}
