/*
 * Modo SAT, fase 3: emision hacia el satelite con precompensacion Doppler. Ver include/sat_tx.h.
 *
 * Copyright (c) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
 */
#include "sat_config.h"
#include "sat_tx.h"

#ifdef HAS_SAT

#include <Arduino.h>
#include <ctype.h>
#include <logger.h>
#include "configuration.h"
#include "board_pinout.h"
#include "display.h"
#include "lora_utils.h"
#include "power_utils.h"
#include "sat_utils.h"
#include "cw_morse.h"

extern Configuration    Config;
extern logging::Logger  logger;
extern Beacon*          currentBeacon;

namespace SATTX_Utils {

    namespace {

        char     msg[28] = "";
        uint32_t lastMs = 0, sent = 0;
        bool     haveLast = false;
        double   lastFreq = 0.0;
        bool     pinsReady = false;

        const int64_t TX_LO_HZ = 430000000LL;               // la subida tiene que caer en la banda de aficionados de 70 cm (UIT)
        const int64_t TX_HI_HZ = 440000000LL;
        const uint32_t CW_MAX_MS = 25000;                   // una emision de CW no pasa de 25 s
        const uint32_t LORA_MAX_AIR_MS = 10000;

        void setMsg(const char* m) { snprintf(msg, sizeof(msg), "%s", m); }

        int pinOk(int pin) { return (pin >= 0 && !POWER_Utils::pinReserved(pin)) ? pin : -1; }

        void drive(int pin, bool rev, bool active) {
            if (pinOk(pin) < 0) return;
            digitalWrite(pin, (active != rev) ? HIGH : LOW);
        }

        void lna(bool on) { drive(Config.sat.lnaPin, Config.sat.lnaRev, on); }
        void pa(bool on)  { drive(Config.sat.paPin, Config.sat.paRev, on); }
        void ptt(bool on) { drive(Config.sat.pttPin, Config.sat.pttRev, on); }

        void initPins() {
            const int pins[3] = { Config.sat.lnaPin, Config.sat.paPin, Config.sat.pttPin };
            for (int i = 0; i < 3; i++) if (pinOk(pins[i]) >= 0) pinMode(pins[i], OUTPUT);
            lna(true); pa(false); ptt(false);                   // reposo: preamplificador en RX, PA y PTT sueltos
            pinsReady = true;
        }

        // Secuencia de emision: preamplificador cortado y PA / rele -> espera -> PTT -> espera -> RF ... fin de RF -> espera -> PTT y PA sueltos -> espera -> preamplificador
        void seqUp() {
            if (!pinsReady) initPins();
            lna(false);
            pa(true);
            delay(Config.sat.seqLeadMs);
            ptt(true);
            delay(Config.sat.seqPreMs);
        }
        void seqDown() {
            delay(Config.sat.seqPostMs);
            ptt(false);
            pa(false);
            delay(Config.sat.seqTailMs);
            lna(true);
        }

        bool callsignOk(String& call) {
            call = currentBeacon ? currentBeacon->callsign : String("");
            call.trim();
            if (call.length() < 3) return false;
            String up = call; up.toUpperCase();
            return !up.startsWith("NOCALL") && !up.startsWith("N0CALL");
        }

        bool buttonDown() {
            #ifdef BUTTON_PIN
                return digitalRead(BUTTON_PIN) == LOW;
            #else
                return false;
            #endif
        }

        // ---- CW ----
        struct CwCtx { const SatCore::Transmitter* t; uint32_t startMs; bool aborted; uint32_t lastLoop; bool keyed; };

        int64_t upHz(const SatCore::Transmitter& t) {
            double rx = 0, tx = 0;
            SAT_Utils::doppler(t, rx, tx);
            return tx > 0 ? (int64_t)llround(tx) : 0;
        }

        bool stillAllowed(CwCtx* c) {
            if (millis() - c->startMs > CW_MAX_MS) return false;
            if (buttonDown()) return false;
            SatCore::Look lk;
            if (!SAT_Utils::live(lk) || lk.elDeg < Config.sat.txMinEl) return false;
            return true;
        }

