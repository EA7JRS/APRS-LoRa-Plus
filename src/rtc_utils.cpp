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

// RTC de la T-Beam S3 Supreme (I2C1, 0x51). La documentacion de LilyGO menciona PCF8563 y PCF85063A, que
// tienen mapas de registros distintos, asi que el chip se identifica en marcha mirando que registro cuenta segundos.
// Uso: poner la hora del sistema antes del primer fix GPS y mantener el RTC sincronizado con el GPS.

#include "board_pinout.h"
#include "rtc_utils.h"

#ifdef TTGO_T_Beam_S3_SUPREME_V3

#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include "TimeLib.h"
#include <logger.h>

extern logging::Logger  logger;
extern TinyGPSPlus      gps;

namespace {

    constexpr uint8_t   RTC_ADDRESS         = 0x51;
    constexpr uint8_t   BASE_PCF8563        = 0x02;     // segundos en 0x02
    constexpr uint8_t   BASE_PCF85063       = 0x04;     // segundos en 0x04
    constexpr uint32_t  DETECT_WAIT_MS      = 1300;
    constexpr uint8_t   DETECT_MAX_TRIES    = 3;
    constexpr uint32_t  CHECK_PERIOD_MS     = 1000;
    constexpr uint32_t  GPS_SYNC_PERIOD_MS  = 3600000UL;

    enum RtcState { RTC_OFF, RTC_DETECTING, RTC_RUNNING };

    RtcState    state           = RTC_OFF;
    uint8_t     base            = 0;
    uint8_t     snapshot[12]    = {0};
    uint32_t    snapshotTime    = 0;
    uint8_t     detectTries     = 0;
    uint32_t    lastCheck       = 0;
    uint32_t    lastGpsSync     = 0;
    bool        systemApplied   = false;

    uint8_t fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
    uint8_t toBcd(uint8_t v)   { return ((v / 10) << 4) | (v % 10); }
    bool    isBcd(uint8_t v)   { return (v >> 4) <= 9 && (v & 0x0F) <= 9; }

    bool readRegs(uint8_t reg, uint8_t* buffer, uint8_t length) {
        Wire1.beginTransmission(RTC_ADDRESS);
        Wire1.write(reg);
        if (Wire1.endTransmission(false) != 0) return false;
        if (Wire1.requestFrom(RTC_ADDRESS, length) != length) return false;
        for (uint8_t i = 0; i < length; i++) buffer[i] = Wire1.read();
        return true;
    }

    bool writeRegs(uint8_t reg, const uint8_t* buffer, uint8_t length) {
        Wire1.beginTransmission(RTC_ADDRESS);
        Wire1.write(reg);
        Wire1.write(buffer, length);
        return Wire1.endTransmission() == 0;
    }

    // block = segundos, minutos, horas, dia, dia de la semana, mes, ano (mismo orden en ambos chips)
    bool decodeTime(const uint8_t* block, time_t& result, bool& flagSet) {
        flagSet = (block[0] & 0x80) != 0;
        uint8_t s  = block[0] & 0x7F;
        uint8_t m  = block[1] & 0x7F;
        uint8_t h  = block[2] & 0x3F;
        uint8_t d  = block[3] & 0x3F;
        uint8_t mo = block[5] & 0x1F;
        uint8_t y  = block[6];
        if (!isBcd(s) || !isBcd(m) || !isBcd(h) || !isBcd(d) || !isBcd(mo) || !isBcd(y)) return false;
        tmElements_t tm;
        tm.Second = fromBcd(s);
        tm.Minute = fromBcd(m);
        tm.Hour   = fromBcd(h);
        tm.Day    = fromBcd(d);
        tm.Month  = fromBcd(mo);
        int year  = 2000 + fromBcd(y);
        if (tm.Second > 59 || tm.Minute > 59 || tm.Hour > 23 || tm.Day < 1 || tm.Day > 31 || tm.Month < 1 || tm.Month > 12 || year < 2024) return false;
        tm.Year = year - 1970;
        result  = makeTime(tm);
        return true;
    }

    bool writeTime(time_t t) {
        tmElements_t tm;
        breakTime(t, tm);
        uint8_t block[7] = {
            toBcd(tm.Second),                       // bit 7 a 0: borra el indicador VL / OS
            toBcd(tm.Minute),
            toBcd(tm.Hour),
            toBcd(tm.Day),
            (uint8_t)(tm.Wday - 1),
            toBcd(tm.Month),
            toBcd((uint8_t)(tmYearToCalendar(tm.Year) - 2000))
        };
        return writeRegs(base, block, 7);
    }

    bool readRtcTime(time_t& result, bool& flagSet) {
        uint8_t block[7];
        if (!readRegs(base, block, 7)) return false;
        return decodeTime(block, result, flagSet);
    }

