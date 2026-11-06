/*
 * Modo SAT, fase 2: recepcion de satelites con correccion Doppler continua. Ver include/sat_rx.h.
 *
 * Una tarea de FreeRTOS es la unica que toca la radio mientras se escucha (el CW necesita muestrear el RSSI cada milisegundo);
 * el bucle principal calcula el Doppler (SAT_Utils) y le deja la frecuencia pedida.
 *
 * Copyright (c) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
 */
#include "sat_config.h"
#include "sat_rx.h"

#ifdef HAS_SAT

#include <Arduino.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <logger.h>
#include "configuration.h"
#include "lora_utils.h"
#include "sat_utils.h"
#include "cw_morse.h"
#include "notification_utils.h"

namespace SATRX_Utils {

    namespace {

        const int LOG_LINES = 8;
        const int LINE_LEN  = 100;

        struct LogEntry { char text[LINE_LEN]; int16_t rssi; };

        // ---- estado compartido entre el bucle principal y la tarea (protegido con `mux`) ----
        portMUX_TYPE   mux = portMUX_INITIALIZER_UNLOCKED;
        SemaphoreHandle_t radioMutex = nullptr;      // el que tenga la radio (tarea en cada vuelta; el bucle al abrir / cerrar)
        TaskHandle_t   task = nullptr;
        volatile bool  running = false;
        Kind           curKind = K_NONE;
        uint8_t        curTx = 0;
        int            curSat = -1;                  // satelite con el que se arranco (si cambia, se para)
        SatCore::Transmitter curT;
        int64_t        nominalHz = 0;
        int64_t        wantHz = 0, tunedHz = 0;      // pedida por el Doppler / la que tiene la radio
        int            dopHz = 0;
        bool           dopOk = false;
        int            fineOffset = 0;
        volatile bool  forceRetune = false;
        int            retuneThrHz = 500;
        uint32_t       lastRetuneMs = 0, lastCalcMs = 0;
        float          lastRssi = -150.0f;
        uint32_t       nFrames = 0, seq = 0;
        char           msg[28] = "";
        LogEntry       logBuf[LOG_LINES];
        int            logHead = 0, logN = 0;        // logBuf[logHead-1] es la mas reciente
        char           cwBuf[48] = "";               // texto CW en curso
        size_t         cwLen = 0;
        uint32_t       cwLastCharMs = 0;
        // LoRa
        uint8_t        loraSf = 10, loraCr = 5;
        float          loraBw = 125.0f;
        uint8_t        presetIdx = 0;
        SatRxParams    params;

        struct Preset { uint8_t sf; float bw; };
        const Preset PRESETS[] = { {12,125.0f}, {11,125.0f}, {10,125.0f}, {9,125.0f}, {8,125.0f}, {7,125.0f}, {10,250.0f}, {9,250.0f}, {8,500.0f}, {7,500.0f}, {9,62.5f}, {7,62.5f} };
        const int NPRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

        void setMsg(const char* m) { portENTER_CRITICAL(&mux); snprintf(msg, sizeof(msg), "%s", m); portEXIT_CRITICAL(&mux); }

        void pushLog(const char* text, int rssi) {
            portENTER_CRITICAL(&mux);
            LogEntry& e = logBuf[logHead];
            snprintf(e.text, sizeof(e.text), "%s", text);
            e.rssi = (int16_t)rssi;
            logHead = (logHead + 1) % LOG_LINES;
            if (logN < LOG_LINES) logN++;
            nFrames++;
            seq++;
            portEXIT_CRITICAL(&mux);
        }

