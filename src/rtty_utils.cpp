/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ tracker.
 * Licensed under the GNU General Public License, version 3 or later.
 *
 * Motor del modo RTTY: baliza de telemetria/texto por FSK con el RTTYClient de RadioLib 8.0.x.
 */
#include "ssdv_config.h"

#if defined(HAS_RTTY)

#include <RadioLib.h>
#include <TinyGPS++.h>
#include <logger.h>
#include "rtty_utils.h"
#include "configuration.h"
#include "lora_utils.h"

extern logging::Logger  logger;
extern Configuration    Config;
extern Beacon           *currentBeacon;
extern TinyGPSPlus       gps;

namespace RTTY_Utils {

    static RTTYClient*  client = nullptr;
    static char         statusText[48] = "RTTY idle";
    static char         lastMsg[96]    = "";
    static uint32_t     count    = 0;
    static bool         active   = false;

    static void setStatus(const char* s) { strncpy(statusText, s, sizeof(statusText) - 1); statusText[sizeof(statusText) - 1] = '\0'; }

    static void baseCall(char* out, size_t n) {
        const char* src = currentBeacon ? currentBeacon->callsign.c_str() : "NOCALL";
        size_t j = 0;
        for (size_t i = 0; src[i] && src[i] != '-' && j < n - 1; i++) out[j++] = src[i];
        out[j] = '\0';
        if (j == 0) strncpy(out, "NOCALL", n);
    }

    // CRC16-CCITT (0xFFFF, poly 0x1021) sobre el texto entre $$ y *, como pide el formato UKHAS
    static uint16_t crc16(const char* s) {
        uint16_t crc = 0xFFFF;
        for (; *s; s++) {
            crc ^= (uint16_t)(*s) << 8;
            for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
        return crc;
    }

    // Construye la sentencia a emitir (sin CR/LF). Devuelve false si en modo telemetria no hay posicion.
    static bool buildSentence(char* out, size_t n) {
        char call[16]; baseCall(call, sizeof(call));
        if (!Config.rtty.telemetry) {
            String t = Config.rtty.text.length() ? Config.rtty.text : String("$$") + call + " APRS LoRa+ RTTY";
            t.replace("{CALL}", call);
            strncpy(out, t.c_str(), n - 1); out[n - 1] = '\0';
            return true;
        }
        if (!gps.location.isValid()) { setStatus("RTTY: WAIT GPS POS"); return false; }
        int hh = gps.time.isValid() ? gps.time.hour() : 0;
        int mm = gps.time.isValid() ? gps.time.minute() : 0;
        int ss = gps.time.isValid() ? gps.time.second() : 0;
        double lat = gps.location.lat(), lon = gps.location.lng();
        double alt = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
        int sats = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
        char body[88];
        snprintf(body, sizeof(body), "%s,%lu,%02d:%02d:%02d,%.5f,%.5f,%ld,%d",
                 call, (unsigned long)(count + 1), hh, mm, ss, lat, lon, (long)alt, sats);
        uint16_t crc = crc16(body);
        snprintf(out, n, "$$%s*%04X", body, crc);
        return true;
    }

    void begin() {
        if (!client) client = new RTTYClient(LoRa_Utils::ssdvPhy());
        active = true;
        setStatus("RTTY ready");
    }

    void end() {
        active = false;
        LoRa_Utils::rttyFinish();
        setStatus("RTTY idle");
    }

    bool sendBeacon() {
        if (!client) begin();
        char sentence[128];
        if (!buildSentence(sentence, sizeof(sentence))) return false;

        LoRa_Utils::rttyPrepare(Config.rtty.powerDbm);
        uint8_t enc = Config.rtty.enc == 2 ? RADIOLIB_ITA2 : (Config.rtty.enc == 1 ? RADIOLIB_ASCII_EXTENDED : RADIOLIB_ASCII);
        int st = client->begin((uint32_t)Config.rtty.freqHz, (uint32_t)Config.rtty.shiftHz, (uint16_t)Config.rtty.baud,
                               enc, (uint8_t)Config.rtty.stopBits);
        if (st != RADIOLIB_ERR_NONE) { LoRa_Utils::rttyFinish(); setStatus("RTTY: radio error"); return false; }

        client->idle();                         // tono de marca
        delay(500);                             // preambulo corto de marca para enganchar
        client->println(sentence);
        client->standby();
        LoRa_Utils::rttyFinish();

        count++;
        strncpy(lastMsg, sentence, sizeof(lastMsg) - 1); lastMsg[sizeof(lastMsg) - 1] = '\0';
        char buf[48]; snprintf(buf, sizeof(buf), "RTTY sent %lu", (unsigned long)count); setStatus(buf);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "RTTY", "Tx %s on %d Hz shift %d %d bd",
                   sentence, Config.rtty.freqHz, Config.rtty.shiftHz, Config.rtty.baud);
        return true;
    }

    const char* status() { return statusText; }
    uint32_t sentCount() { return count; }
    const char* lastSentence() { return lastMsg; }

}

#else  // !HAS_RTTY: stubs

namespace RTTY_Utils {
    void begin() {}
    void end() {}
    bool sendBeacon() { return false; }
    const char* status() { return "RTTY n/a"; }
    uint32_t sentCount() { return 0; }
    const char* lastSentence() { return ""; }
}

#endif
