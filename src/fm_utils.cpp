/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Modo FM (voz) de la T-TWR. Ver include/fm_utils.h
 * Encoder: tabla de estados de paso completo de Ben Buxton (libreria Rotary, GPL-3.0; la que usa el ejemplo de fabrica de LilyGO).
 */

#ifdef HAS_SA868

#include <Arduino.h>
#include <logger.h>
#include "configuration.h"
#include "board_pinout.h"
#include "sa868_radio.h"
#include "fm_utils.h"
#include "display.h"
#include "pages_utils.h"

extern logging::Logger  logger;
extern Configuration    Config;
extern Beacon           *currentBeacon;

namespace FM_Utils {

    static FmCore::Channel cur;
    static FmCore::Scanner scanner;
    static FmCore::Arts    arts;
    static FmCore::Eai     eai;
    static FmCore::ManDown manDown;
    static FmCore::Channel beforeArts;
    static uint8_t  editField = ED_FREQ;
    static bool     started = false, pttDown = false, pttLock = false, applyPending = false, dirty = false, lastBusy = false;
    static uint32_t pttEdgeMs = 0, txStartMs = 0, applyAt = 0, dirtyAt = 0, noticeAt = 0;
    static char     noticeText[24] = "";
    static String   pendingAprs;

    // ---- Encoder (interrupciones) ------------------------------------------------------------------------------------

    static volatile int encCount = 0;
    static volatile uint8_t encState = 0;
    static const uint8_t ENC_TABLE[7][4] = {
        { 0x0, 0x2, 0x4, 0x0 }, { 0x3, 0x0, 0x1, 0x10 }, { 0x3, 0x2, 0x0, 0x0 }, { 0x3, 0x2, 0x1, 0x0 },
        { 0x6, 0x0, 0x4, 0x0 }, { 0x6, 0x5, 0x0, 0x20 }, { 0x6, 0x5, 0x4, 0x0 } };

    static void IRAM_ATTR encIsr() {
        #if defined(TTWR_ENCODER_A) && defined(TTWR_ENCODER_B)
        const uint8_t pins = (uint8_t)((digitalRead(TTWR_ENCODER_B) << 1) | digitalRead(TTWR_ENCODER_A));
        encState = ENC_TABLE[encState & 0x0F][pins];
        if (encState & 0x10) encCount = encCount + 1;
        else if (encState & 0x20) encCount = encCount - 1;
        #endif
    }

    void encoderBegin() {
        static bool done = false;
        if (done) return;
        done = true;
        #if defined(TTWR_ENCODER_A) && defined(TTWR_ENCODER_B)
        pinMode(TTWR_ENCODER_A, INPUT_PULLUP);
        pinMode(TTWR_ENCODER_B, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(TTWR_ENCODER_A), encIsr, CHANGE);
        attachInterrupt(digitalPinToInterrupt(TTWR_ENCODER_B), encIsr, CHANGE);
        #endif
    }

    int takeEncoder() {
        noInterrupts();
        const int n = encCount;
        encCount = 0;
        interrupts();
        return n;
    }

    // ---- Configuracion <-> canal ------------------------------------------------------------------------------------

    static FmCore::Channel fromMem(const FmMemory& m) {
        FmCore::Channel c;
        FmCore::clearChannel(c, m.rxHz);
        snprintf(c.name, sizeof(c.name), "%s", m.name.c_str());
        c.shift = (int8_t)constrain(m.shift, -1, 1);
        c.offsetHz = m.offsetHz;
        c.toneMode = (uint8_t)constrain(m.toneMode, 0, FmCore::TONE_MODES - 1);
        c.tone = FmCore::ctcssIndex((uint16_t)m.tone) >= 0 ? (uint16_t)m.tone : 885;
        c.dcs = FmCore::validDcs((uint16_t)m.dcs) ? (uint16_t)m.dcs : 23;
        c.dcsInv = m.dcsInv; c.wide = m.wide; c.high = m.high;
        return c;
    }

