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
#include "sat_config.h"
#include "sat_utils.h"
#include "gps_utils.h"
#include <APRSPacketLib.h>
#include <TinyGPS++.h>

extern logging::Logger  logger;
extern Configuration    Config;
extern Beacon           *currentBeacon;
extern TinyGPSPlus      gps;

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
    static bool     satActive = false;                  // el VFO sigue a un satelite (ver "Satelite" mas abajo)

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
        if (!Config.fm.memMode && !satActive) toMem(cur, Config.fm.vfo);
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
        if (satActive) { satActive = false; Config.fm.satOn = false; }      // sintonizar a mano (o por CAT) deja de seguir al satelite
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
        if (satActive) { satStop(); return; }
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
        if (satActive) { satActive = false; Config.fm.satOn = false; }
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
        if (satActive && editField == ED_FREQ) { satFine(dir * n * 100); return; }   // con un satelite: ajuste fino de 100 Hz por paso
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
        if (satActive) { note("SAT on"); return; }
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
        if (satActive && !arts.running()) { note("SAT on"); return; }
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

    // ---- Satelite -----------------------------------------------------------------------------------------------------

    static bool     satRxOk = false, satTxOk = false, satDopOkFlag = false, satDataLoaded = false;
    static int      satIdx = 0, satFineOff = 0, satDop = 0, satNorad = 0;
    static SatCore::Transmitter satT;
    static long     satTunedRx = 0, satTunedTx = 0, satWantRx = 0, satWantTx = 0;
    static uint32_t satCalcMs = 0, satRetuneMs = 0, satLastTxMs = 0, satNSent = 0, satNHeard = 0;
    static FmCore::Channel beforeSat;

    // banda de aficionados del modulo para emitir (2 m: 144-146 MHz en la Region 1, 144-148 en la 2 y la 3; 70 cm: 430-440, 420-450 en la 2)
    static long     satExtHz = 0;
    static uint32_t satExtMs = 0;

    static bool hamUplink(long hz) {
        if (!SA868::validFreq(hz)) return false;
        const int r = Config.sa868.iaruRegion;
        if (SA868::uhf()) return r == 2 ? (hz >= 420000000L && hz <= 450000000L) : (hz >= 430000000L && hz <= 440000000L);
        return hz >= 144000000L && hz <= (r == 1 ? 146000000L : 148000000L);
    }

    static bool satLoad() {
        if (!satDataLoaded || SAT_Utils::count() == 0) { SAT_Utils::begin(); satDataLoaded = true; }
        return SAT_Utils::block() != nullptr;
    }

    bool satUsable(int i) {
        const SatCore::Block* b = SAT_Utils::block();
        if (!b || i < 0 || i >= b->txCount) return false;
        return SA868::validFreq((long)SatCore::txDownCenter(b->tx[i])) || hamUplink((long)SatCore::txUpCenter(b->tx[i]));
    }

    static bool satCallOk() {
        String c = currentBeacon ? currentBeacon->callsign : String("");
        c.trim(); c.toUpperCase();
        return c.length() >= 3 && !c.startsWith("NOCALL") && !c.startsWith("N0CALL");
    }

    bool satCanTx(char* why, size_t n) {
        if (n) why[0] = 0;
        if (!satActive)               { snprintf(why, n, "No satellite"); return false; }
        if (!satTxOk)                 { snprintf(why, n, "Uplink not in band"); return false; }
        if (!Config.sat.txEnabled)    { snprintf(why, n, "SAT TX off (WebUI)"); return false; }
        if (!satCallOk())             { snprintf(why, n, "Set your callsign"); return false; }
        SatCore::Look lk;
        if (!SAT_Utils::live(lk))     { snprintf(why, n, "No time/position"); return false; }
        if (lk.elDeg < Config.sat.txMinEl) { snprintf(why, n, "El %d < %d", (int)lround(lk.elDeg), Config.sat.txMinEl); return false; }
        return true;
    }

    // el PTT: en el modo SAT solo hacia el satelite; en FM, con un satelite, solo si se puede emitir hacia el
    static bool satPttOk() {
        if (!satActive) {
            if (Config.operatingMode == MODE_SAT) { note("No sat: Listen first"); return false; }
            return true;
        }
        char why[28];
        if (!satCanTx(why, sizeof(why))) { note(why); return false; }
        // subida al dia antes de emitir
        if (satWantTx && satWantTx != satTunedTx && SA868::fmRetune(satTunedRx, satWantTx)) satTunedTx = satWantTx;
        satLastTxMs = millis();
        return true;
    }

    static void satCompute() {
        double rx = 0, tx = 0;
        SAT_Utils::doppler(satT, rx, tx);
        satDopOkFlag = rx > 0 || tx > 0;
        const long dn = (long)SatCore::txDownCenter(satT), up = (long)SatCore::txUpCenter(satT);
        satDop = (rx > 0 && dn) ? (int)lround(rx - (double)dn) : 0;
        const bool ext = satExtMs && millis() - satExtMs < 15000UL && SA868::validFreq(satExtHz);
        if (ext)          satWantRx = satExtHz;
        else if (satRxOk) satWantRx = (rx > 0 ? (long)lround(rx) : dn) + satFineOff;
        else if (satTxOk) satWantRx = tx > 0 ? (long)lround(tx) : up;           // banda cruzada: se oye la subida (otras estaciones)
        satWantTx = satTxOk ? (tx > 0 ? (long)lround(tx) : up) : satWantRx;
    }

    static int satThreshold() { return SA868::firmware() == SA868::Firmware::OPENRTX ? 250 : 500; }

    bool satStart(int txIndex) {
        if (arts.running()) { note("ARTS on"); return false; }
        if (!satLoad())     { note("No satellite data"); return false; }
        const SatCore::Block* b = SAT_Utils::block();
        if (txIndex < 0 || txIndex >= b->txCount) { note("No transmitter"); return false; }
        const SatCore::Transmitter& t = b->tx[txIndex];
        const long dn = (long)SatCore::txDownCenter(t), up = (long)SatCore::txUpCenter(t);
        const bool rxOk = dn > 0 && SA868::validFreq(dn), txOk = up > 0 && hamUplink(up);
        if (!rxOk && !txOk) { note(SA868::uhf() ? "Not in 70cm band" : "Not in 2m band"); return false; }
        scanStop();
        if (!satActive) beforeSat = cur;
        satActive = true; satIdx = txIndex; satT = t; satNorad = b->noradId; satRxOk = rxOk; satTxOk = txOk;
        satTunedRx = satTunedTx = 0; satCalcMs = 0;
        satCompute();
        FmCore::Channel c = cur;
        snprintf(c.name, sizeof(c.name), "%.8s", b->name);
        c.rxHz = satWantRx;
        if (satWantTx != satWantRx) { c.shift = satWantTx > satWantRx ? 1 : -1; c.offsetHz = labs(satWantTx - satWantRx); } else c.shift = 0;
        c.toneMode = t.tone ? FmCore::TONE_ENC : FmCore::TONE_OFF;
        if (t.tone) c.tone = t.tone;
        c.wide = true;                                          // 25 kHz: margen para el Doppler que queda y para el error del oscilador del satelite
        cur = c;
        applyNow();
        satTunedRx = satWantRx; satTunedTx = satWantTx; satRetuneMs = millis();
        if (Config.fm.satTx != txIndex || !Config.fm.satOn) { Config.fm.satTx = txIndex; Config.fm.satOn = true; markDirty(); }
        char f[12]; FmCore::formatFreq(satWantRx, f, sizeof(f));
        char m[24]; snprintf(m, sizeof(m), "SAT %s", f); note(m);
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "FM SAT", "%s tx %d: down %.4f up %.4f MHz%s%s, tone %.1f Hz", b->name, txIndex,
                   dn / 1e6, up / 1e6, rxOk ? "" : " (listening on the uplink)", txOk ? "" : " (no TX: uplink not in band)", t.tone / 10.0);
        return true;
    }

    void satStop() {
        if (!satActive) return;
        satActive = false;
        cur = beforeSat;
        applyNow();
        if (Config.fm.satOn) { Config.fm.satOn = false; markDirty(); }
        note("SAT off");
    }

    bool satNextTx(int dir) {
        if (!satLoad()) return false;
        const SatCore::Block* b = SAT_Utils::block();
        for (int k = 1; k <= b->txCount; k++) {
            const int i = ((satIdx + dir * k) % b->txCount + b->txCount) % b->txCount;
            if (satUsable(i)) return satStart(i);
        }
        note("No other usable TX");
        return false;
    }

    bool     satOn()          { return satActive; }
    int      satTxIndex()     { return satIdx; }
    void     satFine(int d)   { satFineOff = constrain(satFineOff + d, -20000, 20000); satCalcMs = 0; }
    int      satFineHz()      { return satFineOff; }
    long     satRxHz()        { return satActive ? satTunedRx : 0; }
    long     satUpHz()        { return (satActive && satTxOk) ? satWantTx : 0; }
    int      satDopplerHz()   { return satDop; }
    bool     satDopplerOk()   { return satDopOkFlag; }
    bool     satListenUplink(){ return satActive && !satRxOk; }
    bool     satAprs()        { return satActive && SatCore::aprsDigi(satT); }
    uint16_t satTone()        { return satActive && cur.toneMode == FmCore::TONE_ENC ? cur.tone : 0; }
    uint32_t satSent()        { return satNSent; }
    uint32_t satHeard()       { return satNHeard; }
    void     satOnFrame()     { if (satActive) satNHeard++; }
    void     satExternalHz(long hz) { satExtHz = hz; satExtMs = millis() ? millis() : 1; satCalcMs = 0; }
    bool     uplinkInBand(long hz)  { return hamUplink(hz); }

    // cada 500 ms: Doppler; se resintoniza al pasar el umbral (y como mucho una vez por segundo), nunca emitiendo
    static void satTick(uint32_t now) {
        if (!satActive) return;
        const SatCore::Block* b = SAT_Utils::block();
        if (!b || b->noradId != satNorad) { satStop(); return; }            // otro satelite elegido (modo SAT, app)
        if (Config.operatingMode == MODE_FM) SAT_Utils::loop();            // en el modo SAT lo llama la pagina
        if (now - satCalcMs < 500) return;
        satCalcMs = now;
        satCompute();
        if (SA868::fmTransmitting() || now - satRetuneMs < 1000) return;
        const int thr = satThreshold();
        if (labs(satWantRx - satTunedRx) >= thr || labs(satWantTx - satTunedTx) >= thr * 2) {
            if (SA868::fmRetune(satWantRx, satWantTx)) { satTunedRx = satWantRx; satTunedTx = satWantTx; cur = SA868::fmChannel(); }
            satRetuneMs = now;
        }
    }

    // "EA7JRS-7>APLRT1,WIDE1-1:info" -> "EA7JRS-7>APLRT1,ARISS:info"
    static String satPath(const char* tnc2) {
        const char* gt = strchr(tnc2, '>');
        const char* colon = strchr(tnc2, ':');
        if (!gt || !colon || colon < gt) return String(tnc2);
        const char* comma = gt;
        while (comma < colon && *comma != ',') comma++;
        String out(tnc2);
        out = out.substring(0, comma - tnc2);
        String path = Config.sat.aprsPath; path.trim();
        if (path.length()) { out += ','; out += path; }
        out += colon;
        return out;
    }

    static bool satAprsTx(const String& tnc2) {
        char why[28];
        if (!satCanTx(why, sizeof(why))) { note(why); return false; }
        if (satLastTxMs && millis() - satLastTxMs < (uint32_t)Config.sat.txMinIntervalS * 1000UL) { note("SAT: wait"); return false; }
        if (SA868::fmTransmitting() || pttDown) { note("Busy"); return false; }
        if (satWantTx != satTunedTx && SA868::fmRetune(satTunedRx, satWantTx)) satTunedTx = satWantTx;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "FM SAT", "Tx %.4f MHz ---> %s", satTunedTx / 1e6, tnc2.c_str());
        const bool okTx = SA868::transmit(tnc2.c_str());
        satLastTxMs = millis();
        if (okTx) { satNSent++; PAGES_Utils::onTx((int)tnc2.length()); note("Sent via SAT"); }
        return okTx;
    }

    bool satSendAprs(const char* info) {
        if (!satAprs()) { note("Not an APRS sat"); return false; }
        if (!info || !info[0]) { note("No message"); return false; }
        String call = currentBeacon ? currentBeacon->callsign : String("");
        call.trim();
        return satAprsTx(satPath((call + ">APLRT1:" + info).c_str()));
    }

    bool satSendPosition(const char* comment) {
        if (!satAprs()) { note("Not an APRS sat"); return false; }
        double lat, lon;
        if (gps.location.isValid() && gps.location.age() < 10000UL) { lat = gps.location.lat(); lon = gps.location.lng(); }
        else { SatCore::Observer o; if (!SAT_Utils::observer(o)) { note("No position"); return false; } lat = o.latDeg; lon = o.lonDeg; }
        if (!currentBeacon) return false;
        String p = APRSPacketLib::generateBase91GPSBeaconPacket(currentBeacon->callsign, "APLRT1", Config.sat.aprsPath, currentBeacon->overlay,
                       APRSPacketLib::encodeGPSIntoBase91(lat, lon, 0, 0, currentBeacon->symbol, false, 0, false));
        String c = comment && comment[0] ? String(comment) : currentBeacon->comment;
        if (c.length() > 40) c = c.substring(0, 40);                   // corto: el digipetidor de la ISS es muy concurrido
        if (c.length()) p += c;
        return satAprsTx(p);
    }

    // ---- APRS -------------------------------------------------------------------------------------------------------

    static bool satAprsNow() {
        if (!satAprs()) return false;
        char why[28];
        return satCanTx(why, sizeof(why));
    }

    bool aprsTransmit(const char* tnc2) {
        if (SA868::fmTransmitting() || pttDown || eai.running()) {          // se esta hablando: sale al soltar el PTT
            pendingAprs = tnc2;
            return true;
        }
        if (satAprsNow()) {                                                // satelite APRS a la vista: por el satelite (ruta ARISS), si no toca esperar
            if (satLastTxMs && millis() - satLastTxMs < (uint32_t)Config.sat.txMinIntervalS * 1000UL) return true;     // descartada: no saturar el digipetidor
            return satAprsTx(satPath(tnc2));
        }
        if (Config.operatingMode == MODE_SAT) { note("SAT not in view"); return false; }      // en el modo SAT solo por el satelite
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
        // FM: vuelve a seguir al satelite si se dejo asi (en el modo SAT se empieza con "Listen")
        if (Config.operatingMode == MODE_FM && Config.fm.satOn) { if (!satStart(Config.fm.satTx)) { Config.fm.satOn = false; markDirty(); } }
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
                if (!pttLock && !SA868::fmTransmitting() && satPttOk() && SA868::fmVoice(true)) txStartMs = now;
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
        satTick(now);
        if (manDown.enabled()) {
            const uint8_t m = manDown.tick(now);
            if (m == 1) { note("MAN DOWN in 30 s"); displayToggle(true); displayPopup("MAN DOWN", "EAI in 30 s", "Press any key", -1, 1500); }
            if (m == 2) eaiStart();
        }
        if (pendingAprs.length() && !SA868::fmTransmitting() && !pttDown && !eai.running() && !isBusy) {
            const String p = pendingAprs;
            pendingAprs = "";
            aprsTransmit(p.c_str());
        }
        if (applyPending && now - applyAt >= 150 && !SA868::fmTransmitting()) applyNow();
        if (dirty && now - dirtyAt > 5000 && !SA868::fmTransmitting()) { dirty = false; Config.writeFile(); }
    }
}

#endif