        // Texto imprimible: lo que no lo es se muestra como '.'; si casi todo es binario, volcado hexadecimal
        void printable(const uint8_t* d, size_t n, char* out, size_t outMax) {
            size_t bad = 0;
            for (size_t i = 0; i < n; i++) if (d[i] < 32 || d[i] > 126) bad++;
            size_t o = 0;
            if (n && bad * 2 > n) {
                static const char* hx = "0123456789ABCDEF";
                if (o + 3 < outMax) { out[o++] = 'x'; out[o++] = ':'; }
                for (size_t i = 0; i < n && o + 3 < outMax; i++) { out[o++] = hx[d[i] >> 4]; out[o++] = hx[d[i] & 15]; }
            } else {
                for (size_t i = 0; i < n && o + 1 < outMax; i++) out[o++] = (d[i] >= 32 && d[i] <= 126) ? (char)d[i] : '.';
            }
            out[o] = 0;
        }

        void parseLora(const SatCore::Transmitter& t) {
            loraSf = 10; loraCr = 5; loraBw = 125.0f; presetIdx = 2;
            if (t.baud >= 7800 && t.baud <= 500000) loraBw = t.baud / 1000.0f;       // en los datos de SatNOGS el campo "baud" de LoRa suele ser el ancho de banda (Hz)
            char d[sizeof(t.desc) + 1];
            snprintf(d, sizeof(d), "%s", t.desc);
            for (char* p = d; *p; p++) *p = (char)toupper((unsigned char)*p);
            const char* p = strstr(d, "SF");
            if (p) { p += 2; while (*p == ' ' || *p == ':' || *p == '=') p++; const int v = atoi(p); if (v >= 5 && v <= 12) loraSf = (uint8_t)v; }
            p = strstr(d, "BW");
            if (p) {
                p += 2; while (*p == ' ' || *p == ':' || *p == '=') p++;
                const double v = atof(p);
                if (v >= 7.0 && v <= 500.0) loraBw = (float)v;
                else if (v >= 7800.0 && v <= 500000.0) loraBw = (float)(v / 1000.0);
            }
            p = strstr(d, "CR");
            if (p) { p += 2; while (*p == ' ' || *p == ':' || *p == '=' || *p == '4' || *p == '/') p++; const int v = atoi(p); if (v >= 5 && v <= 8) loraCr = (uint8_t)v; }
            // el ancho de banda admitido por el SX126x: 7.8 10.4 15.6 20.8 31.25 41.7 62.5 125 250 500
            static const float ok[] = { 7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f };
            float best = ok[7]; float bd = 1e9f;
            for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) { const float dd = fabsf(ok[i] - loraBw); if (dd < bd) { bd = dd; best = ok[i]; } }
            loraBw = best;
        }

        // ---- tarea de la radio ----
        CWMorse::Envelope   env;
        CWMorse::Decoder    dec;
        bool                keyed = false;
        uint32_t            lastRssiMs = 0, cwSeenMs = 0;

        void cwCommit(bool force) {
            if (cwLen == 0) return;
            if (!force && cwLen < 3) return;
            char line[LINE_LEN];
            snprintf(line, sizeof(line), "CW: %s", cwBuf);
            pushLog(line, (int)lastRssi);
            portENTER_CRITICAL(&mux);
            cwLen = 0; cwBuf[0] = 0;
            portEXIT_CRITICAL(&mux);
        }

        void cwAdd(char c) {
            portENTER_CRITICAL(&mux);
            if (cwLen + 1 < sizeof(cwBuf)) { cwBuf[cwLen++] = c; cwBuf[cwLen] = 0; }
            seq++;
            portEXIT_CRITICAL(&mux);
            cwLastCharMs = millis();
            if (c == ' ' ? cwLen >= 24 : cwLen >= 40) cwCommit(true);       // linea llena: se pasa al registro
        }

        void cwStep(uint32_t now) {
            const float level = LoRa_Utils::satRxLevel();
            lastRssi = level;
            const bool k = env.update(level);
            if (k != keyed) {
                keyed = k;
                if (keyed) dec.keyDown(now); else dec.keyUp(now);
                if (Config.cw.sidetone && Config.notification.buzzerActive) NOTIFICATION_Utils::sidetone(keyed);
                NOTIFICATION_Utils::lightRx(keyed);
                if (keyed) cwSeenMs = now;
            }
            const char c = dec.tick(now);
            if (c) cwAdd(c);
            if (!keyed && cwLen && now - cwLastCharMs > 6000) cwCommit(true);        // silencio: lo recibido hasta ahora queda en el registro
        }