    static void toMem(const FmCore::Channel& c, FmMemory& m) {
        m.name = c.name; m.rxHz = (int)c.rxHz; m.shift = c.shift; m.offsetHz = (int)c.offsetHz; m.toneMode = c.toneMode;
        m.tone = c.tone; m.dcs = c.dcs; m.dcsInv = c.dcsInv; m.wide = c.wide; m.high = c.high;
    }

    static long bandLo() { return SA868::uhf() ? 400000000L : 134000000L; }
    static long bandHi() { return SA868::uhf() ? 480000000L : 174000000L; }
    // banda de aficionado de la region para escanear la banda (o los limites de la WebUI)
    static long scanLo() {
        if (SA868::validFreq(Config.fm.scanLoHz)) return Config.fm.scanLoHz;
        if (SA868::uhf()) return Config.sa868.iaruRegion == 2 ? 420000000L : 430000000L;
        return 144000000L;
    }
    static long scanHi() {
        if (SA868::validFreq(Config.fm.scanHiHz) && Config.fm.scanHiHz > scanLo()) return Config.fm.scanHiHz;
        if (SA868::uhf()) return Config.sa868.iaruRegion == 2 ? 450000000L : 440000000L;
        return Config.sa868.iaruRegion == 1 ? 146000000L : 148000000L;
    }

