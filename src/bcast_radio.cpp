/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Radio comercial por I2C. Ver include/bcast_radio.h
 * Registros: hojas de datos del RDA5807M (RDA Microelectronics, v1.1), del Si4702/03 (Silicon Labs, AN230 / Rev. 1.1) y guia de
 * programacion del Si47xx (AN332).
 */

#include <Arduino.h>
#include <Wire.h>
#include <logger.h>
#include "configuration.h"
#include "board_pinout.h"
#include "bcast_radio.h"
#include "rds.h"

extern logging::Logger  logger;
extern Configuration    Config;

namespace BCAST {

    static Chip     kind = CHIP_NONE;
    static uint8_t  addr = 0;
    static bool     on = false, mute = false, mono = false, busySeek = false;
    static uint8_t  curBand = BAND_FM;
    static uint32_t curKHz = 0, seekStart = 0;
    static int      vol = 8, sigRssi = 0, sigSnr = 0;
    static bool     sigStereo = false;
    static RdsDecoder rds;
    static uint32_t lastPoll = 0, saveAt = 0;

    // ---- I2C --------------------------------------------------------------------------------------------------------

    static bool present(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

    // RDA5807M, acceso por registro (0x11)
    static bool rdaWrite(uint8_t reg, uint16_t v) {
        Wire.beginTransmission(0x11); Wire.write(reg); Wire.write((uint8_t)(v >> 8)); Wire.write((uint8_t)v);
        return Wire.endTransmission() == 0;
    }
    static uint16_t rdaRead(uint8_t reg) {
        Wire.beginTransmission(0x11); Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return 0;
        if (Wire.requestFrom((uint8_t)0x11, (uint8_t)2) != 2) return 0;
        const uint16_t h = Wire.read();
        return (uint16_t)((h << 8) | Wire.read());
    }

    // Si4703: se lee siempre desde el registro 0x0A (y da la vuelta) y se escribe desde el 0x02
    static uint16_t siReg[16];
    static bool si4703ReadAll() {
        if (Wire.requestFrom((uint8_t)0x10, (uint8_t)32) != 32) return false;
        for (int k = 0; k < 16; k++) { const uint8_t r = (uint8_t)((0x0A + k) & 0x0F); const uint16_t h = Wire.read(); siReg[r] = (uint16_t)((h << 8) | Wire.read()); }
        return true;
    }
    static bool si4703Write(uint8_t upTo) {                 // escribe 0x02 .. upTo
        Wire.beginTransmission(0x10);
        for (uint8_t r = 0x02; r <= upTo; r++) { Wire.write((uint8_t)(siReg[r] >> 8)); Wire.write((uint8_t)siReg[r]); }
        return Wire.endTransmission() == 0;
    }

    // Si4735: ordenes con espera de CTS
    static bool si47Cmd(const uint8_t* cmd, size_t n, uint8_t* resp = nullptr, size_t rn = 0, uint32_t waitMs = 300) {
        uint32_t t0 = millis();
        for (;;) {                                          // CTS antes de mandar
            if (Wire.requestFrom(addr, (uint8_t)1) == 1 && (Wire.read() & 0x80)) break;
            if (millis() - t0 > waitMs) return false;
            delay(1);
        }
        Wire.beginTransmission(addr); Wire.write(cmd, n);
        if (Wire.endTransmission() != 0) return false;
        t0 = millis();
        for (;;) {
            const uint8_t want = (uint8_t)(rn ? rn : 1);
            if (Wire.requestFrom(addr, want) == want) {
                uint8_t b[16]; for (uint8_t k = 0; k < want; k++) b[k] = Wire.read();
                if (b[0] & 0x80) { if (resp) memcpy(resp, b, rn); return true; }
            }
            if (millis() - t0 > waitMs) return false;
            delay(1);
        }
    }
    static bool si47Prop(uint16_t prop, uint16_t value) {
        const uint8_t c[6] = { 0x12, 0x00, (uint8_t)(prop >> 8), (uint8_t)prop, (uint8_t)(value >> 8), (uint8_t)value };
        return si47Cmd(c, 6);
    }

    // ---- Bandas -----------------------------------------------------------------------------------------------------

    static uint32_t bandLo(uint8_t b) {
        switch (b) { case BAND_MW: return 520; case BAND_SW: return 2300; case BAND_LW: return 153; default: return Config.bcast.fmWide ? 76000 : 87500; }
    }
    static uint32_t bandHi(uint8_t b) {
        switch (b) { case BAND_MW: return 1710; case BAND_SW: return 26100; case BAND_LW: return 279; default: return 108000; }
    }
    static uint32_t bandStep(uint8_t b) {
        switch (b) { case BAND_MW: return Config.bcast.mw10k ? 10 : 9; case BAND_SW: return 5; case BAND_LW: return 9; default: return 100; }
    }

    bool bandAvailable(uint8_t b) { return b == BAND_FM || (kind == CHIP_SI4735 && b < BAND_COUNT); }

    const char* bandName(uint8_t b) {
        static const char* const N[BAND_COUNT] = { "FM", "MW", "SW", "LW" };
        return b < BAND_COUNT ? N[b] : "?";
    }

    static void rememberFreq() {
        switch (curBand) {
            case BAND_MW: Config.bcast.mwKHz = (int)curKHz; break;
            case BAND_SW: Config.bcast.swKHz = (int)curKHz; break;
            case BAND_LW: Config.bcast.lwKHz = (int)curKHz; break;
            default:      Config.bcast.fmKHz = (int)curKHz; break;
        }
        Config.bcast.band = curBand;
        saveAt = millis() + 5000;                           // se guarda unos segundos despues del ultimo cambio
    }

    // ---- Encendido por chip -----------------------------------------------------------------------------------------

    static void hardReset(bool sdioLow) {
        const int rst = Config.bcast.resetPin;
        if (rst < 0) return;
        pinMode(rst, OUTPUT);
        #ifdef OLED_SDA
        if (sdioLow) { Wire.end(); pinMode(OLED_SDA, OUTPUT); digitalWrite(OLED_SDA, LOW); }   // Si4703: SDIO bajo = modo de 2 hilos
        #else
        (void)sdioLow;
        #endif
        digitalWrite(rst, LOW); delay(10);
        digitalWrite(rst, HIGH); delay(10);
        #ifdef OLED_SDA
        if (sdioLow) Wire.begin(OLED_SDA, OLED_SCL);
        #endif
    }

    static void applyVolume() {
        const int v = mute ? 0 : constrain(vol, 0, 15);
        switch (kind) {
            case CHIP_RDA5807: rdaWrite(0x05, (uint16_t)(0x8880 | v)); rdaWrite(0x02, (uint16_t)((mute ? 0x8000 : 0xC000) | (mono ? 0x2000 : 0) | 0x000D)); break;
            case CHIP_SI4703:
                siReg[0x05] = (uint16_t)((siReg[0x05] & 0xFFF0) | v);
                siReg[0x02] = (uint16_t)((siReg[0x02] & ~0x6000) | (mute ? 0 : 0x4000) | (mono ? 0x2000 : 0));
                si4703Write(0x05);
                break;
            case CHIP_SI4735: si47Prop(0x4000, (uint16_t)(v * 63 / 15)); si47Prop(0x4001, mute ? 3 : 0); break;
            default: break;
        }
    }

    static bool si47PowerUp(uint8_t b) {
        const uint8_t down[1] = { 0x11 };
        si47Cmd(down, 1);
        const uint8_t up[3] = { 0x01, (uint8_t)(0x10 | (b == BAND_FM ? 0x00 : 0x01)), 0x05 };     // cristal, FM o AM, audio analogico
        if (!si47Cmd(up, 3, nullptr, 0, 1500)) return false;
        delay(500);                                         // arranque del oscilador de cristal
        if (b == BAND_FM) {
            si47Prop(0x1100, Config.bcast.deemph75 ? 2 : 1);                    // desenfasis 75 / 50 us
            si47Prop(0x1400, (uint16_t)(bandLo(b) / 10)); si47Prop(0x1401, (uint16_t)(bandHi(b) / 10)); si47Prop(0x1402, 10);
            si47Prop(0x1502, 0xAA01);                       // RDS activo, se aceptan bloques con errores corregibles
            si47Prop(0x1500, 0x0001);
            si47Prop(0x1800, mono ? 127 : 0x0031);          // FM_BLEND_STEREO_THRESHOLD: mono = umbral alto
        } else {
            si47Prop(0x3400, (uint16_t)bandLo(b)); si47Prop(0x3401, (uint16_t)bandHi(b)); si47Prop(0x3402, (uint16_t)bandStep(b));
        }
        return true;
    }

    static bool chipOn() {
        switch (kind) {
            case CHIP_RDA5807:
                rdaWrite(0x02, 0x0002); delay(50);              // reinicio por software
                rdaWrite(0x02, 0xC00D); delay(100);             // salida activa, sin silencio, RDS, metodo nuevo, encendido
                rdaWrite(0x04, (uint16_t)(Config.bcast.deemph75 ? 0x0000 : 0x0800));   // desenfasis 50 us (Europa)
                return true;
            case CHIP_SI4703:
                si4703ReadAll();
                siReg[0x07] = 0x8100; si4703Write(0x07); delay(500);        // oscilador de cristal
                si4703ReadAll();
                siReg[0x02] = 0x4001; si4703Write(0x02); delay(110);        // encendido sin silencio
                si4703ReadAll();
                siReg[0x04] = (uint16_t)(siReg[0x04] | 0x1000 | (Config.bcast.deemph75 ? 0 : 0x0800));   // RDS, desenfasis
                siReg[0x05] = (uint16_t)((siReg[0x05] & 0xFF00) | 0x0010 | (Config.bcast.fmWide ? 0x0040 : 0x0000));   // 100 kHz; banda
                si4703Write(0x05);
                return true;
            case CHIP_SI4735: return si47PowerUp(curBand);
            default: return false;
        }
    }

    // ---- Sintonia ---------------------------------------------------------------------------------------------------

    bool tune(uint32_t khz) {
        if (kind == CHIP_NONE || !on) return false;
        khz = constrain(khz, bandLo(curBand), bandHi(curBand));
        const uint32_t st = bandStep(curBand);
        khz = bandLo(curBand) + ((khz - bandLo(curBand) + st / 2) / st) * st;
        bool good = false;
        switch (kind) {
            case CHIP_RDA5807: {
                // banda 00 (87-108) o 10 (76-108), paso 100 kHz: CHAN = (f - inicio) / 100
                const uint32_t base = Config.bcast.fmWide ? 76000 : 87000;
                const uint16_t chan = (uint16_t)((khz - base) / 100);
                good = rdaWrite(0x03, (uint16_t)((chan << 6) | 0x0010 | (Config.bcast.fmWide ? 0x0008 : 0x0000)));
                break;
            }
            case CHIP_SI4703: {
                const uint32_t base = Config.bcast.fmWide ? 76000 : 87500;
                si4703ReadAll();
                siReg[0x03] = (uint16_t)(0x8000 | ((khz - base) / 100));
                good = si4703Write(0x03);
                const uint32_t t0 = millis();
                while (millis() - t0 < 300) { delay(10); si4703ReadAll(); if (siReg[0x0A] & 0x4000) break; }
                siReg[0x03] &= 0x7FFF; si4703Write(0x03);
                break;
            }
            case CHIP_SI4735: {
                if (curBand == BAND_FM) {
                    const uint16_t f = (uint16_t)(khz / 10);
                    const uint8_t c[5] = { 0x20, 0x00, (uint8_t)(f >> 8), (uint8_t)f, 0x00 };
                    good = si47Cmd(c, 5);
                } else {
                    const uint16_t f = (uint16_t)khz;
                    const uint8_t c[6] = { 0x40, 0x00, (uint8_t)(f >> 8), (uint8_t)f, 0x00, 0x00 };
                    good = si47Cmd(c, 6);
                }
                break;
            }
            default: break;
        }
        if (good) { curKHz = khz; rds.reset(); rememberFreq(); }
        return good;
    }

    void step(int clicks) {
        if (!on || clicks == 0 || busySeek) return;
        const long st = (long)bandStep(curBand);
        long f = (long)curKHz + clicks * st;
        if (f > (long)bandHi(curBand)) f = (long)bandLo(curBand);
        if (f < (long)bandLo(curBand)) f = (long)bandHi(curBand);
        tune((uint32_t)f);
    }

    void seek(int dir) {
        if (!on || kind == CHIP_NONE) return;
        rds.reset();
        busySeek = true; seekStart = millis();
        switch (kind) {
            case CHIP_RDA5807: rdaWrite(0x02, (uint16_t)(0xC00D | (dir > 0 ? 0x0200 : 0) | 0x0100 | (mono ? 0x2000 : 0))); break;
            case CHIP_SI4703:
                si4703ReadAll();
                siReg[0x02] = (uint16_t)((siReg[0x02] & ~0x0600) | 0x0100 | (dir > 0 ? 0x0200 : 0));     // SEEK, SEEKUP, con vuelta
                si4703Write(0x02);
                break;
            case CHIP_SI4735: {
                const uint8_t c[6] = { (uint8_t)(curBand == BAND_FM ? 0x21 : 0x41), (uint8_t)((dir > 0 ? 0x08 : 0x00) | 0x04), 0, 0, 0, 0 };
                si47Cmd(c, curBand == BAND_FM ? 2 : 6);
                break;
            }
            default: busySeek = false; break;
        }
    }

    bool setBand(uint8_t b) {
        if (!bandAvailable(b)) return false;
        curBand = b;
        if (kind == CHIP_SI4735 && on) si47PowerUp(b);
        uint32_t f;
        switch (b) { case BAND_MW: f = Config.bcast.mwKHz; break; case BAND_SW: f = Config.bcast.swKHz; break; case BAND_LW: f = Config.bcast.lwKHz; break; default: f = Config.bcast.fmKHz; break; }
        if (f < bandLo(b) || f > bandHi(b)) f = bandLo(b);
        applyVolume();
        return tune(f);
    }

    // ---- Estado -----------------------------------------------------------------------------------------------------

    static void pollRda() {
        const uint16_t r0a = rdaRead(0x0A), r0b = rdaRead(0x0B);
        sigStereo = (r0a & 0x0400) != 0;
        sigRssi = (r0b >> 9) & 0x7F;
        if (busySeek) {
            if ((r0a & 0x4000) || millis() - seekStart > 5000) {           // STC: fin de la busqueda
                busySeek = false;
                rdaWrite(0x02, (uint16_t)(0xC00D | (mono ? 0x2000 : 0)));
                const uint32_t base = Config.bcast.fmWide ? 76000 : 87000;
                curKHz = base + (r0a & 0x03FF) * 100;
                rememberFreq();
            }
            return;
        }
        if (r0a & 0x8000) {                                 // RDS listo
            const uint16_t a = rdaRead(0x0C), b = rdaRead(0x0D), c = rdaRead(0x0E), d = rdaRead(0x0F);
            const uint8_t blerA = (r0b >> 2) & 3, blerB = r0b & 3;
            rds.feed(a, b, c, d, (uint8_t)((blerA == 3 ? 1 : 0) | (blerB == 3 ? 2 : 0)));
        }
    }

    static void pollSi4703() {
        si4703ReadAll();
        sigStereo = (siReg[0x0A] & 0x0100) != 0;
        sigRssi = siReg[0x0A] & 0xFF;
        if (busySeek) {
            if ((siReg[0x0A] & 0x4000) || millis() - seekStart > 5000) {
                busySeek = false;
                siReg[0x02] &= (uint16_t)~0x0100; si4703Write(0x02);
                const uint32_t base = Config.bcast.fmWide ? 76000 : 87500;
                curKHz = base + (siReg[0x0B] & 0x03FF) * 100;
                rememberFreq();
            }
            return;
        }
        if (siReg[0x0A] & 0x8000) {
            const uint8_t ea = (siReg[0x0A] >> 9) & 3, eb = (siReg[0x0B] >> 14) & 3, ec = (siReg[0x0B] >> 12) & 3, ed = (siReg[0x0B] >> 10) & 3;
            rds.feed(siReg[0x0C], siReg[0x0D], siReg[0x0E], siReg[0x0F],
                     (uint8_t)((ea == 3 ? 1 : 0) | (eb == 3 ? 2 : 0) | (ec == 3 ? 4 : 0) | (ed == 3 ? 8 : 0)));
        }
    }

    static void pollSi4735() {
        uint8_t r[16];
        const bool fm = curBand == BAND_FM;
        if (busySeek) {
            const uint8_t st[1] = { 0x14 };
            if (si47Cmd(st, 1, r, 1) && (r[0] & 0x01)) {                     // STC
                const uint8_t ts[2] = { (uint8_t)(fm ? 0x22 : 0x42), 0x01 };
                if (si47Cmd(ts, 2, r, 8)) { curKHz = fm ? (uint32_t)((r[2] << 8) | r[3]) * 10 : (uint32_t)((r[2] << 8) | r[3]); rememberFreq(); }
                busySeek = false;
            } else if (millis() - seekStart > 8000) busySeek = false;
            return;
        }
        const uint8_t q[2] = { (uint8_t)(fm ? 0x23 : 0x43), 0x00 };
        if (si47Cmd(q, 2, r, 8)) { sigRssi = r[4]; sigSnr = r[5]; sigStereo = fm && (r[3] & 0x80); }
        if (!fm) return;
        for (int k = 0; k < 4; k++) {                       // vacia la cola de grupos RDS
            const uint8_t s[2] = { 0x24, 0x01 };
            if (!si47Cmd(s, 2, r, 13) || r[3] == 0) break;
            const uint8_t ble = r[12];
            rds.feed((uint16_t)((r[4] << 8) | r[5]), (uint16_t)((r[6] << 8) | r[7]), (uint16_t)((r[8] << 8) | r[9]), (uint16_t)((r[10] << 8) | r[11]),
                     (uint8_t)((((ble >> 6) & 3) == 3 ? 1 : 0) | (((ble >> 4) & 3) == 3 ? 2 : 0) | (((ble >> 2) & 3) == 3 ? 4 : 0) | ((ble & 3) == 3 ? 8 : 0)));
        }
    }

    // ---- Arranque y bucle -------------------------------------------------------------------------------------------

    bool begin() {
        if (!Config.bcast.enabled) return false;
        hardReset(false);
        // Si4735: 0x63 (SEN alto) o 0x11 (SEN bajo); RDA5807M: 0x11 (registro 0x00 = 0x58xx); Si4703: 0x10 tras el reinicio en modo I2C
        if (present(0x63)) { kind = CHIP_SI4735; addr = 0x63; }
        else if (present(0x11)) {
            const uint16_t id = rdaRead(0x00);
            if ((id & 0xFF00) == 0x5800) { kind = CHIP_RDA5807; addr = 0x11; }
            else { kind = CHIP_SI4735; addr = 0x11; }
        } else {
            hardReset(true);
            if (present(0x10) && si4703ReadAll() && siReg[0x00] == 0x1242) { kind = CHIP_SI4703; addr = 0x10; }
        }
        if (kind == CHIP_SI4735) {                          // comprueba que es un Si47xx (GET_REV tras encender)
            const uint8_t up[3] = { 0x01, 0x10, 0x05 };
            uint8_t rev[9];
            const uint8_t gr[1] = { 0x10 };
            if (!si47Cmd(up, 3, nullptr, 0, 1500) || !si47Cmd(gr, 1, rev, 9)) kind = CHIP_NONE;
            else logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BCAST", "Si47%02u firmware %c.%c", (unsigned)rev[1], rev[2], rev[3]);
        }
        if (kind == CHIP_NONE) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "BCAST", "No broadcast receiver found on I2C (RDA5807M 0x11, Si4703 0x10 with reset pin, Si4735 0x11/0x63)");
            return false;
        }
        vol = constrain(Config.bcast.volume, 0, 15);
        mono = Config.bcast.mono;
        curBand = bandAvailable((uint8_t)Config.bcast.band) ? (uint8_t)Config.bcast.band : BAND_FM;
        rds.reset();
        on = chipOn();
        if (on) { applyVolume(); setBand(curBand); }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BCAST", "%s at 0x%02X: %s %lu kHz", chipName(), addr, bandName(curBand), (unsigned long)curKHz);
        return on;
    }

    void loop() {
        if (kind == CHIP_NONE || !on) return;
        const uint32_t now = millis();
        if (now - lastPoll >= 100) {
            lastPoll = now;
            switch (kind) {
                case CHIP_RDA5807: pollRda(); break;
                case CHIP_SI4703:  pollSi4703(); break;
                case CHIP_SI4735:  pollSi4735(); break;
                default: break;
            }
        }
        if (saveAt && (int32_t)(now - saveAt) >= 0) { saveAt = 0; Config.writeFile(); }
    }

    void power(bool p) {
        if (kind == CHIP_NONE || p == on) return;
        if (!p) {
            switch (kind) {
                case CHIP_RDA5807: rdaWrite(0x02, 0x0000); break;
                case CHIP_SI4703:  siReg[0x02] = (uint16_t)((siReg[0x02] & ~0x0001) | 0x0040); si4703Write(0x02); break;      // DISABLE
                case CHIP_SI4735:  { const uint8_t c[1] = { 0x11 }; si47Cmd(c, 1); break; }
                default: break;
            }
            on = false;
            return;
        }
        on = chipOn();
        if (on) { applyVolume(); setBand(curBand); }
    }

    Chip        chip()      { return kind; }
    const char* chipName()  { switch (kind) { case CHIP_RDA5807: return "RDA5807M"; case CHIP_SI4703: return "Si4703"; case CHIP_SI4735: return "Si4735"; default: return "none"; } }
    bool        isOn()      { return on; }
    uint8_t     band()      { return curBand; }
    int         volume()    { return vol; }
    bool        muted()     { return mute; }
    uint32_t    freqKHz()   { return curKHz; }
    int         rssi()      { return sigRssi; }
    int         snr()       { return sigSnr; }
    bool        stereo()    { return sigStereo; }
    bool        seeking()   { return busySeek; }
    uint16_t    rdsPi()     { return rds.piCode(); }
    const char* stationName() {
        static char n[9];
        if (rds.groupCount() == 0 || curBand != BAND_FM) { n[0] = 0; return n; }
        snprintf(n, sizeof(n), "%s", rds.name());
        return n;
    }
    void radioText(char* out, size_t n) { if (curBand != BAND_FM) { if (n) out[0] = 0; return; } rds.text(out, n); }
    bool rdsClock(int& y, int& mo, int& d, int& h, int& mi, int& off) { return rds.clock(y, mo, d, h, mi, off); }

    void setVolume(int v) { vol = constrain(v, 0, 15); Config.bcast.volume = vol; applyVolume(); saveAt = millis() + 5000; }
    void setMute(bool m) { mute = m; applyVolume(); }
    void setMono(bool m) { mono = m; Config.bcast.mono = m; if (kind == CHIP_SI4735 && curBand == BAND_FM) si47Prop(0x1800, m ? 127 : 0x0031); else applyVolume(); saveAt = millis() + 5000; }

    bool presetStore(int i) {
        if (i < 0 || i >= BcastConfig::PRESETS || curKHz == 0) return false;
        Config.bcast.presets[i] = (int)curKHz;
        Config.writeFile();
        return true;
    }

    static uint8_t bandOf(uint32_t khz) {
        if (khz >= 64000) return BAND_FM;
        if (khz >= 2300) return BAND_SW;
        if (khz >= 520) return BAND_MW;
        return BAND_LW;
    }

    bool presetRecall(int i) {
        if (i < 0 || i >= BcastConfig::PRESETS || Config.bcast.presets[i] <= 0) return false;
        const uint32_t f = (uint32_t)Config.bcast.presets[i];
        const uint8_t b = bandOf(f);
        if (!bandAvailable(b)) return false;
        if (b != curBand) { curBand = b; if (kind == CHIP_SI4735) si47PowerUp(b); applyVolume(); }
        return tune(f);
    }

    bool presetErase(int i) {
        if (i < 0 || i >= BcastConfig::PRESETS) return false;
        Config.bcast.presets[i] = 0;
        Config.writeFile();
        return true;
    }

    int presetIndexNow() {
        for (int i = 0; i < BcastConfig::PRESETS; i++) if (Config.bcast.presets[i] > 0 && (uint32_t)Config.bcast.presets[i] == curKHz) return i;
        return -1;
    }

    int presetNext(int dir) {
        const int from = presetIndexNow();
        for (int k = 1; k <= BcastConfig::PRESETS; k++) {
            const int i = ((from < 0 ? (dir > 0 ? -1 : 0) : from) + dir * k + BcastConfig::PRESETS * 4) % BcastConfig::PRESETS;
            if (Config.bcast.presets[i] > 0 && presetRecall(i)) return i;
        }
        return -1;
    }
}
