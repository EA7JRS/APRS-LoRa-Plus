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
// Baliza LR-FHSS de propagacion (modo LR-FHSS propio, independiente del WSPR): utilidades puras (sin hardware, se prueban en PC).
// LR-FHSS solo se emite (SX1261/SX1262/SX1268); la recepcion queda para un firmware dedicado a placas con modulos capaces de decodificarlo.
#include <stdint.h>
#include <stddef.h>

namespace LRFHSS {
    constexpr size_t  MAX_PAYLOAD = 58;     // carga maxima de LR-FHSS con CR 1/3 (el mensaje de la baliza usa 20-26 bytes)
    constexpr uint8_t BW_COUNT    = 10;
    constexpr uint8_t CR_COUNT    = 4;
    constexpr uint32_t BEACON_PHASE_MS = 114000UL;   // la baliza sale en el segundo 114 del minuto par: tras acabar el WSPR (111,6 s) y antes del siguiente

    float       bwKHz(uint8_t idx);         // anchura de la rejilla: 39.06 ... 1574.2 kHz
    const char* crName(uint8_t idx);        // "5/6", "2/3", "1/2", "1/3"
    // Duracion aproximada de una emision (ms) para una carga de len bytes
    uint32_t    airtimeMs(size_t len, uint8_t crIdx);

    // Comprobacion de la frecuencia para una baliza de radioaficionado en 70 cm. Devuelve:
    //   0 = permitida; 1 = permitida con aviso (ancho de banda > 125 kHz: nota (p) del plan IARU R1 para datos tipo LoRa);
    //  -1 = fuera de 430-440 MHz; -2 = invade 432,000-432,500 MHz (senales debiles y balizas exclusivas); -3 = invade 435-438 MHz (servicio de satelites)
    // El ancho ocupado se toma como bwKHz(bwIdx) mas 2 kHz de margen por lado.
    int         freqCheck(uint32_t freqHz, uint8_t bwIdx);
    const char* freqCheckText(int code);   // texto corto para pantalla / log

    // Baliza de propagacion: texto ASCII "PB1 <indicativo> <locator> <dBm>", p. ej. "PB1 EA7JRS IM67NG 14" (indicativo, locator de 6 caracteres y potencia)
    bool        callValid(const char* call);        // 3 a 10 caracteres A-Z, 0-9 y '/', con al menos un digito
    bool        gridValid(const char* grid);        // locator de 6 caracteres: AA00AA .. RR99XX (mayusculas)
    // Escribe el mensaje en out (terminado en '\0'). Devuelve su longitud o 0 si los datos no son validos / no caben
    size_t      beaconEncode(const char* call, const char* grid, int dbm, char* out, size_t cap);
    bool        beaconParse(const char* msg, char* call, size_t callCap, char* grid, size_t gridCap, int* dbm);

    // Milisegundos hasta la proxima baliza (segundo 114 de un minuto par) cuya ranura cumple (minuto/2) % (intervalMin/2) == offset. 0 = ahora.
    // msOfDay = milisegundos del dia UTC. intervalMin par, 2..120. Si ya pasaron mas de 800 ms de la ranura actual, salta a la siguiente.
    uint32_t    msUntilBeacon(uint32_t msOfDay, uint16_t intervalMin, uint8_t offset);
    // Desplazamiento de ranura repartido por el indicativo (distinto del de WSPR)
    uint8_t     slotOffset(const char* call, uint16_t intervalMin);
}