    static void note(const char* text) {
        snprintf(noticeText, sizeof(noticeText), "%s", text);
        noticeAt = millis();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "FM", "%s", text);
    }

    const char* notice() { return (noticeText[0] && millis() - noticeAt < 4000) ? noticeText : ""; }

    static void markDirty() { dirty = true; dirtyAt = millis(); }

    static void requestApply() { applyPending = true; applyAt = millis(); }

    static bool applyNow() {
        applyPending = false;
        const bool good = SA868::fmApply(cur, Config.sa868.squelch);
        if (good && !SA868::fmLastToneOk()) note(cur.toneMode == FmCore::TONE_DCS ? "No DCS (OpenRTX)" : "Tone not in NiceRF");
        return good;
    }

    // el VFO se guarda en la configuracion (unos segundos despues del ultimo cambio, para no gastar la flash)
    static void vfoChanged() {
        if (!Config.fm.memMode) toMem(cur, Config.fm.vfo);
        Config.fm.vfo.name = "";
        markDirty();
        requestApply();
    }

    // ---- Estado -----------------------------------------------------------------------------------------------------

    const FmCore::Channel& channel() { return cur; }
    bool     memMode()        { return Config.fm.memMode; }
    int      memIndex()       { return Config.fm.memIndex; }
    bool     memUsed(int i)   { return i >= 0 && i < FmConfig::MEMORIES && SA868::validFreq(Config.fm.mem[i].rxHz); }
    String   memName(int i)   { return memUsed(i) ? Config.fm.mem[i].name : String(""); }
    long     stepHz()         { return FmCore::STEPS[constrain(Config.fm.stepIndex, 0, FmCore::STEP_COUNT - 1)]; }
    uint8_t  edit()           { return editField; }
    void     setEdit(uint8_t e) { editField = e < ED_COUNT ? e : ED_FREQ; }
    bool     ptt()            { return SA868::fmVoiceActive(); }
    bool     transmitting()   { return SA868::fmTransmitting(); }
    bool     busy()           { return SA868::fmBusy(); }
    int      rssi()           { return SA868::fmRssi(); }
    uint32_t txSeconds()      { return ptt() ? (millis() - txStartMs) / 1000 : 0; }
    int      squelch()        { return SA868::fmSquelchLevel(); }
    bool     scanning()       { return scanner.running(); }
    bool     scanPaused()     { return scanner.paused(); }
    bool     artsOn()         { return arts.running(); }
    uint8_t  artsState()      { return arts.state; }
    bool     eaiOn()          { return eai.running(); }
    uint32_t eaiRemainingS()  { return eai.remainingS(millis()); }

    void activity() { manDown.activity(millis()); }

    // ---- Canal ------------------------------------------------------------------------------------------------------

    bool setChannel(const FmCore::Channel& c) {
        if (!SA868::validFreq(c.rxHz) || !SA868::validFreq(FmCore::txHz(c))) { note("Out of band"); return false; }
        if (arts.running()) { note("ARTS on"); return false; }
        cur = c;
        cur.name[0] = 0;
        Config.fm.memMode = false;
        vfoChanged();
        return true;
    }

    bool setVfoHz(long hz) {
        FmCore::Channel c = Config.fm.memMode ? cur : fromMem(Config.fm.vfo);
        c.rxHz = hz;
        int8_t sh; long off;
        if (FmCore::autoRepeater(hz, Config.sa868.iaruRegion, sh, off)) { c.shift = sh; c.offsetHz = off; } else c.shift = 0;
        return setChannel(c);
    }

    void toggleVfoMem() {
        if (arts.running()) { note("ARTS on"); return; }
        if (Config.fm.memMode) {
            Config.fm.memMode = false;
            cur = fromMem(Config.fm.vfo);
            note("VFO");
        } else {
            int i = Config.fm.memIndex;
            if (!memUsed(i)) for (i = 0; i < FmConfig::MEMORIES && !memUsed(i); i++) {}
            if (i >= FmConfig::MEMORIES) { note("No memories"); return; }
            Config.fm.memMode = true; Config.fm.memIndex = i;
            cur = fromMem(Config.fm.mem[i]);
            note("MEMORY");
        }
        markDirty();
        requestApply();
    }

    bool recall(int i) {
        if (!memUsed(i)) { note("Empty memory"); return false; }
        if (arts.running()) { note("ARTS on"); return false; }
        Config.fm.memMode = true; Config.fm.memIndex = i;
        cur = fromMem(Config.fm.mem[i]);
        markDirty();
        requestApply();
        return true;
    }

    bool store(int i, const char* name) {
        if (i < 0 || i >= FmConfig::MEMORIES) return false;
        FmCore::Channel c = cur;
        snprintf(c.name, sizeof(c.name), "%s", (name && name[0]) ? name : (cur.name[0] ? cur.name : ""));
        if (!c.name[0]) { char f[12]; FmCore::formatFreq(c.rxHz, f, sizeof(f)); snprintf(c.name, sizeof(c.name), "%.8s", f); }
        toMem(c, Config.fm.mem[i]);
        Config.writeFile();
        char t[24]; snprintf(t, sizeof(t), "Stored M%02d", i + 1); note(t);
        return true;
    }

    bool erase(int i) {
        if (i < 0 || i >= FmConfig::MEMORIES) return false;
        Config.fm.mem[i].rxHz = 0; Config.fm.mem[i].name = "";
        if (Config.fm.memMode && Config.fm.memIndex == i) { Config.fm.memMode = false; cur = fromMem(Config.fm.vfo); requestApply(); }
        Config.writeFile();
        return true;
    }

    static int nextMem(int from, int dir) {
        for (int k = 1; k <= FmConfig::MEMORIES; k++) {
            const int i = ((from + dir * k) % FmConfig::MEMORIES + FmConfig::MEMORIES) % FmConfig::MEMORIES;
            if (memUsed(i)) return i;
        }
        return -1;
    }

    void memStep(int dir) {
        const int i = nextMem(Config.fm.memIndex, dir >= 0 ? 1 : -1);
        if (i >= 0) recall(i);
    }

    void cycleStep() {
        Config.fm.stepIndex = (Config.fm.stepIndex + 1) % FmCore::STEP_COUNT;
        markDirty();
    }

    // en memoria, cambiar un ajuste pasa el canal al VFO (como en las emisoras)
    static void editing() { if (Config.fm.memMode) { Config.fm.memMode = false; cur.name[0] = 0; } }

    void cycleShift()   { if (arts.running()) return; editing(); cur.shift = (int8_t)(cur.shift == 0 ? -1 : (cur.shift < 0 ? 1 : 0)); vfoChanged(); }
    void cycleToneMode(){ if (arts.running()) return; editing(); cur.toneMode = (uint8_t)((cur.toneMode + 1) % FmCore::TONE_MODES); vfoChanged(); }
    void togglePower()  { cur.high = !cur.high; editing(); vfoChanged(); }
    void toggleWide()   { cur.wide = !cur.wide; editing(); vfoChanged(); }

    void toneStep(int dir) {
        if (arts.running()) return;
        editing();
        if (cur.toneMode == FmCore::TONE_DCS) cur.dcs = FmCore::nextDcs(cur.dcs, dir);
        else { if (cur.toneMode == FmCore::TONE_OFF || cur.toneMode == FmCore::TONE_1750) cur.toneMode = FmCore::TONE_ENC; cur.tone = FmCore::nextCtcss(cur.tone, dir); }
        vfoChanged();
    }

    void offsetStep(int dir) {
        editing();
        long o = cur.offsetHz + (dir >= 0 ? 100000L : -100000L);
        cur.offsetHz = constrain(o, 0L, 99900000L);
        if (cur.shift == 0) cur.shift = -1;
        vfoChanged();
    }

    void squelchStep(int dir) {
        Config.sa868.squelch = constrain(Config.sa868.squelch + (dir >= 0 ? 1 : -1), 0, 8);
        if (Config.fm.squelchAuto) { Config.fm.squelchAuto = false; note("Squelch manual"); }
        markDirty();
        requestApply();
    }

    void toggleAutoSquelch() {
        Config.fm.squelchAuto = !Config.fm.squelchAuto;
        note(Config.fm.squelchAuto ? "Squelch auto" : "Squelch manual");
        Config.writeFile();                                 // el camino del altavoz se decide al arrancar (Rev2.1)
        requestApply();
    }

    void cycleNoiseReduction() {
        Config.fm.nrLevel = (Config.fm.nrLevel + 1) % 4;
        static const char* const N[4] = { "NR off (restart)", "NR low (restart)", "NR mid (restart)", "NR high (restart)" };
        note(N[Config.fm.nrLevel]);
        Config.writeFile();                                 // se aplica al arrancar el modo
    }

    bool qsy(const FmCore::Qsy& q) {
        FmCore::Channel c;
        FmCore::qsyToChannel(q, Config.sa868.iaruRegion, c);
        c.high = cur.high;
        if (!setChannel(c)) return false;
        char t[24], f[12]; FmCore::formatFreq(c.rxHz, f, sizeof(f));
        snprintf(t, sizeof(t), "QSY %s", f); note(t);
        return true;
    }

    // ---- Encoder ----------------------------------------------------------------------------------------------------

    void dial(int clicks) {
        if (clicks == 0) return;
        activity();
        const int dir = clicks > 0 ? 1 : -1;
        const int n = abs(clicks);
        if (scanner.running()) { scanner.dir = (int8_t)dir; return; }          // durante el escaneo: cambia el sentido
        for (int k = 0; k < n; k++) {
            switch (editField) {
                case ED_TONE:    toneStep(dir); break;
                case ED_OFFSET:  offsetStep(dir); break;
                case ED_SQUELCH: squelchStep(dir); break;
                default:
                    if (Config.fm.memMode) { memStep(dir); break; }
                    if (arts.running()) { note("ARTS on"); return; }
                    {
                        const long hz = FmCore::tune(cur.rxHz, dir, stepHz(), bandLo(), bandHi());
                        cur.rxHz = hz;
                        int8_t sh; long off;                // ARS: desplazamiento automatico en los segmentos de repetidores
                        if (FmCore::autoRepeater(hz, Config.sa868.iaruRegion, sh, off)) { cur.shift = sh; cur.offsetHz = off; } else cur.shift = 0;
                        vfoChanged();
                    }
                    break;
            }
        }
    }

    // ---- Escaneo, ARTS, EAI -----------------------------------------------------------------------------------------

    void scanStart() {
        if (arts.running()) { note("ARTS on"); return; }
        if (Config.fm.scanMemories && nextMem(Config.fm.memIndex, 1) < 0) { note("No memories"); return; }
        scanner.start(millis(), (uint8_t)Config.fm.scanResume);
        note(Config.fm.scanMemories ? "Scan memories" : "Scan band");
    }

    void scanStop() { if (scanner.running()) { scanner.stop(); note("Scan stopped"); markDirty(); } }

    static void scanAdvance() {
        if (Config.fm.scanMemories) {
            const int i = nextMem(Config.fm.memIndex, scanner.dir);
            if (i < 0) { scanner.stop(); return; }
            Config.fm.memMode = true; Config.fm.memIndex = i;
            cur = fromMem(Config.fm.mem[i]);
        } else {
            Config.fm.memMode = false;
            cur.rxHz = FmCore::tune(cur.rxHz, scanner.dir, stepHz(), scanLo(), scanHi());
            int8_t sh; long off;
            if (FmCore::autoRepeater(cur.rxHz, Config.sa868.iaruRegion, sh, off)) { cur.shift = sh; cur.offsetHz = off; } else cur.shift = 0;
            toMem(cur, Config.fm.vfo);
        }
        applyNow();
        scanner.tuned(millis());
    }

    void artsToggle() {
        if (arts.running()) {
            arts.stop();
            cur = beforeArts;
            applyNow();
            note("ARTS off");
            return;
        }
        scanStop();
        beforeArts = cur;
        // ARTS: el mismo DCS para emitir y recibir (con OpenRTX, que no programa el DCS, TSQL de 100 Hz: solo entre T-TWR)
        cur.toneMode = FmCore::TONE_DCS; cur.dcs = (uint16_t)Config.fm.artsDcs;
        if (!SA868::fmToneSupported(cur)) { cur.toneMode = FmCore::TONE_TSQL; cur.tone = 1000; }
        applyNow();
        arts.start(millis(), Config.fm.artsInterval, Config.fm.cwId);
        note("ARTS on");
    }

    void eaiStart() {
        if (eai.running()) return;
        scanStop();
        eai.start(millis(), (uint8_t)Config.fm.eaiMode, Config.fm.eaiMinutes);
        note("EAI ACTIVE");
        displayToggle(true);
        if (Config.fm.aprsBeacon && !PAGES_Utils::emergencyActive()) PAGES_Utils::onEmergencyGesture();   // y baliza SOS por APRS
    }

    void eaiStop() {
        if (!eai.running()) return;
        if (eai.micOn) SA868::fmVoice(false);
        eai.stop();
        note("EAI stopped");
    }

    void toggleAprsBeacon() {
        Config.fm.aprsBeacon = !Config.fm.aprsBeacon;
        note(Config.fm.aprsBeacon ? "APRS beacon on" : "APRS beacon off");
        markDirty();
    }

    // ---- APRS -------------------------------------------------------------------------------------------------------

    bool aprsTransmit(const char* tnc2) {
        if (SA868::fmTransmitting() || pttDown || eai.running()) {          // se esta hablando: sale al soltar el PTT
            pendingAprs = tnc2;
            return true;
        }
        return SA868::fmAprsTransmit(tnc2);
    }

    size_t qsySpec(char* out, size_t outMax) {
        if (!Config.fm.qsyInBeacon || !started) return 0;
        return FmCore::formatQsy(cur, Config.sa868.iaruRegion, out, outMax);
    }

    // ---- Arranque y bucle -------------------------------------------------------------------------------------------

    void begin() {
        if (!SA868::validFreq(Config.fm.vfo.rxHz)) {
            Config.fm.vfo.rxHz = SA868::uhf() ? 433500000 : 145500000;     // llamada simplex de la Region 1
            Config.fm.vfo.offsetHz = (int)FmCore::standardOffset(Config.fm.vfo.rxHz, Config.sa868.iaruRegion);
            Config.fm.vfo.shift = 0;
        }
        if (Config.fm.memMode && memUsed(Config.fm.memIndex)) cur = fromMem(Config.fm.mem[Config.fm.memIndex]);
        else { Config.fm.memMode = false; cur = fromMem(Config.fm.vfo); }
        applyNow();
        #ifdef BUTTON2_PIN
        pinMode(BUTTON2_PIN, INPUT_PULLUP);                 // PTT lateral
        #endif
        encoderBegin();
        manDown.configure(Config.fm.manDownMin);
        manDown.activity(millis());
        started = true;
        char f[12]; FmCore::formatFreq(cur.rxHz, f, sizeof(f));
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "FM", "Voice mode on %s MHz (%s), squelch %s", f, Config.fm.memMode ? "memory" : "VFO",
                   Config.fm.squelchAuto ? "auto" : "manual");
    }

    static void handlePtt(uint32_t now) {
        #ifdef BUTTON2_PIN
        const bool down = digitalRead(BUTTON2_PIN) == LOW;
        if (down != pttDown && now - pttEdgeMs > 30) {
            pttDown = down; pttEdgeMs = now;
            activity();
            if (down) {
                if (scanner.running()) scanStop();
                if (!pttLock && !SA868::fmTransmitting() && SA868::fmVoice(true)) txStartMs = now;
            } else {
                pttLock = false;
                if (SA868::fmVoiceActive() && !eai.micOn) {
                    SA868::fmVoice(false);
                    if (arts.running()) arts.polled(now);   // la emision lleva el DCS: cuenta como sondeo
                }
            }
        }
        if (SA868::fmVoiceActive() && !eai.micOn && Config.fm.totSec > 0 && now - txStartMs > (uint32_t)Config.fm.totSec * 1000UL) {
            SA868::fmVoice(false);                          // tiempo maximo de emision: hasta soltar el PTT
            pttLock = true;
            note("TOT: PTT released");
        }
        #endif
    }

    static void handleEai(uint32_t now) {
        switch (eai.tick(now)) {
            case FmCore::EA_CWID:    SA868::fmCwId(currentBeacon ? currentBeacon->callsign.c_str() : "", Config.fm.cwWpm); break;
            case FmCore::EA_BEEP:    { static const uint16_t hz[1] = { 1000 }, ms[1] = { 500 }; SA868::fmPlayTones(hz, ms, 1); break; }
            case FmCore::EA_MIC_ON:  SA868::fmVoice(true); break;
            case FmCore::EA_MIC_OFF: SA868::fmVoice(false); break;
            case FmCore::EA_END:     note("EAI ended"); break;
            default: break;
        }
    }

    static void handleArts(uint32_t now, bool isBusy) {
        if (!arts.running()) return;
        if (isBusy && !lastBusy) arts.heard(now);
        if (!SA868::fmTransmitting() && !isBusy) {
            if (arts.wantCwId(now)) { SA868::fmCwId(currentBeacon ? currentBeacon->callsign.c_str() : "", Config.fm.cwWpm); arts.cwIdSent(now); }
            else if (arts.wantPoll(now, false)) {
                static const uint16_t hz[1] = { 0 }, ms[1] = { 1000 };      // un segundo de portadora con el DCS
                SA868::fmPlayTones(hz, ms, 1);
                arts.polled(millis());
            }
        }
        if (arts.update(now)) {
            const bool in = arts.state == FmCore::ARTS_IN;
            note(in ? "ARTS: IN RANGE" : "ARTS: OUT RANGE");
            displayToggle(true);
            displayPopup("ARTS", in ? "IN RANGE" : "OUT RANGE", "", -1, 1200);
        }
    }

    void loop() {
        if (!started) return;
        const uint32_t now = millis();
        SA868::fmPoll();
        #ifdef BUTTON_PIN
        if (digitalRead(BUTTON_PIN) == LOW) activity();     // cualquier pulsacion: no es un "hombre caido"
        #endif
        handlePtt(now);
        const bool isBusy = SA868::fmBusy();
        if (eai.running()) handleEai(now);
        else {
            if (scanner.running() && !SA868::fmTransmitting() && scanner.tick(now, isBusy)) scanAdvance();
            handleArts(now, isBusy);
        }
        lastBusy = isBusy;
        if (manDown.enabled()) {
            const uint8_t m = manDown.tick(now);
            if (m == 1) { note("MAN DOWN in 30 s"); displayToggle(true); displayPopup("MAN DOWN", "EAI in 30 s", "Press any key", -1, 1500); }
            if (m == 2) eaiStart();
        }
        if (pendingAprs.length() && !SA868::fmTransmitting() && !pttDown && !eai.running() && !isBusy) {
            const String p = pendingAprs;
            pendingAprs = "";
            SA868::fmAprsTransmit(p.c_str());
        }
        if (applyPending && now - applyAt >= 150 && !SA868::fmTransmitting()) applyNow();
        if (dirty && now - dirtyAt > 5000 && !SA868::fmTransmitting()) { dirty = false; Config.writeFile(); }
    }
}

#endif
