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

#include "smartbeacon_utils.h"
#include "configuration.h"
#include "winlink_utils.h"
#include "ais_profiles.h"
#include "gps_utils.h"
#include "imu_utils.h"
#include <TinyGPS++.h>

extern Configuration    Config;
extern Beacon           *currentBeacon;
extern bool             smartBeaconActive;
extern uint32_t         txInterval;
extern uint32_t         lastTxTime;
extern bool             sendUpdate;
extern uint8_t          winlinkStatus;
extern TinyGPSPlus      gps;


SmartBeaconValues   currentSmartBeaconValues;
byte                smartBeaconSettingsIndex    = 10;
bool                wxRequestStatus             = false;
uint32_t            wxRequestTime               = 0;


SmartBeaconValues   smartBeaconSettings[6] = {
    {120,  3, 60, 15,  50, 20, 12, 60, 0},      // Runner settings  = SLOW
    {120,  5, 60, 40, 100, 12, 12, 60, 0},      // Bike settings    = MEDIUM
    {120, 10, 60, 70, 100, 12, 10, 80, 0},      // Car settings     = FAST
    // Perfiles AIS (ITU-R M.1371-6): el intervalo sale de una tabla por velocidad y cambio de rumbo (ais_profiles.cpp),
    // sin condicion de distancia ni corner pegging. Los valores de abajo solo son informativos.
    {180,  2,  2, 43,   0,  0, 360, 0, AIS_Profiles::CLASS_A},      // AIS Clase A
    {180,  4,  5, 43,   0,  0, 360, 0, AIS_Profiles::CLASS_B_SO},   // AIS Clase B "SO"
    {180,  4, 15, 26,   0,  0, 360, 0, AIS_Profiles::CLASS_B_CS}    // AIS Clase B "CS"
};


namespace SMARTBEACON_Utils {

    void checkSettings(byte index) {
        if (index >= sizeof(smartBeaconSettings) / sizeof(smartBeaconSettings[0])) index = 0;      // configuracion no valida
        if (smartBeaconSettingsIndex != index) {
            currentSmartBeaconValues = smartBeaconSettings[index];
            smartBeaconSettingsIndex = index;
            AIS_Profiles::reset();
            txInterval = (uint32_t)currentSmartBeaconValues.slowRate * 1000UL;
        }
    }

    void checkInterval(int speed) {
        if (smartBeaconActive) {
            if (currentSmartBeaconValues.mode != AIS_Profiles::CLASSIC) {           // perfiles AIS: intervalo por tabla
                txInterval = AIS_Profiles::updateInterval(currentSmartBeaconValues.mode, GPS_Utils::getSpeedKmph(),
                                                          GPS_Utils::getCourseDeg(), gps.course.isValid(),
                                                          millis(), IMU_Utils::getYawRateDps());
                return;
            }
            if (speed < currentSmartBeaconValues.slowSpeed) {
                txInterval = (uint32_t)currentSmartBeaconValues.slowRate * 1000UL;
            } else if (speed > currentSmartBeaconValues.fastSpeed) {
                txInterval = (uint32_t)currentSmartBeaconValues.fastRate * 1000UL;
            } else {
                int safeSpeed = (speed > 0) ? speed : 1;
                int rateSec   = min(currentSmartBeaconValues.slowRate, currentSmartBeaconValues.fastSpeed * currentSmartBeaconValues.fastRate / safeSpeed);
                txInterval    = (uint32_t)rateSec * 1000UL;
            }
        }
    }

    void checkFixedBeaconTime() {
        if (!smartBeaconActive) {
            uint32_t lastTxSmartBeacon = millis() - lastTxTime;
            if (lastTxSmartBeacon >= (uint32_t)Config.nonSmartBeaconRate * 60000UL) sendUpdate = true;
        }
    }

    void checkState() {
        if (wxRequestStatus && (millis() - wxRequestTime) > 20000UL) wxRequestStatus = false;
        smartBeaconActive = (winlinkStatus == 0 && !wxRequestStatus && currentBeacon != nullptr) ? currentBeacon->smartBeaconActive : false;
    }

}