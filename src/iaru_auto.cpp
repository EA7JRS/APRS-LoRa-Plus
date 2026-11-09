/*
 * APRS LoRa+ - Region IARU automatica.
 * Copyright (C) 2026 Manuel - EA7JRS   (GPL-3.0-or-later)
 *
 * Que cambia al pasar a la region R (todos los perfiles; el perfil nautico AIS se deja como esta):
 *   - perfiles LoRa/FSK: region y frecuencia (canal equivalente; frecuencia Custom respetada si cabe en la banda de R, si no el canal por defecto)
 *   - FT8 / FT4 / FST4W: region; si la frecuencia de FT4 o FST4W queda fuera de la banda de R se vacia (no emite hasta que se rellene) y la de FT8 vuelve a 432,174 MHz
 *   - CW Beacon y Telegrafia: region y frecuencia (mismas reglas)
 * No se tocan las frecuencias fijas de WSPR, LR-FHSS ni DAPNET.
 * Las normas nacionales prevalecen siempre: la deteccion es orientativa (limites simplificados de la UIT).
 */
#include <TinyGPS++.h>
#include <vector>
#include "iaru_auto.h"
#include "iaru_plan.h"
#include "configuration.h"
#include "lora_utils.h"
#include "display.h"
#include "logger.h"

extern TinyGPSPlus      gps;
extern bool             gpsIsActive;
extern Configuration    Config;
extern logging::Logger  logger;
extern int              menuDisplay;
extern LoraType         *currentLoRaType;

namespace IARU_Auto {

    namespace {

        const uint32_t STABLE_BOOT_MS   = 20000UL;      // fix con la misma region durante 20 s antes de decidir (arranque y peticion)
        const uint32_t STABLE_MOVE_MS   = 60000UL;      // en marcha: 60 s en la nueva region (no cambiar de un lado a otro junto a un limite)

        int           lastDetected  = 0;                // ultima region con fix estable
        int           candidate     = 0;                // region vista en el fix actual
        uint32_t      candSince     = 0;
        int           lastEvaluated = 0;                // region para la que ya se decidio (aplicar o descartar): no se vuelve a preguntar
        bool          bootDone      = false;
        volatile bool wanted        = false;            // peticion pendiente (SYSTEM / WebUI)
        uint32_t      lastTick      = 0;
        bool          modal         = false;
        int           modalRegion   = 0;
        String        modalTitle, modalApply, modalKeep;

        bool fixOk() {
            return gpsIsActive && gps.location.isValid() && gps.location.age() < 3000 && gps.satellites.isValid() && gps.satellites.value() >= 4;
        }

        // Recorre los campos que cambian. dry = true solo cuenta. Devuelve el numero de frecuencias que cambian; regs = campos de region que cambian
        int walk(int r, bool dry, int* regs) {
            int freqs = 0, rg = 0;
            auto setFreq = [&](long& f, IARU_Plan::Kind k, int from) {
                const long nf = IARU_Plan::mapFrequency(k, from, r, f);
                if (nf != f) { freqs++; if (!dry) f = nf; }
            };
            for (size_t i = 0; i < Config.loraTypes.size(); i++) {
                LoraType& t = Config.loraTypes[i];
                if (t.ais) continue;
                if (t.iaruRegion != r) {
                    rg++;
                    setFreq(t.frequency, IARU_Plan::LORA, t.iaruRegion);
                    if (!dry) t.iaruRegion = r;
                }
                if (t.fskIaruRegion != r) {
                    rg++;
                    if (t.fskFrequency > 0) setFreq(t.fskFrequency, IARU_Plan::FSK, t.fskIaruRegion);
                    if (!dry) t.fskIaruRegion = r;
                }
            }
            auto ftField = [&](int& region, int& freq, int def) {
                if (region == r) return;
                rg++;
                if (freq > 0 && !IARU_Plan::allowed(r, freq)) { freqs++; if (!dry) freq = def; }
                if (!dry) region = r;
            };
            ftField(Config.ft.ft8Region, Config.ft.ft8FreqHz, 432174000);
            ftField(Config.ft.ft4Region, Config.ft.ft4FreqHz, 0);
            ftField(Config.ft.fstRegion, Config.ft.fstFreqHz, 0);
            if (Config.cw.beaconRegion != r) { rg++; setFreq(Config.cw.beaconFreq, IARU_Plan::CWB, Config.cw.beaconRegion); if (!dry) Config.cw.beaconRegion = r; }
            if (Config.cw.keyRegion != r)    { rg++; setFreq(Config.cw.keyFreq, IARU_Plan::CW, Config.cw.keyRegion);        if (!dry) Config.cw.keyRegion = r; }
            #ifdef HAS_SA868
            if (Config.sa868.iaruRegion != r) {         // T-TWR: canal APRS de 2 m de la region (144,800 / 144,390 ...)
                rg++;
                long rx = Config.sa868.rxFreqHz, tx = Config.sa868.txFreqHz;
                setFreq(rx, IARU_Plan::VHF, Config.sa868.iaruRegion);
                setFreq(tx, IARU_Plan::VHF, Config.sa868.iaruRegion);
                if (!dry) { Config.sa868.rxFreqHz = (int)rx; Config.sa868.txFreqHz = (int)tx; Config.sa868.iaruRegion = r; }
            }
            #endif
            if (regs) *regs = rg;
            return freqs;
        }