        void packetStep(uint32_t now) {
            uint8_t buf[256];
            size_t len; int rssi; float snr; int ferr;
            const int r = LoRa_Utils::satRxPoll(buf, sizeof(buf), len, rssi, snr, ferr);
            if (now - lastRssiMs >= 250) { lastRssiMs = now; lastRssi = LoRa_Utils::satRxLevel(); }
            if (r == 0) return;
            lastRssi = (float)rssi;
            char line[LINE_LEN];
            if (r == 1) {                                   // trama AX.25 UI en TNC2
                printable(buf, len, line, sizeof(line));
            } else if (r == 3) {                            // trama AX.25 que no es UI: hexadecimal
                snprintf(line, sizeof(line), "AX25 x:%.90s", (const char*)buf);
            } else {                                        // paquete LoRa
                char body[LINE_LEN];
                printable(buf, len, body, sizeof(body));
                snprintf(line, sizeof(line), "LoRa %ddB: %.80s", (int)lroundf(snr), body);
            }
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SAT Rx", "---> %s", line);
            pushLog(line, rssi);
        }

        void rxTask(void*) {
            for (;;) {
                if (!running) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
                xSemaphoreTake(radioMutex, portMAX_DELAY);
                if (running) {
                    const uint32_t now = millis();
                    int64_t want;
                    portENTER_CRITICAL(&mux);
                    want = wantHz;
                    portEXIT_CRITICAL(&mux);
                    if (want != tunedHz && now - lastRetuneMs >= 1000) {
                        if (LoRa_Utils::satRxTune(want)) tunedHz = want;
                        lastRetuneMs = now;
                    }
                    if (curKind == K_CW) cwStep(now); else packetStep(now);
                }
                xSemaphoreGive(radioMutex);
                vTaskDelay(curKind == K_CW ? 1 : 5);
            }
        }

