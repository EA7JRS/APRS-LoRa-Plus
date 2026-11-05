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

#include "notification_utils.h"
#include "configuration.h"

uint8_t channel                 = 0;
uint8_t resolution              = 8;
uint8_t pauseDuration           = 20;

int     startUpSound[]          = {440, 880, 440, 1760};
uint8_t startUpSoundDuration[]  = {100, 100, 100, 200};

int     shutDownSound[]         = {1720, 880, 400};
uint8_t shutDownSoundDuration[] = {60, 60, 200};

extern Configuration    Config;
extern bool             digipeaterActive;
extern bool             flashlight;

namespace NOTIFICATION_Utils {

    // ---- Luces ligadas al zumbador y al modo CW ------------------------------------------------------------------------
    static bool lampLit     = false;        // el flash lo lleva una luz (sirena / lampara Morse)
    static bool rxLedOwned  = false;        // el LED rojo lo lleva la recepcion CW

    // Flash blanco: enciende / apaga; al apagarse recupera el estado de la linterna manual
    static void lampSet(bool on) {
        if (!Config.notification.ledFlashlight || Config.notification.ledFlashlightPin < 0) return;
        lampLit = on;
        digitalWrite(Config.notification.ledFlashlightPin, (on || flashlight) ? HIGH : LOW);
    }

    // Sirena de emergencia: el flash blanco sigue cada nota del zumbador
    static void sirenLight(bool on) {
        if (Config.notification.flashEmergency) lampSet(on);
    }

    bool lampBusy()  { return lampLit; }
    bool rxLedBusy() { return rxLedOwned; }

    void lightTx(bool on) {
        if (!Config.notification.ledCw || !Config.notification.ledTx || Config.notification.ledTxPin < 0) return;
        digitalWrite(Config.notification.ledTxPin, on ? HIGH : LOW);
    }

    void lightMorse(bool on) {
        if (!Config.notification.ledCw) return;
        lampSet(on);
    }

    void lightRx(bool on) {
        if (!Config.notification.ledCw || !(Config.notification.ledMessage || Config.notification.ledRxBeacon) || Config.notification.ledMessagePin < 0) return;
        rxLedOwned = on;
        digitalWrite(Config.notification.ledMessagePin, on ? HIGH : LOW);
    }

    void sidetone(bool on) {
        static bool active = false;
        if (on == active) return;
        active = on;
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            if (on) {
                ledcAttach(Config.notification.buzzerPinTone, 700, resolution);
                ledcWrite(Config.notification.buzzerPinTone, 128);
            } else {
                ledcWrite(Config.notification.buzzerPinTone, 0);
                ledcDetach(Config.notification.buzzerPinTone);
            }
        #else
            if (on) {
                ledcSetup(channel, 700, resolution);
                ledcAttachPin(Config.notification.buzzerPinTone, 0);
                ledcWrite(channel, 128);
            } else {
                ledcWrite(channel, 0);
            }
        #endif
    }

    // ---- Campanas APRS (bell_rings.h): reproductor sin bloqueos, una nota por vez en el pin de tono ------------------
    static Bell::Player bellPlayer;
    static uint16_t     bellHz     = 0;         // frecuencia que suena ahora (0 = nada)
    static bool         bellPowered = false;    // pin de alimentacion del zumbador activado

    static void bellToneOn(uint16_t hz) {
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcAttach(Config.notification.buzzerPinTone, hz, resolution);
            ledcWrite(Config.notification.buzzerPinTone, 128);
        #else
            ledcSetup(channel, hz, resolution);
            ledcAttachPin(Config.notification.buzzerPinTone, 0);
            ledcWrite(channel, 128);
        #endif
    }

    static void bellToneOff() {
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcWrite(Config.notification.buzzerPinTone, 0);
            ledcDetach(Config.notification.buzzerPinTone);
        #else
            ledcWrite(channel, 0);
        #endif
    }

    void update() {
        if (!bellPlayer.active() && bellHz == 0 && !bellPowered) return;
        uint16_t hz = 0;
        const bool playing = bellPlayer.tick(millis(), hz);
        if (hz != bellHz) {
            if (bellHz) bellToneOff();
            if (hz) bellToneOn(hz);
            bellHz = hz;
        }
        if (!playing && bellPowered) {
            digitalWrite(Config.notification.buzzerPinVcc, LOW);
            bellPowered = false;
        }
    }