        // Frecuencia (Hz) que tendra el perfil en uso tras el cambio, para mostrarla en la ventana
        long previewActive(int r, long* before) {
            long f = LoRa_Utils::profileFreqHz();
            *before = f;
            #ifdef HAS_SA868
                return IARU_Plan::mapFrequency(IARU_Plan::VHF, Config.sa868.iaruRegion, r, f);
            #endif
            if (currentLoRaType == nullptr || currentLoRaType->ais) return f;
            if (Config.operatingMode == MODE_MORSE) return f;
            const bool fsk = Config.operatingMode == MODE_APRS_FSK;
            return IARU_Plan::mapFrequency(fsk ? IARU_Plan::FSK : IARU_Plan::LORA, fsk ? currentLoRaType->fskIaruRegion : currentLoRaType->iaruRegion, r, f);
        }

        String mhz(long hz) { return String((double)hz / 1e6, 3); }

        void notice(const String& l1, const String& l2) {
            displayPopup("IARU region", l1, l2, -1, 1800);
        }

        void applyNow(int r) {
            int regs = 0;
            const int freqs = walk(r, false, &regs);
            Config.writeFile();
            if (Config.operatingMode == MODE_APRS_LORA || Config.operatingMode == MODE_APRS_FSK) LoRa_Utils::retune();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "IARU", "Region %d applied (%d region fields, %d frequencies changed)", r, regs, freqs);
            notice(String("Region ") + r + " applied", freqs ? String(freqs) + " freq. changed" : "Frequencies kept");
        }

        void evaluate(int r, bool explicitRequest) {
            lastEvaluated = r;
            int regs = 0;
            const int freqs = walk(r, true, &regs);
            if (regs == 0) {
                if (explicitRequest) notice(String("Region ") + r + " detected", "Already set");
                return;
            }
            if (freqs == 0 || !Config.iaruAuto.confirm) {       // solo etiquetas de region, o sin confirmacion: se aplica
                applyNow(r);
                return;
            }
            long before = 0;
            const long after = previewActive(r, &before);
            modalRegion = r;
            modalTitle  = String("IARU region ") + r;
            modalApply  = (after != before) ? String("Apply ") + mhz(after) : String("Apply ") + freqs + " freq.";
            modalKeep   = "Keep current";
            modal       = true;
            menuDisplay = 9100;
        }

    }

    int    detected()     { return lastDetected; }
    bool   asking()       { return modal; }
    String title()        { return modalTitle; }
    String applyText()    { return modalApply; }
    String keepText()     { return modalKeep; }

    void answer(bool apply) {
        if (!modal) return;
        modal = false;
        menuDisplay = 0;
        if (apply) applyNow(modalRegion);
        else logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "IARU", "Change to region %d declined", modalRegion);
    }

    bool request() {
        wanted = true;
        return fixOk();
    }

    String json() {
        String s = "{\"mode\":";
        s += Config.iaruAuto.mode;
        s += ",\"confirm\":";
        s += Config.iaruAuto.confirm ? "true" : "false";
        s += ",\"fix\":";
        s += fixOk() ? "true" : "false";
        s += ",\"region\":";
        s += lastDetected;
        s += ",\"asking\":";
        s += modal ? "true" : "false";
        s += "}";
        return s;
    }

    void loop() {
        const uint32_t now = millis();
        if (now - lastTick < 1000) return;
        lastTick = now;

        if (modal) {                                            // la ventana se cerro sin responder (tiempo agotado o doble pulsacion): se descarta
            if (menuDisplay < 9100 || menuDisplay > 9101) answer(false);
            return;
        }
        if (Config.iaruAuto.mode == 0 && !wanted) return;

        if (!fixOk()) { candidate = 0; if (wanted) { wanted = false; notice("No GPS fix", "Try again outdoors"); } return; }
        const int r = IARU_Plan::regionAt((float)gps.location.lat(), (float)gps.location.lng());
        if (r != candidate) { candidate = r; candSince = now; }
        const uint32_t stableMs = now - candSince;
        if (stableMs >= STABLE_BOOT_MS && r != lastDetected) lastDetected = r;

        if (menuDisplay != 0) return;                           // no se interrumpe un menu abierto: se reintenta en el siguiente segundo
        if (wanted) {                                           // peticion: pide una region estable, pero no mas que el arranque
            if (stableMs < STABLE_BOOT_MS) return;
            wanted = false;
            evaluate(r, true);
            bootDone = true;
            return;
        }
        if (Config.iaruAuto.mode == 0) return;
        if (!bootDone) {
            if (stableMs < STABLE_BOOT_MS) return;
            bootDone = true;
            evaluate(r, false);
        } else if (Config.iaruAuto.mode == 2 && r != lastEvaluated && stableMs >= STABLE_MOVE_MS) {
            evaluate(r, false);
        }
    }

}
