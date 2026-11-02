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

// Funciones auxiliares del GPS: filtro de velocidad/rumbo con la unidad parada y ultima posicion valida.

#include "board_pinout.h"
#include "gps_utils.h"
#include "baro_utils.h"
#include "imu_utils.h"
#include <TinyGPS++.h>

extern TinyGPSPlus gps;

#ifdef GPS_SPEED_FILTER

#ifndef GPS_SPEED_START_KMPH
    #define GPS_SPEED_START_KMPH    3.5f        // por encima de esto (3 lecturas seguidas) se considera movimiento
#endif
#ifndef GPS_SPEED_STOP_KMPH
    #define GPS_SPEED_STOP_KMPH     1.8f        // por debajo de esto (3 lecturas seguidas) se considera parado
#endif
#ifndef GPS_COURSE_MIN_KMPH
    #define GPS_COURSE_MIN_KMPH     3.0f        // por debajo de esta velocidad el rumbo del GPS no es fiable
#endif

namespace {
    bool    moving          = false;
    uint8_t aboveCount      = 0;
    uint8_t belowCount      = 0;
    float   filteredSpeed   = 0.0f;
    float   heldCourse      = 0.0f;
}

namespace GPS_Utils {

    void updateSpeedFilter() {
        if (!gps.speed.isValid()) return;
        float raw = gps.speed.kmph();

        // Si el IMU QMI8658 esta operativo y confirma reposo fisico, ignorar picos de deriva GPS (< 12 km/h)
        bool imuQuiet = IMU_Utils::isReady() && !IMU_Utils::movingNow();
        if (imuQuiet && raw < 12.0f) {
            moving        = false;
            aboveCount    = 0;
            belowCount    = 0;
            filteredSpeed = 0.0f;
            return;
        }

        if (!moving) {
            aboveCount = (raw >= GPS_SPEED_START_KMPH) ? aboveCount + 1 : 0;
            if (aboveCount >= 3) {
                moving     = true;
                aboveCount = 0;
                belowCount = 0;
            }
        } else {
            belowCount = (raw < GPS_SPEED_STOP_KMPH) ? belowCount + 1 : 0;
            if (belowCount >= 3) {
                moving     = false;
                belowCount = 0;
                aboveCount = 0;
            }
        }
        filteredSpeed = moving ? raw : 0.0f;
        if (moving && raw >= GPS_COURSE_MIN_KMPH && gps.course.isValid()) heldCourse = gps.course.deg();
    }

    float getSpeedKmph()    { return filteredSpeed; }
    float getSpeedKnots()   { return filteredSpeed / 1.852f; }
    float getCourseDeg()    { return heldCourse; }
    bool  isMoving()        { return moving; }

}

#else

namespace GPS_Utils {
    void  updateSpeedFilter() {}
    float getSpeedKmph()    { return gps.speed.kmph(); }
    float getSpeedKnots()   { return gps.speed.knots(); }
    float getCourseDeg()    { return gps.course.deg(); }
    bool  isMoving()        { return gps.speed.kmph() > 0.5; }
}

#endif

#ifdef GPS_BARO_ALTITUDE

namespace GPS_Utils {
    float getAltitudeMeters() {
        return BARO_Utils::altitudeValid() ? BARO_Utils::getAltitude() : (float) gps.altitude.meters();
    }
    float getAltitudeFeet() {
        return getAltitudeMeters() * 3.28084f;
    }
}

#else

namespace GPS_Utils {
    float getAltitudeMeters()   { return (float) gps.altitude.meters(); }
    float getAltitudeFeet()     { return (float) gps.altitude.feet(); }
}

#endif

#ifdef GPS_LAST_POSITION

#include <Arduino.h>
#include <SPIFFS.h>

#ifndef GPS_LAST_POS_MIN_MOVE_M
    #define GPS_LAST_POS_MIN_MOVE_M     50.0
#endif
#ifndef GPS_LAST_POS_MIN_INTERVAL_MS
    #define GPS_LAST_POS_MIN_INTERVAL_MS    120000UL
#endif

namespace {
    constexpr char      POS_FILE[]  = "/last_pos.dat";
    constexpr uint16_t  POS_MAGIC   = 0x4C51;       // v2: incluye la altitud

    struct PosFile {
        uint16_t    magic;
        double      lat;
        double      lng;
        float       alt;
        uint8_t     altKnown;
    };

    bool        haveLast    = false;
    double      lastLat     = 0;
    double      lastLng     = 0;
    float       lastAlt     = 0;
    bool        altKnown    = false;
    uint32_t    lastSave    = 0;

    bool validCoordinates(double lat, double lng) {
        return lat >= -90.0 && lat <= 90.0 && lng >= -180.0 && lng <= 180.0 && !(lat == 0.0 && lng == 0.0);
    }

    void save(double lat, double lng, float alt, bool known) {
        File file = SPIFFS.open(POS_FILE, "w");
        if (!file) return;
        PosFile data = {POS_MAGIC, lat, lng, alt, (uint8_t)(known ? 1 : 0)};
        file.write((const uint8_t*)&data, sizeof(data));
        file.close();
        haveLast = true;
        lastLat  = lat;
        lastLng  = lng;
        lastAlt  = alt;
        altKnown = known;
        lastSave = millis() ? millis() : 1;
    }
}

namespace GPS_Utils {

    void loadLastPosition() {
        File file = SPIFFS.open(POS_FILE, "r");
        if (!file) return;
        PosFile data;
        bool ok = file.read((uint8_t*)&data, sizeof(data)) == sizeof(data) && data.magic == POS_MAGIC && validCoordinates(data.lat, data.lng);
        file.close();
        if (!ok) return;
        haveLast = true;
        lastLat  = data.lat;
        lastLng  = data.lng;
        lastAlt  = data.alt;
        altKnown = data.altKnown != 0;
    }

    // Llamar solo cuando hay actualizacion de posicion (lat()/lng() borran el indicador "updated" de TinyGPS)
    void updateLastPosition() {
        if (!gps.location.isValid() || gps.satellites.value() < 4 || !gps.hdop.isValid() || gps.hdop.hdop() > 5.0) return;
        double lat = gps.location.lat();
        double lng = gps.location.lng();
        if (!validCoordinates(lat, lng)) return;
        bool  known = gps.altitude.isValid() || BARO_Utils::altitudeValid();
        float alt   = known ? getAltitudeMeters() : 0.0f;
        if (!haveLast) {
            save(lat, lng, alt, known);
            return;
        }
        if (millis() - lastSave < GPS_LAST_POS_MIN_INTERVAL_MS && lastSave != 0) return;
        if (TinyGPSPlus::distanceBetween(lat, lng, lastLat, lastLng) > GPS_LAST_POS_MIN_MOVE_M) save(lat, lng, alt, known);
    }

    bool    hasLastPosition()   { return haveLast; }
    double  getLastLat()        { return lastLat; }
    double  getLastLng()        { return lastLng; }
    bool    hasLastAltitude()   { return haveLast && altKnown; }
    float   getLastAltitude()   { return lastAlt; }

}

#else

namespace GPS_Utils {
    void    loadLastPosition() {}
    void    updateLastPosition() {}
    bool    hasLastPosition()   { return false; }
    double  getLastLat()        { return 0; }
    double  getLastLng()        { return 0; }
    bool    hasLastAltitude()   { return false; }
    float   getLastAltitude()   { return 0; }
}

#endif