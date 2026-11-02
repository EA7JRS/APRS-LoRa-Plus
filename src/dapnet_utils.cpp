/*
 * Modo DAPNET: receptor de buscapersonas POCSAG (solo recepcion). Ver include/dapnet_utils.h
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <TinyGPS++.h>
#include "TimeLib.h"
#include "dapnet_utils.h"
#include "configuration.h"
#include "logger.h"

extern Configuration    Config;
extern TinyGPSPlus      gps;
extern bool             gpsIsActive;
extern logging::Logger  logger;

namespace DAPNET_Utils {

    namespace {
        Pocsag::Decoder decoder;
        Entry           history[HISTORY];
        uint8_t         stored      = 0;
        uint8_t         head        = 0;            // posicion del siguiente en el anillo
        uint32_t        receivedCount = 0;
        bool            ownLast     = false;
        int             rssiNow     = 0;
        uint32_t        syncMs      = 0;
        uint32_t        seenSyncs   = 0;
        uint32_t        ownRics[8];
        uint8_t         ownN        = 0;
        uint32_t        timeMs      = 0;
        bool            timeUtc     = false;
        bool            applied     = false;
        char            timeText[40] = "";

        // RIC de hora que difunde DAPNET (buscapersonas): UTC y hora local. Fuente: wiki de DAPNET (comparativa de buscapersonas)
        const uint32_t TIME_UTC_RICS[]   = { 2504, 216, 200 };      // Skyper, AlphaPOC, Swissphone
        const uint32_t TIME_LOCAL_RICS[] = { 224, 208, 2000 };      // AlphaPOC CE(S)T, Swissphone local, TPL Birdy (zona sin confirmar)
        const uint32_t TX_IDENT_RIC      = 8;               // cada transmisor emite aqui su indicativo cada 10 minutos

        bool inList(const uint32_t* list, size_t n, uint32_t ric) { for (size_t i = 0; i < n; i++) if (list[i] == ric) return true; return false; }

        bool gpsTimeOk() { return gpsIsActive && gps.time.isValid() && gps.date.isValid() && gps.date.year() >= 2024 && gps.time.age() < 10000; }

        void sanitize(char* text) {                     // la pantalla solo dibuja ASCII imprimible
            for (char* p = text; *p; p++) if ((uint8_t)*p < 0x20 || (uint8_t)*p > 0x7E) *p = '?';
        }

        void handleTime(const Pocsag::Message& m, bool utc) {
            char numeric[64], alpha[64];
            Pocsag::decodeNumeric(m, numeric, sizeof(numeric));
            Pocsag::decodeAlpha(m, alpha, sizeof(alpha), Pocsag::CHARSET_ASCII);
            Pocsag::TimeStamp t;
            const char* used = nullptr;
            if (Pocsag::parseTime(numeric, t)) used = numeric;          // la hora llega como texto: se prueba como numerico y como alfanumerico
            else if (Pocsag::parseTime(alpha, t)) used = alpha;
            if (!used) { logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "DAPNET", "Time message not understood (RIC %lu): %s / %s", (unsigned long)m.ric, numeric, alpha); return; }
            snprintf(timeText, sizeof(timeText), "%02u:%02u:%02u %02u/%02u/%04u", t.hour, t.minute, t.second, t.day, t.month, (unsigned)t.year);
            timeMs  = millis() ? millis() : 1;
            timeUtc = utc;
            if (utc && Config.dapnet.timeSync && !gpsTimeOk()) {        // solo la hora UTC, y solo si el GPS no da hora
                setTime(t.hour, t.minute, t.second, t.day, t.month, t.year);
                applied = true;
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "DAPNET", "Clock set from DAPNET: %s UTC", timeText);
            }
        }

        void onMessage(const Pocsag::Message& m, void*) {
            if (inList(TIME_UTC_RICS, sizeof(TIME_UTC_RICS) / sizeof(TIME_UTC_RICS[0]), m.ric))   { handleTime(m, true);  return; }
            if (inList(TIME_LOCAL_RICS, sizeof(TIME_LOCAL_RICS) / sizeof(TIME_LOCAL_RICS[0]), m.ric)) { handleTime(m, false); return; }
            const bool own = inList(ownRics, ownN, m.ric);
            if (!own && m.ric != TX_IDENT_RIC && !Config.dapnet.monitorAll) return;
            Entry& e = history[head];
            e.ric = m.ric; e.func = m.func; e.own = own; e.corrupt = m.corrupt; e.atMs = millis(); e.rssi = rssiNow;
            if (m.func == 0) Pocsag::decodeNumeric(m, e.text, sizeof(e.text));
            else             Pocsag::decodeAlpha(m, e.text, sizeof(e.text), (uint8_t)Config.dapnet.charset);
            sanitize(e.text);
            head = (uint8_t)((head + 1) % HISTORY);
            if (stored < HISTORY) stored++;
            receivedCount++;
            ownLast = own;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "DAPNET", "RIC %lu F%u: %s%s", (unsigned long)m.ric, (unsigned)m.func, e.text, m.corrupt ? " (errors)" : "");
        }
    }

    void reset() {
        decoder.reset(2);
        ownN = Pocsag::parseRicList(Config.dapnet.rics.c_str(), ownRics, 8);
        seenSyncs = 0; syncMs = 0;
    }

    void feed(const uint8_t* data, size_t len, int rssi) {
        rssiNow = rssi;
        decoder.feed(data, len, onMessage, nullptr);
        // el mensaje en curso se entrega al llegar el siguiente sincronismo, la palabra de relleno o el fin de la emision (siguiente bloque)
        if (decoder.syncs() != seenSyncs) { seenSyncs = decoder.syncs(); syncMs = millis() ? millis() : 1; }
    }

    void clear() { stored = 0; head = 0; }
    uint8_t count() { return stored; }
    const Entry* at(uint8_t i) { if (i >= stored) return nullptr; return &history[(head + HISTORY - 1 - i) % HISTORY]; }
    uint32_t received() { return receivedCount; }
    bool lastWasOwn() { return ownLast; }
    int lastRssi() { return rssiNow; }
    uint32_t batches() { return decoder.batches(); }
    uint32_t messages() { return decoder.messages(); }
    uint32_t badWords() { return decoder.badWords(); }
    uint32_t lastSyncMs() { return syncMs; }
    bool inverted() { return decoder.inverted(); }
    uint8_t ownCount() { return ownN; }
    uint32_t lastTimeMs() { return timeMs; }
    bool lastTimeWasUtc() { return timeUtc; }
    const char* lastTimeText() { return timeText; }
    bool timeApplied() { return applied; }

}
