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
// FT8 y FT4 (WSJT-X), solo transmision. El empaquetado del mensaje y el codificador (CRC-14, LDPC 174,91, sincronismo y mapa Gray) son
// los de ft8_lib (Karlis Goba, YL3JG; MIT) en lib/ft8lib; aqui van los parametros de cada modo, el calendario de ranuras (GPS) y el
// suavizado gaussiano de la frecuencia (GFSK) convertido en palabras del PLL del SX126x. Sin hardware: se prueba en PC.
#include <stdint.h>
#include <stddef.h>

namespace FTX {
    enum Proto : uint8_t { PROTO_FT8 = 0, PROTO_FT4 = 1 };

    constexpr uint8_t MAX_SYMBOLS   = 105;          // FT4 (103 + 2 de rampa); FT8 usa 79
    constexpr uint8_t MAX_STEPS     = 64;           // escrituras del PLL por simbolo: FT8 64 (cada 2,5 ms), FT4 20 (cada 2,4 ms)
    constexpr double  AUDIO_TONE0   = 1500.0;       // frecuencia de audio del tono 0 respecto al dial (Hz), como en WSJT-X por defecto
    constexpr size_t  MAX_TEXT      = 40;           // mensaje de texto (hasta 35 caracteres + terminador)

    const char* name(Proto p);                      // "FT8" / "FT4"
    uint8_t     symbols(Proto p);                   // 79 / 105
    uint32_t    symbolUs(Proto p);                  // 160000 / 48000 microsegundos
    uint32_t    slotMs(Proto p);                    // 15000 / 7500 ms: periodo T/R
    uint32_t    startDelayMs(Proto p);              // 500 / 300 ms tras el inicio de la ranura (los de WSJT-X)
    uint8_t     stepsPerSymbol(Proto p);            // 64 / 20
    double      toneSpacingHz(Proto p);             // 6,25 / 20,8333 Hz (1 / duracion del simbolo)

    // Mensaje: el texto libre de la WebUI si no esta vacio; si lo esta, "CQ <indicativo> <locator4>"
    size_t buildText(const char* custom, const char* call, const char* grid, char* out, size_t cap);
    // Empaqueta (tipo estandar, hash no usado) y codifica: tones[0..symbols-1] con valores 0..7 (FT8) o 0..3 (FT4). false si no se puede empaquetar
    bool   encode(Proto p, const char* text, uint8_t tones[MAX_SYMBOLS]);

    // Milisegundos hasta el proximo inicio de transmision (ranura + retardo) cuyo numero de ranura del dia UTC cumple idx % everyCycles == offset % everyCycles.
    // msOfDay = milisegundos del dia UTC; everyCycles 1..240
    uint32_t msUntilSlot(Proto p, uint32_t msOfDay, uint16_t everyCycles, uint16_t offset);
    // Desplazamiento repartido por el indicativo, para que no coincidan todas las balizas
    uint16_t slotOffset(const char* call, uint16_t everyCycles);

    constexpr double PLL_STEP_HZ = 32000000.0 / 33554432.0;     // paso del PLL del SX126x: 0,95367 Hz
    inline uint32_t pllWord(double hz) { return (uint32_t)(hz / PLL_STEP_HZ + 0.5); }

    // Trayectoria de frecuencia GFSK: la de ft8_lib/WSJT-X (pulso gaussiano de 3 simbolos, BT 2,0 en FT8 y 1,0 en FT4), muestreada
    // MAX_STEPS (FT8) o 20 (FT4) veces por simbolo y mantenida entre escrituras del PLL.
    class Shaper {
    public:
        // tones: secuencia de begin() hasta el final de la emision (debe seguir viva); tone0Hz: frecuencia RF del tono 0
        void   begin(Proto p, const uint8_t* tones, double tone0Hz);
        double hz(uint8_t sym, uint8_t step) const;             // frecuencia ideal en el paso "step" (0..stepsPerSymbol-1) del simbolo "sym"
        uint32_t word(uint8_t sym, uint8_t step) const { return pllWord(hz(sym, step)); }
        uint8_t  steps() const { return n_; }
    private:
        const uint8_t* tones_ = nullptr;
        uint8_t  nsym_ = 0;
        uint8_t  n_ = 0;
        double   tone0_ = 0.0;
        double   spacing_ = 0.0;
        float    pulse_[3 * MAX_STEPS];
    };
}