    void bell(Bell::Id id) {
        if (!Config.notification.buzzerActive) return;
        if (!bellPlayer.start(id, millis())) return;        // ya suena una campana igual o mas importante
        if (!bellPowered) {
            digitalWrite(Config.notification.buzzerPinVcc, HIGH);
            bellPowered = true;
        }
        update();
    }

    // light = true: el flash blanco se enciende y se apaga con la nota (sirena de emergencia)
    static void playToneLit(int frequency, uint8_t duration, bool light) {
        #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttach(Config.notification.buzzerPinTone, frequency, resolution);   // Arduino core 3.x API
        ledcWrite(Config.notification.buzzerPinTone, 128);
        if (light) sirenLight(true);
        delay(duration);
        ledcWrite(Config.notification.buzzerPinTone, 0);
        ledcDetach(Config.notification.buzzerPinTone);
        if (light) sirenLight(false);
        #else
        ledcSetup(channel, frequency, resolution);
        ledcAttachPin(Config.notification.buzzerPinTone, 0);
        ledcWrite(channel, 128);
        if (light) sirenLight(true);
        delay(duration);
        ledcWrite(channel, 0);
        if (light) sirenLight(false);
        #endif
        delay(pauseDuration);
    }

    void playTone(int frequency, uint8_t duration) {
        playToneLit(frequency, duration, false);
    }

    void beaconTxBeep() {
        if (Config.notification.bellRings) { bell(Bell::BELL_TX); return; }
        digitalWrite(Config.notification.buzzerPinVcc, HIGH);
        playTone(1320,100);
        if (digipeaterActive) {
            playTone(1560,100);
        }
        digitalWrite(Config.notification.buzzerPinVcc, LOW);
    }

    void messageBeep() {
        if (Config.notification.bellRings) { bell(Bell::BELL_MSG_TO_ME); return; }
        digitalWrite(Config.notification.buzzerPinVcc, HIGH);
        playTone(1100,100);
        playTone(1100,100);
        digitalWrite(Config.notification.buzzerPinVcc, LOW);
    }

    void stationHeardBeep() {
        if (Config.notification.bellRings) { bell(Bell::BELL_BEACON_RX); return; }
        digitalWrite(Config.notification.buzzerPinVcc, HIGH);
        playTone(1200,100);
        playTone(600,100);
        digitalWrite(Config.notification.buzzerPinVcc, LOW);
    }

    void countdownBeep() {
        digitalWrite(Config.notification.buzzerPinVcc, HIGH);
        playToneLit(1500,80,true);
        digitalWrite(Config.notification.buzzerPinVcc, LOW);
    }

    void alarmBeep() {
        digitalWrite(Config.notification.buzzerPinVcc, HIGH);
        playToneLit(1800,120,true);
        playToneLit(1200,120,true);
        playToneLit(1800,120,true);
        playToneLit(1200,120,true);
        digitalWrite(Config.notification.buzzerPinVcc, LOW);
    }

    void shutDownBeep() {
        digitalWrite(Config.notification.buzzerPinVcc, HIGH);
        for (int i = 0; i < sizeof(shutDownSound) / sizeof(shutDownSound[0]); i++) {
            playTone(shutDownSound[i], shutDownSoundDuration[i]);
        }
        digitalWrite(Config.notification.buzzerPinVcc, LOW);
    }

    void lowBatteryBeep() {
        digitalWrite(Config.notification.buzzerPinVcc, HIGH);
        playTone(1550,100);
        playTone(650,100);
        playTone(1550,100);
        playTone(650,100);
        digitalWrite(Config.notification.buzzerPinVcc, LOW);
    }

    void start() {
        digitalWrite(Config.notification.buzzerPinVcc, HIGH);
        for (int i = 0; i < sizeof(startUpSound) / sizeof(startUpSound[0]); i++) {
            playTone(startUpSound[i], startUpSoundDuration[i]);
        }
        digitalWrite(Config.notification.buzzerPinVcc, LOW);
    }

}