/*
 * Modo SAT y control de rotor: solo en las placas que lo admiten.
 * Requiere las pantallas APRS y no se compila en el ESP32 original (el calculo orbital en doble precision y la libm que arrastra
 * desbordan su IRAM); se puede quitar de una placa concreta con #define NO_SAT en su board_pinout.h.
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#ifndef SAT_CONFIG_H_
#define SAT_CONFIG_H_

#ifndef ARDUINO
    #define HAS_SAT                 // pruebas en el PC (tools/test_sat_core.cpp)
#else
    #include <Arduino.h>
    #include "board_pinout.h"
    #include "display.h"
    #if defined(HAS_APRS_PAGES) && !defined(NO_SAT) && !defined(CONFIG_IDF_TARGET_ESP32)
        #define HAS_SAT
    #endif
#endif

#endif
