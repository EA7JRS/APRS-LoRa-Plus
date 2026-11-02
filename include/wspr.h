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
// WSPR (Weak Signal Propagation Reporter): mensaje tipo 1 (indicativo, locator de 4 caracteres, potencia), codificacion a 162 simbolos 4-FSK
// y plan de tonos para el PLL del SX126x. Codigo puro sin hardware (se prueba en PC).
#include <stdint.h>
#include <stddef.h>

namespace WSPR {
    constexpr uint8_t  SYMBOLS        = 162;
    constexpr double   TONE_SPACING   = 12000.0 / 8192.0;       // 1,46484375 Hz entre tonos y 1,4648 baudios
    constexpr double   PLL_STEP_HZ    = 32000000.0 / 33554432.0;  // paso del PLL del SX126x: 0,95367 Hz (xtal 32 MHz / 2^25)
    constexpr uint32_t START_DELAY_MS = 1000;                    // la transmision empieza 1 s despues del minuto par

    // Potencias validas del mensaje (dBm): terminan en 0, 3 o 7
    bool     validPower(int dbm);
    int      nearestValidPower(int dbm);
    // Tabla de calibracion por potencia: 7 pasos (0, 3, 7, 10, 13, 17, 20 dBm). Devuelve el indice 0..6 de la potencia valida mas cercana
    constexpr uint8_t POWER_STEPS = 7;
    uint8_t  powerIndex(int dbm);

    // Prepara y codifica. call: hasta 6 caracteres (3.er caracter, tras ajustar, un digito; sin prefijos ni sufijos). grid: 4 caracteres (AA00..RR99).
    // Devuelve false si el mensaje no es un tipo 1 valido. symbols: 162 valores 0..3
    bool     encode(const char* call, const char* grid, int dbm, uint8_t symbols[SYMBOLS]);
    bool     callValid(const char* call);
    bool     gridValid(const char* grid);

    // Inicio del simbolo i, en microsegundos desde el comienzo de la transmision (duracion total 110,59 s)
    uint32_t symbolStartUs(uint8_t i);

    // Milisegundos hasta el proximo inicio de transmision (minuto par + 1 s) cuyo numero de ranura cumple (minuto/2) % (intervalMin/2) == offset.
    // msOfDay = milisegundos transcurridos del dia UTC. intervalMin par, 2..120.
    uint32_t msUntilSlot(uint32_t msOfDay, uint16_t intervalMin, uint8_t offset);
    // Desplazamiento de ranura repartido por el indicativo, para que no coincidan todas las balizas
    uint8_t  slotOffset(const char* call, uint16_t intervalMin);

    // Generador de tonos: palabra PLL (frf) para cada simbolo. El paso del PLL (0,954 Hz) no da la separacion de 1,4648 Hz; con "fine" cada
    // simbolo se emite en DOS tramos (paso inferior y superior vecinos) repartidos para que la media sea el tono exacto. Solo hay UNA escritura
    // extra del PLL por simbolo (a ~1,46 Hz), en lugar de un modulador a 1 kHz, cuyas bandas laterales (peine de lineas) sucian la senal.
    struct TonePlan {
        uint32_t first;                             // palabra PLL al inicio del simbolo
        uint32_t second;                            // palabra PLL tras switchUs (igual a first si no hay segundo tramo)
        uint32_t switchUs;                          // microsegundos desde el inicio del simbolo hasta cambiar a "second"
    };
    class Tones {
    public:
        // centerHz: frecuencia central (dial + 1500 Hz); corrHz: correccion de frecuencia medida del oscilador (Hz, se suma)
        // driftHzPerMin: pendiente de la compensacion de deriva (Hz/min, puede ser negativa); rampa centrada en la mitad de la emision, asi el
        // centro calibrado no cambia. Positiva = sube la frecuencia a lo largo de la emision (compensa una caida, p. ej. drift negativo en WSJT-X)
        void     begin(double centerHz, double corrHz, bool fine, double driftHzPerMin = 0.0);
        // tone: tono 0..3 del simbolo; index: posicion del simbolo en la emision 0..161; symbolUs: duracion del simbolo
        TonePlan plan(uint8_t tone, uint8_t index, uint32_t symbolUs) const;   // palabras y momento del cambio
        uint32_t exact(uint8_t tone, uint8_t index) const;                     // palabra redondeada sin segundo tramo (para el arranque)
        double   hz(uint8_t tone, uint8_t index) const;                        // frecuencia ideal del simbolo (con la compensacion de deriva)
    private:
        double   base_ = 0;
        bool     fine_ = false;
        double   driftHzPerMin_ = 0;
    };
    inline uint32_t pllWord(double hz) { return (uint32_t)(hz / PLL_STEP_HZ + 0.5); }
}
