/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
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

#ifndef NOTIFICATION_UTILS_H_
#define NOTIFICATION_UTILS_H_

#include <Arduino.h>
#include "bell_rings.h"


namespace NOTIFICATION_Utils {

    void playTone(int frequency, uint8_t duration);
    void sidetone(bool on);         // tono continuo de manipulador CW en el zumbador (700 Hz)
    void beaconTxBeep();
    void messageBeep();
    void stationHeardBeep();
    void countdownBeep();           // pitido corto de la cuenta atras de SOS
    void alarmBeep();               // sirena de emergencia (Mic-E Emergency, SOS, MOB)
    void shutDownBeep();
    void lowBatteryBeep();
    void start();
    void bell(Bell::Id id);         // melodia de campana APRS (sin bloqueos): se reproduce desde update()
    void update();                  // llamar en cada vuelta del loop

    // Luces (LED verde / blanco / rojo) ligadas al zumbador y al modo CW; cada una solo actua si su LED esta activo en la WebUI
    void lightTx(bool on);          // modo CW: LED verde = portadora emitida (WebUI: LEDs in CW mode)
    void lightMorse(bool on);       // modo CW: flash blanco como lampara Morse (sigue el manipulador / entrenador)
    void lightRx(bool on);          // modo CW: LED rojo = senal CW recibida
    bool rxLedBusy();               // el LED rojo lo lleva ahora la recepcion CW (ledNotification no lo toca)
    bool lampBusy();                // el flash lo lleva ahora el zumbador o la lampara Morse (checkFlashlight no lo toca)

}

#endif