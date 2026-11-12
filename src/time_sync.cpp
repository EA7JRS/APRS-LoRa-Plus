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

#include "time_sync.h"
#include <WiFi.h>
#include <TinyGPS++.h>
#include <sys/time.h>
#include <time.h>
#include <esp_sntp.h>
#include "TimeLib.h"
#include "rtc_utils.h"
#include <logger.h>

extern TinyGPSPlus          gps;
extern bool                 gpsIsActive;
extern logging::Logger      logger;

namespace TIME_Utils {

    namespace {
        Source           src       = SRC_NONE;
        uint32_t         setMs     = 0;
        bool             started   = false;
        volatile bool    ntpDone   = false;         // lo marca el cliente SNTP (tarea de lwIP); se aplica en loop()
        uint32_t         lastCheck = 0;

        void onSntp(struct timeval*) { ntpDone = true; }

        void mark(Source s) { src = s; setMs = millis() ? millis() : 1; }
    }

    bool gpsTimeOk() {
        return gpsIsActive && gps.time.isValid() && gps.date.isValid() && gps.date.year() >= 2024 && gps.time.age() < 10000;
    }

    void gpsTimeSet() {
        if (!gpsTimeOk()) return;
        if (src != SRC_GPS) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "TIME", "Hora del GPS (antes %s)", sourceName());
        }
        mark(SRC_GPS);
        const time_t t = now();                     // reloj del sistema igual que TimeLib (+-1 s)
        const time_t sys = time(nullptr);
        if (sys < t - 1 || sys > t + 1) {
            struct timeval tv = {t, 0};
            settimeofday(&tv, nullptr);
        }
    }

    void note(Source s) {
        if (src == SRC_GPS && gpsTimeOk()) return;  // manda el GPS
        mark(s);
    }

    void loop() {
        if (ntpDone) {                              // NTP acaba de poner el reloj del sistema
            ntpDone = false;
            const time_t t = time(nullptr);
            if (!gpsTimeOk() && t > 1704067200) {   // sin GPS (y la hora es de 2024 o despues): a TimeLib y al RTC
                setTime(t);
                mark(SRC_NTP);
                RTC_Utils::syncFromSystem();
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "TIME", "Hora de NTP (%s)", NTP_SERVER_1);
            }
        }
        if (started) return;
        const uint32_t ms = millis();
        if (ms - lastCheck < 1000) return;
        lastCheck = ms;
        if (!(WiFi.getMode() & WIFI_MODE_STA) || WiFi.status() != WL_CONNECTED) return;
        sntp_set_time_sync_notification_cb(onSntp);
        configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);    // UTC: el firmware trabaja en UTC y aplica la zona al mostrar
        started = true;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "TIME", "NTP: %s, %s, %s", NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    }

    Source source() { return src; }

    const char* sourceName() {
        switch (src) {
            case SRC_GPS:    return "GPS";
            case SRC_NTP:    return "NTP";
            case SRC_RTC:    return "RTC";
            case SRC_DAPNET: return "DAPNET";
            default:         return "NONE";
        }
    }

    uint32_t sinceMs() { return setMs ? millis() - setMs : 0; }
    bool ntpStarted() { return started; }
}
