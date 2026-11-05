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

#ifndef BOARD_PINS_H_
#define BOARD_PINS_H_

#include <Arduino.h>

// Which GPIO of the board in use are taken (radio, GPS, display, buttons, battery...) or unusable (flash, USB),
// derived from the macros of board_pinout.h so that every variant is covered without a hand-written table.
// The WebUI reads it from /api/pins to identify the pins and to suggest free ones.
namespace BOARD_PINS {

    bool    reserved(int pin);      // taken by the board or by the chip (flash, USB): the firmware refuses it for LEDs, buzzer, PTT, key, NMEA
    bool    inputOnly(int pin);     // GPIO that cannot drive an output (ESP32 classic 34..39)
    String  json();                 // {"chip":..,"lo":..,"hi":..,"skip":[..],"adc1":[a,b],"pins":{"<gpio>":["u|a|i","text"],...}}

}

#endif
