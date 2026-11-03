/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
// FST4W (WSJT-X 3.3): mensaje de 50 bits (indicativo, locator de 4 caracteres y potencia, como WSPR tipo 1) + CRC de 24 bits +
// LDPC(240,74) + 160 simbolos 4-GFSK (cuatro palabras de sincronismo de 8 simbolos y 120 de datos, mapa Gray). Solo la parte de
// codificacion; el calendario y la modulacion estan en ft_beacon.h. El empaquetado del indicativo (c28) lo hace ft8_lib (lib/ft8lib);
// la matriz generadora (include/fst4w_table.h) y el resto del formato son los de lib/fst4/ de WSJT-X 3.3.0 (GPL-3.0).
#include <stdint.h>
#include <stddef.h>

namespace FST4W {
    constexpr uint8_t SYMBOLS   = 160;
    constexpr uint8_t DATA_SYMS = 120;
    constexpr uint8_t CODE_BITS = 240;

    // Potencias que el mensaje puede llevar: 0, 3, 7, 10, 13, 17, 20, 23, 27, 30 ... 60 dBm (nint(0,3 dBm) debe devolver el mismo valor)
    bool validDbm(int dbm);
    int  nearestValidDbm(int dbm);

    // CRC de 24 bits (polinomio 0x100065b) del vector de n bits (un bit por byte, el primero en primer lugar); los 24 ultimos deben ser cero
    uint32_t crc24(const uint8_t* bits, size_t n);

    // Palabra de codigo de 240 bits (un bit por byte): 74 de mensaje y CRC seguidos de 166 de paridad. false si el indicativo o el locator no sirven
    bool encodeBits(const char* call, const char* grid4, int dbm, uint8_t codeword[CODE_BITS]);
    // Tonos 0..3 de los 160 simbolos
    bool encode(const char* call, const char* grid4, int dbm, uint8_t tones[SYMBOLS]);
}