        void cwKey(void* ctx, bool on) {
            CwCtx* c = (CwCtx*)ctx;
            if (on) {
                const int64_t hz = upHz(*c->t);
                if (hz < TX_LO_HZ || hz > TX_HI_HZ) { c->aborted = true; return; }
                lastFreq = (double)hz;
                if (!LoRa_Utils::satTxCarrier(true, hz)) c->aborted = true;
                c->keyed = true;
            } else {
                LoRa_Utils::satTxCarrier(false, 0);
                c->keyed = false;
            }
        }

        bool cwWait(void* ctx, uint32_t ms) {
            CwCtx* c = (CwCtx*)ctx;
            if (c->aborted) return false;
            const uint32_t end = millis() + ms;
            while ((int32_t)(end - millis()) > 0) {
                if (millis() - c->lastLoop >= 250) {
                    c->lastLoop = millis();
                    SAT_Utils::loop();                          // mantiene la posicion / Doppler al dia durante una emision larga
                    if (!stillAllowed(c)) { c->aborted = true; return false; }
                }
                delay(1);
            }
            return !c->aborted;
        }

        void cwProgress(void*, size_t, size_t, const char*, uint8_t) {}

    }   // namespace

    void begin() { initPins(); }

    const char* message() { return msg; }
    uint32_t count() { return sent; }
    double lastHz() { return lastFreq; }
    uint32_t sinceLastS() { return haveLast ? (millis() - lastMs) / 1000 : 0xFFFFFFFFUL; }

    SATRX_Utils::Kind classifyUp(const SatCore::Transmitter& t, const char** why) {
        char m[12];
        size_t n = 0;
        for (const char* p = t.mode; *p && n + 1 < sizeof(m); p++) if (isalnum((unsigned char)*p)) m[n++] = (char)tolower((unsigned char)*p);
        m[n] = 0;
        if (why) *why = "";
        const int64_t up = SatCore::txUpCenter(t);
        if (up <= 0) { if (why) *why = "No uplink"; return SATRX_Utils::K_NO; }
        if (up < TX_LO_HZ || up > TX_HI_HZ) { if (why) *why = "Uplink not 430-440"; return SATRX_Utils::K_NO; }
        if (strstr(m, "lrfhss")) { if (why) *why = "LR-FHSS TX n/a"; return SATRX_Utils::K_NO; }
        if (strstr(m, "lora")) return SATRX_Utils::K_LORA;
        if (strncmp(m, "afsk", 4) == 0 || strstr(m, "fm") || strstr(m, "psk") || strstr(m, "ssb") || strstr(m, "usb") || strstr(m, "lsb") ||
            strstr(m, "sstv") || strstr(m, "qam") || strstr(m, "ofdm")) {
            if (why) *why = "Use a radio";
            return SATRX_Utils::K_NO;
        }
        if (strcmp(m, "cw") == 0) return SATRX_Utils::K_CW;
        if (strstr(m, "fsk") || strstr(m, "msk")) {
            if (t.baud < 300 || t.baud > 120000) { if (why) *why = "Baud rate unknown"; return SATRX_Utils::K_NO; }
            return SATRX_Utils::K_FSK;
        }
        if (why) *why = "Mode not supported";
        return SATRX_Utils::K_NO;
    }

    bool check(uint8_t txIdx, char* why, size_t n) {
        why[0] = 0;
        if (!SATRX_Utils::available())  { snprintf(why, n, "Board cannot TX"); return false; }
        if (!Config.sat.txEnabled)      { snprintf(why, n, "TX off (WebUI)"); return false; }
        String call;
        if (!callsignOk(call))          { snprintf(why, n, "Set your callsign"); return false; }
        const SatCore::Block* b = SAT_Utils::block();
        if (!b || txIdx >= b->txCount)  { snprintf(why, n, "No transmitter"); return false; }
        if (SAT_Utils::geo())           { snprintf(why, n, "GEO: no uplink"); return false; }
        const char* r = "";
        if (classifyUp(b->tx[txIdx], &r) == SATRX_Utils::K_NO) { snprintf(why, n, "%s", r[0] ? r : "Not possible"); return false; }
        SatCore::Look lk;
        if (!SAT_Utils::live(lk))       { snprintf(why, n, "No time/position"); return false; }
        if (lk.elDeg < Config.sat.txMinEl) { snprintf(why, n, "El %d < %d", (int)lround(lk.elDeg), Config.sat.txMinEl); return false; }
        const int64_t hz = upHz(b->tx[txIdx]);
        if (hz < TX_LO_HZ || hz > TX_HI_HZ) { snprintf(why, n, "Uplink not 430-440"); return false; }
        if (haveLast && millis() - lastMs < (uint32_t)Config.sat.txMinIntervalS * 1000UL) {
            snprintf(why, n, "Wait %ds", (int)(Config.sat.txMinIntervalS - (millis() - lastMs) / 1000));
            return false;
        }
        return true;
    }

