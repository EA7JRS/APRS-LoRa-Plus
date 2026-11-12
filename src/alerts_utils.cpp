/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "alerts_utils.h"

#include <TimeLib.h>
#include <vector>

#include "configuration.h"
#include "notification_utils.h"
#include "same.h"
#include <logger.h>
#ifdef HAS_SA868
#include "sa868_radio.h"
#endif

extern Configuration   Config;
extern logging::Logger logger;

namespace ALERTS {

    static plotter::AlertList       items;
    static plotter::SpaceWx         sw;
    static uint32_t                 swMs        = 0;
    static uint16_t                 ver         = 0;
    static std::vector<std::string> unseen;                 // id de los avisos nuevos (para la ventana)
    static std::vector<std::string> closed;                 // cerrados por el usuario (con su nivel, "id#2")
    static bool                     buzzing     = false;
    static uint32_t                 buzzStartMs = 0, buzzBeepMs = 0;
    static uint32_t                 lastExpireMs = 0;

    static int64_t nowUnix() {
        const time_t t = now();
        return year(t) >= 2024 ? (int64_t)t : 0;
    }

    static std::string closedKey(const plotter::Alert& a) {
        return a.id + "#" + std::to_string((int)a.level);
    }

    static void notifyNew(const plotter::Alert& a) {
        for (const auto& c : closed) if (c == closedKey(a)) return;
        if (a.level < Config.display.alertPopup || Config.display.alertPopup > 3) return;
        for (const auto& u : unseen) if (u == a.id) return;
        unseen.push_back(a.id);
        if (a.level >= Config.display.alertBuzz && Config.display.alertBuzz <= 3) {
            buzzing = true;
            buzzStartMs = millis();
            buzzBeepMs = 0;
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Alerts", "%s %s: %s %s", plotter::alertSrcName(a.src),
                   plotter::alertLevelName(a.level), a.event.c_str(), a.area.c_str());
    }

    void add(const plotter::Alert& a, bool notify) {
        const bool raised = items.upsert(a);
        ver++;
        if (notify && raised) notifyNew(a);
    }

    static std::string cut(const char* s, size_t n) {
        std::string v = plotter::asciiFold(s ? s : "");        // la fuente de la pantalla es de 7 bits
        if (v.size() > n) v.resize(n);
        return v;
    }

    bool fromJson(JsonVariantConst doc, String& error) {
        plotter::Alert a;
        a.id = doc["id"] | "";
        if (a.id.empty()) { error = "no id"; return false; }
        a.src = plotter::alertSrcFromCode(doc["s"] | "");
        a.level = (uint8_t)constrain((int)(doc["lv"] | 0), 0, 3);
        a.event = cut(doc["ev"] | "", 40);
        a.area = cut(doc["ar"] | "", 48);
        a.text = cut(doc["tx"] | "", 80);
        a.onset = (int64_t)(doc["on"] | 0LL);
        a.expires = (int64_t)(doc["ex"] | 0LL);
        a.sent = a.onset;
        if (!doc["la"].isNull() && !doc["lo"].isNull()) { a.lat = doc["la"] | 1000.0; a.lon = doc["lo"] | 1000.0; }
        const int64_t t = nowUnix();
        if (t && a.expires && a.expires <= t) { error = "expired"; return false; }
        add(a, doc["n"] | true);                                // n = false: sin ventana (la primera carga de la app)
        return true;
    }

    bool remove(const char* id) {
        if (!items.remove(id ? id : "")) return false;
        ver++;
        return true;
    }

    void clear(const char* src) {
        if (!src || !*src) { items.clear(); unseen.clear(); ver++; return; }
        const plotter::AlertSrc s = plotter::alertSrcFromCode(src);
        items.replaceSource(s, {});
        ver++;
    }