        int thresholdFor(Kind k, int baud) {
            if (k == K_CW) return 400;
            if (k == K_LORA) return (int)(loraBw * 1000.0f / 10.0f);       // el LoRa tolera hasta ~1/4 del ancho de banda
            return max(300, baud * 2 / 3);                                 // FSK: una fraccion del ancho de banda del filtro
        }

    }   // namespace

    const char* kindText(Kind k) {
        switch (k) {
            case K_CW:   return "CW";
            case K_FSK:  return "FSK";
            case K_LORA: return "LoRa";
            case K_NO:   return "n/a";
            default:     return "-";
        }
    }

    bool available() { return LoRa_Utils::satRxAvailable(); }

    Kind classify(const SatCore::Transmitter& t, const char** why) {
        char m[12];
        size_t n = 0;
        for (const char* p = t.mode; *p && n + 1 < sizeof(m); p++) if (isalnum((unsigned char)*p)) m[n++] = (char)tolower((unsigned char)*p);
        m[n] = 0;
        const int64_t dn = SatCore::txDownCenter(t);
        if (why) *why = "";
        if (dn <= 0) { if (why) *why = "No downlink"; return K_NO; }
        if (dn < 400000000LL || dn > 510000000LL) { if (why) *why = "Out of radio range"; return K_NO; }       // SX1268: 410-510 MHz (el SX1262 llega a 960 pero estos satelites son de 70 cm)
        if (strstr(m, "lrfhss")) { if (why) *why = "LR-FHSS is TX only"; return K_NO; }
        if (strstr(m, "lora")) return K_LORA;
        if (strncmp(m, "afsk", 4) == 0 || strstr(m, "fm") || strstr(m, "psk") || strstr(m, "ssb") || strstr(m, "usb") || strstr(m, "lsb") ||
            strstr(m, "sstv") || strstr(m, "lrpt") || strstr(m, "apt") || strstr(m, "qam") || strstr(m, "dvb") || strstr(m, "cdma") || strstr(m, "ofdm") || strstr(m, "dstar")) {
            if (why) *why = "Use an SDR / radio";
            return K_NO;
        }
        if (strcmp(m, "cw") == 0) return K_CW;
        if (strstr(m, "fsk") || strstr(m, "msk")) {
            if (t.baud < 300 || t.baud > 120000) { if (why) *why = "Baud rate unknown"; return K_NO; }
            return K_FSK;
        }
        if (why) *why = "Mode not supported";
        return K_NO;
    }

    bool active() { return running; }
    Kind kind() { return running ? curKind : K_NONE; }
    uint8_t txIndex() { return curTx; }
    const char* message() { return msg; }

    double rxHz() { portENTER_CRITICAL(&mux); const int64_t v = tunedHz; portEXIT_CRITICAL(&mux); return (double)v; }
    int dopplerHz() { return dopHz; }
    bool dopplerOk() { return dopOk; }
    float rssi() { return lastRssi; }
    uint32_t frames() { return nFrames; }
    int fineHz() { return fineOffset; }
    uint32_t eventSeq() { return seq; }

    void fine(int deltaHz) { fineOffset = constrain(fineOffset + deltaHz, -20000, 20000); lastCalcMs = 0; forceRetune = true; }

    void loraText(char* out, size_t n) { snprintf(out, n, "SF%u BW%g CR4/%u", (unsigned)loraSf, (double)loraBw, (unsigned)loraCr); }

    static bool beginRadio() {
        params.kind  = curKind == K_CW ? 0 : (curKind == K_FSK ? 1 : 2);
        params.hz    = wantHz;
        params.baud  = curT.baud;
        params.sf    = loraSf;
        params.bwKHz = loraBw;
        params.cr    = loraCr;
        return LoRa_Utils::satRxBegin(params);
    }

    void nextLoraPreset() {
        if (curKind != K_LORA) return;
        presetIdx = (uint8_t)((presetIdx + 1) % NPRESETS);
        loraSf = PRESETS[presetIdx].sf;
        loraBw = PRESETS[presetIdx].bw;
        if (!running) return;
        xSemaphoreTake(radioMutex, portMAX_DELAY);
        retuneThrHz = thresholdFor(K_LORA, 0);
        tunedHz = wantHz;
        if (!beginRadio()) { running = false; setMsg("LoRa restart failed"); }
        xSemaphoreGive(radioMutex);
    }

    bool start(uint8_t txIdx) {
        if (running) stop();
        const SatCore::Block* b = SAT_Utils::block();
        if (!available())                 { setMsg("Board cannot receive"); return false; }
        if (!b || txIdx >= b->txCount)    { setMsg("No transmitter"); return false; }
        const char* why = "";
        const Kind k = classify(b->tx[txIdx], &why);
        if (k == K_NO)                    { setMsg(why[0] ? why : "Not receivable"); return false; }
        if (!radioMutex) radioMutex = xSemaphoreCreateMutex();
        if (!radioMutex)                  { setMsg("Out of memory"); return false; }
        if (!task && xTaskCreate(rxTask, "satrx", 6144, nullptr, 2, &task) != pdPASS) { task = nullptr; setMsg("Out of memory"); return false; }

        curT = b->tx[txIdx];
        curKind = k;
        curTx = txIdx;
        curSat = SAT_Utils::selected();
        nominalHz = SatCore::txDownCenter(curT);
        double rx = 0, tx = 0;
        SAT_Utils::doppler(curT, rx, tx);
        dopOk = rx > 0;
        wantHz = (int64_t)llround(dopOk ? rx : (double)nominalHz) + fineOffset;
        tunedHz = wantHz;
        dopHz = dopOk ? (int)llround(rx - (double)nominalHz) : 0;
        if (k == K_LORA) parseLora(curT);
        retuneThrHz = thresholdFor(k, curT.baud);
        env.begin();
        dec.begin((uint8_t)constrain(Config.cw.wpm, (int)CWMorse::WPM_MIN, (int)CWMorse::WPM_MAX), true);
        keyed = false;
        cwLen = 0; cwBuf[0] = 0;
        lastRetuneMs = millis(); lastRssiMs = 0; lastCalcMs = 0;
        lastRssi = -150.0f;

        xSemaphoreTake(radioMutex, portMAX_DELAY);
        const bool ok = beginRadio();
        if (ok) running = true;
        xSemaphoreGive(radioMutex);
        if (!ok) { setMsg("Freq/params refused"); return false; }
        setMsg("Listening");
        return true;
    }

    void stop() {
        if (!running) return;
        running = false;
        xSemaphoreTake(radioMutex, portMAX_DELAY);           // la tarea ya no toca la radio
        cwCommit(true);
        LoRa_Utils::satRxEnd();
        xSemaphoreGive(radioMutex);
        NOTIFICATION_Utils::sidetone(false);
        NOTIFICATION_Utils::lightRx(false);
        curKind = K_NONE;
        setMsg("Stopped");
    }

    void loop() {
        if (!running) return;
        if (Config.operatingMode != MODE_SAT || SAT_Utils::selected() != curSat) { stop(); return; }
        const uint32_t now = millis();
        if (now - lastCalcMs < 500) return;
        lastCalcMs = now;
        double rx = 0, tx = 0;
        SAT_Utils::doppler(curT, rx, tx);
        const bool ok = rx > 0;
        const int64_t want = (int64_t)llround(ok ? rx : (double)nominalHz) + fineOffset;
        portENTER_CRITICAL(&mux);
        dopOk = ok;
        dopHz = ok ? (int)llround(rx - (double)nominalHz) : 0;
        if (forceRetune || llabs((long long)(want - tunedHz)) >= retuneThrHz) { wantHz = want; forceRetune = false; }
        portEXIT_CRITICAL(&mux);
    }

    void clearLog() {
        portENTER_CRITICAL(&mux);
        logN = 0; logHead = 0; cwLen = 0; cwBuf[0] = 0; nFrames = 0; seq++;
        portEXIT_CRITICAL(&mux);
    }

    size_t logCount() { return (size_t)logN; }

    bool logLine(size_t newestFirst, char* out, size_t n, int* rssiOut) {
        bool ok = false;
        portENTER_CRITICAL(&mux);
        if ((int)newestFirst < logN) {
            const LogEntry& e = logBuf[(logHead - 1 - (int)newestFirst + 2 * LOG_LINES) % LOG_LINES];
            snprintf(out, n, "%s", e.text);
            if (rssiOut) *rssiOut = e.rssi;
            ok = true;
        }
        portEXIT_CRITICAL(&mux);
        return ok;
    }

    void cwLine(char* out, size_t n) {
        portENTER_CRITICAL(&mux);
        const char* s = cwBuf;
        const size_t l = cwLen;
        if (l > n - 1) s += l - (n - 1);
        snprintf(out, n, "%s", s);
        portEXIT_CRITICAL(&mux);
    }

}

#else   // !HAS_SAT

namespace SATRX_Utils {
    Kind classify(const SatCore::Transmitter&, const char** why) { if (why) *why = ""; return K_NO; }
    const char* kindText(Kind) { return "-"; }
    bool available() { return false; }
    bool start(uint8_t) { return false; }
    void stop() {}
    bool active() { return false; }
    void loop() {}
    const char* message() { return ""; }
    Kind kind() { return K_NONE; }
    uint8_t txIndex() { return 0; }
    double rxHz() { return 0; }
    int dopplerHz() { return 0; }
    bool dopplerOk() { return false; }
    float rssi() { return -150.0f; }
    uint32_t frames() { return 0; }
    void fine(int) {}
    int fineHz() { return 0; }
    void nextLoraPreset() {}
    void loraText(char* out, size_t n) { if (n) out[0] = 0; }
    void clearLog() {}
    size_t logCount() { return 0; }
    bool logLine(size_t, char*, size_t, int*) { return false; }
    void cwLine(char* out, size_t n) { if (n) out[0] = 0; }
    uint32_t eventSeq() { return 0; }
}

#endif