    bool send(uint8_t txIdx, const char* text) {
        char why[28];
        if (!text || !text[0]) { setMsg("No message"); return false; }
        if (!check(txIdx, why, sizeof(why))) { setMsg(why); return false; }

        const SatCore::Transmitter t = SAT_Utils::block()->tx[txIdx];
        const SATRX_Utils::Kind kind = classifyUp(t);
        String call;
        callsignOk(call);
        const bool wasRx = SATRX_Utils::active();
        const uint8_t rxTx = SATRX_Utils::txIndex();
        if (wasRx) SATRX_Utils::stop();                                      // la radio es semiduplex

        int64_t hz = upHz(t);
        SatTxParams p;
        p.kind     = kind == SATRX_Utils::K_CW ? 0 : (kind == SATRX_Utils::K_FSK ? 1 : 2);
        p.hz       = hz;
        p.baud     = t.baud;
        SATRX_Utils::loraDefaults(t, p.sf, p.bwKHz, p.cr);
        p.powerDbm = Config.sat.txPower;

        char hdr[24];
        snprintf(hdr, sizeof(hdr), "<<< SAT TX %s >>>", SATRX_Utils::kindText(kind));
        displayShow(hdr, String(hz / 1e6, 4) + " MHz", text, 0);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SAT Tx", "%s %.4f MHz: %s", SATRX_Utils::kindText(kind), hz / 1e6, text);

        bool good = false;
        if (LoRa_Utils::satTxBegin(p)) {
            seqUp();
            lastFreq = (double)hz;
            if (kind == SATRX_Utils::K_CW) {
                CwCtx c = { &t, millis(), false, millis(), false };
                CWMorse::Hooks hooks = { cwKey, cwWait, cwProgress, &c };
                const uint8_t wpm = (uint8_t)constrain(Config.cw.wpm, (int)CWMorse::WPM_MIN, (int)CWMorse::WPM_MAX);
                good = CWMorse::send(text, wpm, hooks);
                LoRa_Utils::satTxCarrier(false, 0);
                if (!good) setMsg(c.aborted ? "CW stopped" : "CW failed");
            } else if (kind == SATRX_Utils::K_FSK) {
                char tnc2[200];
                snprintf(tnc2, sizeof(tnc2), "%s>APLRT1:%s", call.c_str(), text);
                good = LoRa_Utils::satTxAx25(tnc2) > 0;
                if (!good) setMsg("AX.25 frame invalid");
            } else {
                const size_t len = strlen(text);
                if (LoRa_Utils::satTxLoraAirMs(len) > LORA_MAX_AIR_MS) setMsg("Packet too long in air");
                else {
                    uint32_t air = 0;
                    good = LoRa_Utils::satTxLora((const uint8_t*)text, len, air) == 0;
                    if (!good) setMsg("LoRa TX failed");
                }
            }
            seqDown();
        } else {
            setMsg("Radio refused TX");
        }
        LoRa_Utils::satTxEnd();

        lastMs = millis();
        haveLast = true;
        if (good) { sent++; setMsg("Sent"); }
        if (wasRx) SATRX_Utils::start(rxTx);                                 // vuelve a escuchar
        return good;
    }

}

#else   // !HAS_SAT

namespace SATTX_Utils {
    void begin() {}
    SATRX_Utils::Kind classifyUp(const SatCore::Transmitter&, const char** why) { if (why) *why = ""; return SATRX_Utils::K_NO; }
    bool check(uint8_t, char* why, size_t n) { if (n) why[0] = 0; return false; }
    bool send(uint8_t, const char*) { return false; }
    const char* message() { return ""; }
    uint32_t sinceLastS() { return 0xFFFFFFFFUL; }
    uint32_t count() { return 0; }
    double lastHz() { return 0; }
}

#endif