    bool spaceFromJson(JsonVariantConst d) {
        plotter::SpaceWx w;
        w.g = d["g"] | -1;
        w.r = d["r"] | -1;
        w.s = d["s"] | -1;
        w.gMax = d["gm"] | -1;
        JsonArrayConst kp = d["kp"].as<JsonArrayConst>();
        for (JsonVariantConst v : kp) { if (w.kpCount < 8) w.kp[w.kpCount++] = v.as<float>(); }
        w.kpTime = (int64_t)(d["kt"] | 0LL);
        w.sfi = d["sfi"] | -1.0f;
        w.bz = d["bz"] | 1000.0f;
        w.bt = d["bt"] | -1.0f;
        w.windKms = d["ws"] | -1.0f;
        w.xray = cut(d["xr"] | "", 8);
        w.updated = nowUnix();
        if (!w.valid()) return false;
        sw = w;
        swMs = millis() ? millis() : 1;
        ver++;
        return true;
    }

    void onSame(const char* header) {
        if (!header) return;
        if (strcmp(header, "NNNN") == 0) {                      // fin del aviso: nada que quitar (caduca por su duracion)
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Alerts", "SAME end of message");
            return;
        }
        plotter::SameMsg m;
        if (!plotter::sameParse(header, m)) return;
        int64_t t = nowUnix();
        plotter::Alert a = plotter::sameToAlert(m, t ? t : 0);
        if (!t) { a.onset = a.sent = 0; a.expires = 0; }        // sin hora: no caduca hasta que haya hora
        a.event = cut(a.event.c_str(), 40);
        a.text = cut(a.text.c_str(), 80);
        // id corto: la cabecera entera no cabe bien en las ordenes BLE
        char id[20];
        uint32_t h = 2166136261u;
        for (char c : m.raw) { h ^= (uint8_t)c; h *= 16777619u; }
        snprintf(id, sizeof(id), "same/%08lx", (unsigned long)h);
        a.id = id;
        #ifdef HAS_SA868
            const int ch = plotter::nwrChannel(SA868::rxFreqHz() / 1e6);
            if (ch) { char x[48]; snprintf(x, sizeof(x), "WX%d %s", ch, a.text.c_str()); a.text = cut(x, 80); }
        #endif
        add(a, true);
    }

    void loop() {
        const uint32_t ms = millis();
        #ifdef HAS_SA868
            char h[300];
            if (SA868::sameTake(h, sizeof(h))) onSame(h);
        #endif
        if (ms - lastExpireMs > 30000) {
            lastExpireMs = ms;
            const int64_t t = nowUnix();
            if (t) {
                const size_t n = items.size();
                items.expire(t);
                if (items.size() != n) ver++;
            }
            // los no vistos que ya no estan
            for (size_t i = 0; i < unseen.size();) {
                if (!items.find(unseen[i])) unseen.erase(unseen.begin() + (long)i);
                else i++;
            }
            if (closed.size() > 48) closed.erase(closed.begin(), closed.begin() + 16);
        }
        if (buzzing) {
            if (ms - buzzStartMs > 120000UL || unseen.empty()) buzzing = false;     // 2 min o al cerrar
            else if (buzzBeepMs == 0 || ms - buzzBeepMs >= 3000) {
                if (Config.notification.buzzerActive) NOTIFICATION_Utils::alarmBeep();
                buzzBeepMs = ms ? ms : 1;
            }
        }
    }

    const plotter::AlertList& list() { return items; }
    const plotter::SpaceWx& space() { return sw; }
    bool hasSpace() { return swMs != 0 && millis() - swMs < 6UL * 3600000UL; }
    uint32_t spaceAgeS() { return swMs ? (millis() - swMs) / 1000 : 0; }
    uint16_t version() { return ver; }

    const plotter::Alert* popup() {
        const plotter::Alert* best = nullptr;
        for (const auto& id : unseen) {
            const plotter::Alert* a = items.find(id);
            if (a && (!best || a->level > best->level)) best = a;
        }
        return best;
    }

    void popupClose() {
        const plotter::Alert* a = popup();
        if (!a) return;
        closed.push_back(closedKey(*a));
        const std::string id = a->id;
        for (size_t i = 0; i < unseen.size(); i++) if (unseen[i] == id) { unseen.erase(unseen.begin() + (long)i); break; }
        if (unseen.empty()) buzzing = false;
    }

    bool sounding() { return buzzing; }
    void mute() { buzzing = false; }

    uint32_t sameBursts() {
        #ifdef HAS_SA868
            return SA868::sameBursts();
        #else
            return 0;
        #endif
    }
}
