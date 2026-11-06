/*
 * Modo SAT (fase 1). Ver include/sat_utils.h
 *
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include "sat_config.h"
#include "sat_utils.h"
#include "rotor_utils.h"

#ifdef HAS_SAT

#include <Arduino.h>
#include <SPIFFS.h>
#include <TinyGPS++.h>
#include <TimeLib.h>
#include <vector>
#include <logger.h>
#include "sat_utils.h"
#include "configuration.h"
#include "gps_utils.h"

extern Configuration    Config;
extern logging::Logger  logger;
extern TinyGPSPlus      gps;

namespace SAT_Utils {

    namespace {

        const char*  FILE_PATH       = "/sats.txt";
        const char*  FILE_TMP        = "/sats.tmp";
        const size_t MAX_FILE_BYTES  = 64000;          // la particion de datos es de 192 kB y durante la descarga conviven el archivo viejo y el nuevo
        const int    MAX_ENTRIES     = 300;

        std::vector<Entry> index;
        int                sel       = -1;
        SatCore::Sat       sat;                                  // satelite activo (~1,3 kB)
        SatCore::Block     blk;
        bool               satReady  = false;
        char               stamp[24] = "";

        // estado en vivo
        SatCore::Observer  obs{ 0, 0, 0 };
        bool               qthOk     = false;
        SatCore::Look      lastLook;
        bool               lookOk    = false;
        uint32_t           lastCalcMs = 0;

        // busqueda de pasos
        SatCore::PassSearch search;
        SatCore::Pass       passFound;
        bool                passValid  = false;
        bool                searching  = false;
        bool                geoSat     = false;
        uint32_t            retryAtMs  = 0;                       // sin paso en el horizonte de busqueda: se vuelve a intentar mas tarde
        double              searchQthLat = 999, searchQthLon = 999;

        // archivo de datos recibido de la app (el tracker no descarga nada: lo hace la app)
        volatile UpdateState updState = UPD_IDLE;
        char                 updMsg[48] = "";

        double nowUnix() { return (double)::now(); }

        bool timeOk() { return year(::now()) >= 2024; }

        // posicion enviada por el movil (app, orden BLE "pos")
        double   phLat = 0, phLon = 0, phAlt = 0;
        float    phAcc = 0;
        uint32_t phAtMs = 0;
        const char* qthSrc = "";
        const uint32_t PHONE_MAX_AGE_MS = 120000UL;                 // una posicion del movil mas vieja ya no vale

        // Posicion de la estacion segun Config.sat.posSource:
        //   0 = GPS del tracker como referencia (si no tiene fix, la del movil)
        //   1 = fusion del tracker y del movil (media ponderada por la inversa de la varianza de cada precision)
        //   2 = solo el movil
        // y, si no hay ninguna: ultima posicion conocida y, por ultimo, la fija de la WebUI.
        bool pickQth() {
            const bool trk = Config.sat.posSource != 2 && gps.location.isValid() && gps.location.age() < 10000UL;
            const bool ph  = Config.sat.posSource != 3 && phAtMs != 0 && millis() - phAtMs < PHONE_MAX_AGE_MS;
            // precision (m) del tracker: HDOP x 5 m, como en un receptor de consumo; 15 m si no hay HDOP
            const double accT = gps.hdop.isValid() ? (gps.hdop.hdop() * 5.0 > 3.0 ? gps.hdop.hdop() * 5.0 : 3.0) : 15.0;
            const double accP = phAcc > 1.0f ? (double)phAcc : 10.0;
            if (trk && ph && Config.sat.posSource == 1) {
                const double wT = 1.0 / (accT * accT), wP = 1.0 / (accP * accP);
                const double tAlt = gps.altitude.isValid() ? gps.altitude.meters() : phAlt;
                obs.latDeg = (gps.location.lat() * wT + phLat * wP) / (wT + wP);
                obs.lonDeg = (gps.location.lng() * wT + phLon * wP) / (wT + wP);
                obs.altM   = (tAlt * wT + phAlt * wP) / (wT + wP);
                qthSrc = "FUS";
                return true;
            }
            if (trk) {
                obs.latDeg = gps.location.lat();
                obs.lonDeg = gps.location.lng();
                obs.altM   = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
                qthSrc = "GPS";
                return true;
            }
            if (ph) {
                obs.latDeg = phLat; obs.lonDeg = phLon; obs.altM = phAlt;
                qthSrc = "PHN";
                return true;
            }
            if (GPS_Utils::hasLastPosition()) {
                obs.latDeg = GPS_Utils::getLastLat();
                obs.lonDeg = GPS_Utils::getLastLng();
                obs.altM   = 0;
                qthSrc = "LAST";
                return true;
            }
            if (Config.sat.lat != 0.0 || Config.sat.lon != 0.0) {          // posicion fija de la WebUI (sin GPS)
                obs.latDeg = Config.sat.lat;
                obs.lonDeg = Config.sat.lon;
                obs.altM   = 0;
                qthSrc = "FIX";
                return true;
            }
            qthSrc = "";
            return false;
        }

        void setMsg(const char* m) { snprintf(updMsg, sizeof(updMsg), "%s", m); }

        // ---- indice del archivo ----
        void buildIndex() {
            index.clear();
            sel = -1;
            satReady = false;
            stamp[0] = '\0';
            if (!SPIFFS.begin(true)) return;
            File f = SPIFFS.open(FILE_PATH, "r");
            if (!f) return;
            index.reserve(MAX_ENTRIES);
            Entry cur;
            bool open = false;
            while (f.available()) {
                const uint32_t pos = f.position();
                String line = f.readStringUntil('\n');
                if (line.length() < 2) continue;
                if (line[0] == '#') {
                    // "# APRS LoRa+ satellites (70 cm) 2026-11-06T03:17Z"
                    int sp = line.lastIndexOf(' ');
                    if (stamp[0] == '\0' && sp > 0 && line.indexOf("satellites") > 0) snprintf(stamp, sizeof(stamp), "%s", line.c_str() + sp + 1);
                    continue;
                }
                if (line[0] == 'S' && line[1] == '|') {
                    if (open) { cur.length = (uint16_t)(pos - cur.offset); index.push_back(cur); }
                    if ((int)index.size() >= MAX_ENTRIES) { open = false; break; }
                    memset(&cur, 0, sizeof(cur));
                    cur.offset = pos;
                    cur.cat = 'U';
                    int p2 = line.indexOf('|', 2);
                    cur.noradId = line.substring(2, p2 > 0 ? p2 : line.length()).toInt();
                    if (p2 > 0) {
                        int p3 = line.indexOf('|', p2 + 1);
                        String n = line.substring(p2 + 1, p3 > 0 ? p3 : line.length());
                        n.trim();
                        snprintf(cur.name, sizeof(cur.name), "%s", n.c_str());
                        if (p3 > 0 && p3 + 1 < (int)line.length() && (line[p3 + 1] == 'L' || line[p3 + 1] == 'G')) cur.cat = line[p3 + 1];
                    }
                    open = true;
                } else if (open && line[0] == 'T' && line[1] == '|') {
                    cur.txCount++;
                }
            }
            if (open) { cur.length = (uint16_t)(f.position() - cur.offset); index.push_back(cur); }
            f.close();
        }

        bool loadBlock(int i) {
            if (i < 0 || i >= (int)index.size()) return false;
            static char buf[1500];
            File f = SPIFFS.open(FILE_PATH, "r");
            if (!f) return false;
            const Entry& e = index[i];
            if (!f.seek(e.offset)) { f.close(); return false; }
            size_t n = e.length < sizeof(buf) - 1 ? e.length : sizeof(buf) - 1;
            n = f.readBytes(buf, n);
            f.close();
            buf[n] = '\0';
            if (SatCore::parseBlock(buf, n, blk) == 0) return false;
            satReady = SatCore::parseTle(blk.name, blk.line1, blk.line2, sat) && sat.noradId == blk.noradId;
            return satReady;
        }

        void restartSearch() {
            passValid = false;
            searching = false;
            lookOk    = false;
            geoSat    = satReady && SatCore::isGeo(sat);
            retryAtMs = 0;
            if (!satReady || !timeOk() || !qthOk) return;
            if (geoSat) return;                                  // geoestacionario: sin pasos, solo posicion fija en el cielo
            SatCore::passSearchStart(search, nowUnix(), 36.0 * 3600.0, (double)Config.sat.minEl);
            searchQthLat = obs.latDeg;
            searchQthLon = obs.lonDeg;
            searching = true;
        }

    }

    // ------------------------------------------------------------------------------------------------ API

    void begin() {
        buildIndex();
        sel = -1;
        for (int i = 0; i < (int)index.size(); i++) if (index[i].noradId == Config.sat.noradId) { sel = i; break; }
        if (sel < 0 && !index.empty()) sel = 0;
        if (sel >= 0) loadBlock(sel);
        qthOk = pickQth();
        restartSearch();
        updState = UPD_IDLE;
        updMsg[0] = '\0';
    }

    bool hasData()  { return !index.empty(); }
    int  count()    { return (int)index.size(); }
    const Entry* at(int i) { return (i >= 0 && i < (int)index.size()) ? &index[i] : nullptr; }
    int  selected() { return sel; }
    const SatCore::Block* block() { return satReady ? &blk : nullptr; }
    bool haveTime() { return timeOk(); }
    const char* qthSource() { return qthSrc; }
    bool observer(SatCore::Observer& out) {
        qthOk = pickQth();
        if (qthOk) out = obs;
        return qthOk;
    }
    void setPhonePosition(double lat, double lon, double alt, float accuracyM) {
        if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return;
        phLat = lat; phLon = lon; phAlt = alt; phAcc = accuracyM;
        phAtMs = millis() ? millis() : 1;
    }
    bool haveQth()  { return qthOk; }
    const char* dataStamp() { return stamp; }

    bool geo() { return geoSat; }

    bool inCategory(const Entry& e) {
        switch (Config.sat.category) {
            case 1:  return e.cat == 'U';
            case 2:  return e.cat == 'L';
            case 3:  return e.cat == 'G';
            default: return true;
        }
    }

    const char* categoryName() {
        switch (Config.sat.category) {
            case 1:  return "70cm";
            case 2:  return "LoRa/TinyGS";
            case 3:  return "GEO/Inmarsat";
            default: return "All";
        }
    }

    int step(int direction) {
        const int n = (int)index.size();
        if (n == 0) return -1;
        int i = sel < 0 ? (direction > 0 ? -1 : 0) : sel;
        for (int k = 0; k < n; k++) {
            i = (i + (direction >= 0 ? 1 : -1) + n) % n;
            if (inCategory(index[i])) return i;
        }
        return -1;
    }

    int nextCategory() {
        Config.sat.category = (Config.sat.category + 1) % 4;
        if (sel < 0 || sel >= (int)index.size() || !inCategory(index[sel])) {
            const int i = step(1);
            if (i >= 0) select(i); else Config.writeFile();
        } else {
            Config.writeFile();
        }
        return Config.sat.category;
    }

    bool select(int i) {
        if (i < 0 || i >= (int)index.size()) return false;
        sel = i;
        Config.sat.noradId = index[i].noradId;
        Config.writeFile();
        loadBlock(i);
        restartSearch();
        return satReady;
    }

    bool live(SatCore::Look& out) {
        if (!lookOk) return false;
        out = lastLook;
        return true;
    }

    bool nextPass(SatCore::Pass& out, bool& isSearching) {
        isSearching = searching;
        if (!passValid) return false;
        out = passFound;
        return true;
    }

    double tleAgeDays() {
        if (!satReady || !timeOk()) return -1.0;
        return SatCore::tleAgeDays(sat, nowUnix());
    }

    void doppler(const SatCore::Transmitter& t, double& rxHz, double& txHz) {
        rxHz = txHz = 0.0;
        if (!lookOk) return;
        const int64_t dn = SatCore::txDownCenter(t), up = SatCore::txUpCenter(t);
        if (dn) rxHz = SatCore::dopplerDown((double)dn, lastLook.rangeRateKmS);
        if (up) txHz = SatCore::dopplerUpPre((double)up, lastLook.rangeRateKmS);
    }

    void loop() {
        const uint32_t ms = millis();
        if (!satReady) return;

        // posicion y hora: se vuelven a mirar cada segundo
        if (ms - lastCalcMs >= 1000) {
            lastCalcMs = ms;
            const bool hadQth = qthOk;
            qthOk = pickQth();
            if (qthOk && timeOk()) {
                lookOk = SatCore::look(sat, obs, nowUnix(), lastLook);
                // si la estacion se ha movido mas de ~0,5 grados los pasos calculados ya no valen
                if (!hadQth || fabs(obs.latDeg - searchQthLat) > 0.5 || fabs(obs.lonDeg - searchQthLon) > 0.5) restartSearch();
                // el paso previsto ya termino: se busca el siguiente
                if (passValid && nowUnix() > passFound.losUnix + 5.0) restartSearch();
                if (!passValid && !searching && !geoSat && (retryAtMs == 0 || (int32_t)(ms - retryAtMs) >= 0)) restartSearch();
            } else {
                lookOk = false;
            }
        }

        // busqueda de pasos: poco trabajo por vuelta para no retrasar el GPS ni la radio
        if (searching) {
            const uint32_t t0 = micros();
            int guard = 0;
            while (searching && micros() - t0 < 6000 && guard++ < 8) {
                const int ph = SatCore::passSearchAdvance(search, sat, obs, 1);
                if (ph == 2) { passFound = search.pass; passValid = true; searching = false; }
                else if (ph >= 3) { passValid = false; searching = false; retryAtMs = ms + 1800000UL; if (retryAtMs == 0) retryAtMs = 1; }
            }
        }
    }

    UpdateState updateState()   { return updState; }
    const char* updateMessage() { return updMsg; }

    // ---- Subida del archivo desde el movil (BLE) ----
    namespace { File upFile; bool upOpen = false; size_t upTotal = 0; }

    void dataAbort() {
        if (upOpen) { upFile.close(); SPIFFS.remove(FILE_TMP); }
        upOpen = false; upTotal = 0;
    }

    bool dataBegin(size_t expectedBytes) {
        if (updState == UPD_WORKING) return false;
        dataAbort();
        if (expectedBytes > MAX_FILE_BYTES || !SPIFFS.begin(true)) return false;
        upFile = SPIFFS.open(FILE_TMP, "w");
        if (!upFile) return false;
        upOpen = true; upTotal = 0;
        return true;
    }

    bool dataChunk(const char* text, size_t n) {
        if (!upOpen) return false;
        if (upTotal + n > MAX_FILE_BYTES || upFile.write((const uint8_t*)text, n) != n) { dataAbort(); return false; }
        upTotal += n;
        return true;
    }

    bool dataEnd() {
        if (!upOpen) return false;
        upFile.close();
        upOpen = false;
        const size_t total = upTotal;
        upTotal = 0;
        if (total < 200) { SPIFFS.remove(FILE_TMP); setMsg("Empty file"); updState = UPD_ERROR; return false; }
        File chk = SPIFFS.open(FILE_TMP, "r");
        bool hasBlock = false;
        while (chk && chk.available() && !hasBlock) { String l = chk.readStringUntil('\n'); hasBlock = l.startsWith("S|"); }
        if (chk) chk.close();
        if (!hasBlock) { SPIFFS.remove(FILE_TMP); setMsg("Unexpected file"); updState = UPD_ERROR; return false; }
        SPIFFS.remove(FILE_PATH);
        if (!SPIFFS.rename(FILE_TMP, FILE_PATH)) { setMsg("Cannot save"); updState = UPD_ERROR; return false; }
        const int keep = sel >= 0 && sel < (int)index.size() ? index[sel].noradId : Config.sat.noradId;
        buildIndex();
        for (int i = 0; i < (int)index.size(); i++) if (index[i].noradId == keep) { sel = i; break; }
        if (sel >= 0) loadBlock(sel);
        restartSearch();
        char m[40]; snprintf(m, sizeof(m), "%d satellites (app)", (int)index.size());
        setMsg(m);
        updState = UPD_OK;
        return true;
    }

}

#else   // sin HAS_SAT: la placa no admite el modo SAT; funciones vacias para que el resto compile igual

namespace SAT_Utils {
    void begin() {}
    void loop() {}
    bool hasData() { return false; }
    int  count() { return 0; }
    const Entry* at(int) { return nullptr; }
    int  selected() { return -1; }
    bool select(int) { return false; }
    bool inCategory(const Entry&) { return false; }
    int  step(int) { return -1; }
    const char* categoryName() { return ""; }
    int  nextCategory() { return 0; }
    bool geo() { return false; }
    const SatCore::Block* block() { return nullptr; }
    bool haveTime() { return false; }
    bool haveQth() { return false; }
    bool observer(SatCore::Observer&) { return false; }
    const char* qthSource() { return ""; }
    void setPhonePosition(double, double, double, float) {}
    bool live(SatCore::Look&) { return false; }
    bool nextPass(SatCore::Pass&, bool& searching) { searching = false; return false; }
    double tleAgeDays() { return -1.0; }
    const char* dataStamp() { return ""; }
    void doppler(const SatCore::Transmitter&, double& rxHz, double& txHz) { rxHz = txHz = 0.0; }
    bool dataBegin(size_t) { return false; }
    bool dataChunk(const char*, size_t) { return false; }
    bool dataEnd() { return false; }
    void dataAbort() {}
    UpdateState updateState() { return UPD_IDLE; }
    const char* updateMessage() { return ""; }
}

#endif  // HAS_SAT