    bool takeSnapshot() {
        if (!readRegs(0x00, snapshot, 12)) return false;
        snapshotTime = millis();
        return true;
    }

    bool gpsTimeFresh() {
        return gps.time.isValid() && gps.date.isValid() && gps.time.age() < 1500 && gps.date.year() >= 2024;
    }

    time_t gpsTime() {
        tmElements_t tm;
        tm.Year   = gps.date.year() - 1970;
        tm.Month  = gps.date.month();
        tm.Day    = gps.date.day();
        tm.Hour   = gps.time.hour();
        tm.Minute = gps.time.minute();
        tm.Second = gps.time.second();
        return makeTime(tm);
    }

    void applyRtcToSystem() {
        systemApplied = true;
        if (year() >= 2024) return;                 // el GPS ya puso la hora
        time_t t;
        bool   flagSet;
        if (readRtcTime(t, flagSet) && !flagSet) {
            setTime(t);
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "RTC", "Hora del sistema tomada del RTC");
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "RTC", "RTC sin hora valida, esperando GPS");
        }
    }

    void syncFromGps() {
        uint32_t now = millis();
        if (lastGpsSync != 0 && now - lastGpsSync < GPS_SYNC_PERIOD_MS) return;
        if (!gpsTimeFresh()) return;
        lastGpsSync = now ? now : 1;
        time_t g = gpsTime();
        time_t r;
        bool   flagSet;
        if (readRtcTime(r, flagSet) && !flagSet) {
            long diff = (long)(r - g);
            if (diff >= -1 && diff <= 1) return;
        }
        if (writeTime(g)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "RTC", "RTC actualizado desde el GPS");
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "RTC", "Error al escribir el RTC");
        }
    }

    void detectLayout() {
        if (millis() - snapshotTime < DETECT_WAIT_MS) return;
        uint8_t current[12];
        if (!readRegs(0x00, current, 12)) {
            state = RTC_OFF;
            return;
        }
        bool changed8563  = ((current[BASE_PCF8563] ^ snapshot[BASE_PCF8563]) & 0x7F) != 0;
        bool changed85063 = ((current[BASE_PCF85063] ^ snapshot[BASE_PCF85063]) & 0x7F) != 0;
        if (changed8563 != changed85063) {
            base  = changed8563 ? BASE_PCF8563 : BASE_PCF85063;
            state = RTC_RUNNING;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "RTC", "Detectado %s", changed8563 ? "PCF8563" : "PCF85063A");
            if (!systemApplied) applyRtcToSystem();
            syncFromGps();
            return;
        }
        detectTries++;
        if (detectTries >= DETECT_MAX_TRIES) {
            state = RTC_OFF;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "RTC", "No se pudo identificar el chip (el reloj no avanza), desactivado");
            return;
        }
        if (!takeSnapshot()) state = RTC_OFF;
    }

}

namespace RTC_Utils {

    void setup() {
        Wire1.beginTransmission(RTC_ADDRESS);
        if (Wire1.endTransmission() != 0) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "RTC", "RTC no encontrado en 0x51");
            return;
        }
        uint8_t control1;
        if (!readRegs(0x00, &control1, 1)) return;
        if (control1 & (0x20 | 0x02)) {             // limpiar bit STOP (0x20) y forzar modo 24 h (bit 0x02 a 0)
            control1 &= ~(0x20 | 0x02);
            writeRegs(0x00, &control1, 1);
        }
        if (!takeSnapshot()) return;

        // Si en la instantanea inicial uno de los dos mapas ya contiene una fecha/hora BCD valida (>= 2024 y sin flag VL/OS),
        // adelantamos la hora del sistema de inmediato sin esperar los 1,3 s de deteccion
        time_t t8563 = 0, t85063 = 0;
        bool flag8563 = true, flag85063 = true;
        bool ok8563  = decodeTime(snapshot + BASE_PCF8563,  t8563,  flag8563)  && !flag8563;
        bool ok85063 = decodeTime(snapshot + BASE_PCF85063, t85063, flag85063) && !flag85063;
        if (ok8563 != ok85063 && year() < 2024) {
            setTime(ok8563 ? t8563 : t85063);
            systemApplied = true;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "RTC", "Hora inicial cargada en setup (%s)", ok8563 ? "PCF8563" : "PCF85063A");
        }

        detectTries = 0;
        state       = RTC_DETECTING;
    }

    void update() {
        if (state == RTC_OFF) return;
        uint32_t now = millis();
        if (now - lastCheck < CHECK_PERIOD_MS) return;
        lastCheck = now;
        if (state == RTC_DETECTING) {
            detectLayout();
            return;
        }
        if (!systemApplied) applyRtcToSystem();
        syncFromGps();
    }

    bool isReady() {
        return state == RTC_RUNNING;
    }

}

#else

namespace RTC_Utils {
    void setup() {}
    void update() {}
    bool isReady() { return false; }
}

#endif