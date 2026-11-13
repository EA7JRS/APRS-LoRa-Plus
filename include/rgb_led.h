/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LED RGB (WS2812) de la placa, en la LilyGO T-TWR Plus (GPIO42): rojo al emitir, verde con el silenciador abierto o al recibir
 * una trama, azul con mensajes sin leer, rojo parpadeando con una alarma. Brillo en Display > RGB LED (0 = apagado).
 */
#ifndef RGB_LED_H_
#define RGB_LED_H_

namespace RGB_LED {
    void setup();
    void loop();
    void tx(bool on);       // emision APRS (bloquea el bucle: se enciende antes y se apaga despues)
    void rx();              // trama recibida: destello verde
}

#endif
