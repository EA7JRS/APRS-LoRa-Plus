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

#include "board_pinout.h"
#include "sleep_utils.h"
#include "power_utils.h"
#include <logger.h>
#include <TinyGPS++.h>
#include "imu_utils.h"
#include "gps_utils.h"
#include "pages_utils.h"
#include "configuration.h"


extern uint32_t         lastGPSTime;
extern bool             gpsIsActive;
extern bool             disableGPS;
extern bool             sosActive;
extern bool             batteryConnected;
extern uint32_t         txInterval;
extern TinyGPSPlus      gps;
extern HardwareSerial   gpsSerial;
extern Configuration    Config;
extern Beacon           *currentBeacon;
extern logging::Logger  logger;

bool gpsShouldSleep     = false;
bool gpsManualOff       = false;        // apagado a mano desde la pantalla GPS: no se despierta solo


namespace SLEEP_Utils {

    void gpsSleep() {
        #ifdef HAS_GPS_CTRL
            if (Config.operatingMode == MODE_WSPR || Config.operatingMode == MODE_LRFHSS || Config.operatingMode == MODE_FT8 || Config.operatingMode == MODE_FT4 || Config.operatingMode == MODE_FST4W || Config.operatingMode == MODE_SAT) {            // SAT, WSPR, LR-FHSS, FT8 y FT4 necesitan la hora del GPS siempre activa
                gpsShouldSleep = false;
                return;
            }
            // El modo eco es por perfil: un perfil sin eco nunca duerme el GPS
            if ( currentBeacon == nullptr || !currentBeacon->gpsEcoMode ) {
                gpsShouldSleep = false;
                return;
            }
            #ifdef TTGO_T_Beam_S3_SUPREME_V3
                // El backup del M10 sale de la 18650: sin bateria cada reactivacion seria un arranque en frio
                if (!batteryConnected) {
                    gpsShouldSleep = false;
                    return;
                }
            #endif
            // Con movimiento sostenido el GPS se queda encendido (si no hay IMU, isReady() es falso y no cambia nada)
            if (IMU_Utils::isReady() && IMU_Utils::movingNow()) {
                gpsShouldSleep = false;
                return;
            }
            if (gpsIsActive) {
                POWER_Utils::deactivateGPS();
                gpsShouldSleep = false;
                lastGPSTime = millis() ? millis() : 1;
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "GPS", "GPS SLEEPING");
            }
        #else
            gpsShouldSleep = false;
        #endif
    }

    void gpsWakeUp() {
        #ifdef HAS_GPS_CTRL
            if (disableGPS) return;
            // Si fue apagado a mano desde la pantalla GPS, solo despertar si hay emergencia activa
            #ifdef HAS_APRS_PAGES
                if (gpsManualOff && !PAGES_Utils::emergencyActive() && !sosActive) return;
            #else
                if (gpsManualOff && !sosActive) return;
            #endif
            if (!gpsIsActive) {
                POWER_Utils::activateGPS();
                while (gpsSerial.available() > 0) gpsSerial.read();     // descartar posibles bytes truncados en UART
                GPS_Utils::reapplyAfterWake();                          // L76K: tras quitar la alimentacion vuelve a sus valores de fabrica
                gpsShouldSleep = false;
                gpsManualOff = false;
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "GPS", "GPS WAKEUP");
            }
        #endif
    }

    bool gpsShouldWakeUp() {
        if (disableGPS) return false;
        #ifdef HAS_APRS_PAGES
            if (PAGES_Utils::emergencyActive()) return true;
        #endif
        if (sosActive) return true;
        if (gpsManualOff) return false;
        if (currentBeacon == nullptr || !currentBeacon->gpsEcoMode) return true;    // perfil sin eco: GPS siempre encendido

        uint32_t elapsed = millis() - lastGPSTime;
        if (IMU_Utils::isReady()) {
            if (IMU_Utils::movedSince(lastGPSTime)) return true;                    // movimiento: despertar ya
            if (GPS_Utils::getSpeedKmph() < 1.0f) {                                 // ultimo fix parado y sin movimiento: dormir mas
                uint32_t wait = txInterval > IMU_STATIONARY_WAKE_MS ? txInterval : IMU_STATIONARY_WAKE_MS;
                return elapsed > wait;
            }
        }
        return elapsed > txInterval;
    }

    void toggleGpsManual() {
        #ifdef HAS_GPS_CTRL
            if (disableGPS) return;
            if (gpsIsActive) {
                gpsManualOff = true;
                gpsShouldSleep = false;
                POWER_Utils::deactivateGPS();
                lastGPSTime = millis() ? millis() : 1;
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "GPS OFF (manual)");
            } else {
                gpsManualOff = false;
                gpsWakeUp();
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "GPS", "GPS ON (manual)");
            }
        #endif
    }

    void checkIfGPSShouldSleep() {
        #ifdef HAS_APRS_PAGES
            if (PAGES_Utils::emergencyActive()) {           // en emergencia el GPS no se duerme
                gpsShouldSleep = false;
                return;
            }
        #endif
        if (sosActive) {
            gpsShouldSleep = false;
            return;
        }
        if (gpsShouldSleep) {
            gpsSleep();
        }
    }

}