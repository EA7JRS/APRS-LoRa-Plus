/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ tracker.
 * Licensed under the GNU General Public License, version 3 or later.
 *
 * Modo de entrada del boton de usuario. En el ESP32 clasico los GPIO 34 a 39 son solo de entrada y no tienen
 * resistencia interna: pedirla (INPUT_PULLUP) no hace nada y deja en el registro
 * «gpio_pullup_en: GPIO number error (input-only pad has no internal PU)» cada vez. Las placas que ponen ahi el
 * boton (T-Beam v0.7 / v1.x en el 38 o el 39, Hydra) llevan la resistencia en la placa.
 */
#ifndef BUTTON_PIN_H_
#define BUTTON_PIN_H_

#include <Arduino.h>
#include "board_pinout.h"

#if defined(BUTTON_PIN) && defined(CONFIG_IDF_TARGET_ESP32) && (BUTTON_PIN >= 34) && (BUTTON_PIN <= 39)
    #define BUTTON_INPUT_MODE   INPUT
    #define BUTTON_HAS_PULLUP   false
#else
    #define BUTTON_INPUT_MODE   INPUT_PULLUP
    #define BUTTON_HAS_PULLUP   true
#endif

#endif
