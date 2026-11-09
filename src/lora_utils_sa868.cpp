/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LoRa_Utils para la LilyGO T-TWR Plus: la misma interfaz que usa el resto del firmware, pero la radio es el SA868 (FM) con los
 * modems del ESP32-S3: APRS AFSK 1200 (modo 0, el analogico de VHF) y APRS FSK 9600 G3RUH (modo 1). No hay LoRa, CW,
 * WSPR, LR-FHSS, DAPNET ni receptor de satelites, y las funciones de esos modos contestan que no estan disponibles.
 */

#ifdef HAS_SA868

#include <Arduino.h>
#include <SPI.h>          // RadioLib se sigue compilando (otras partes lo incluyen) y necesita SPI en el buscador de librerias
#include <logger.h>
#include "notification_utils.h"
#include "configuration.h"
#include "board_pinout.h"
#include "pages_utils.h"
#include "lora_utils.h"
#include "sa868_radio.h"
#include "display.h"

extern logging::Logger  logger;
extern Configuration    Config;

bool operationDone   = true;
bool transmitFlag    = true;

namespace LoRa_Utils {

    static constexpr int POWER_LOW_DBM  = 30;       // ~1 W
    static constexpr int POWER_HIGH_DBM = 32;       // ~1,6 W (la placa admite 1,8 W, pero se calienta)
    static long catRxHz = 0, catTxHz = 0;           // CAT (Hamlib): frecuencia en RAM; 0 = la de la configuracion

    void setFlag() { operationDone = true; }

    int  validPower(int requested) { return requested > (POWER_LOW_DBM + POWER_HIGH_DBM) / 2 ? POWER_HIGH_DBM : POWER_LOW_DBM; }
    int  getPower()                 { return Config.sa868.highPower ? POWER_HIGH_DBM : POWER_LOW_DBM; }
    int  powerMin()                 { return POWER_LOW_DBM; }
    int  powerMax()                 { return POWER_HIGH_DBM; }

    int  setProfilePower(int requested) {
        const bool high = validPower(requested) == POWER_HIGH_DBM;
        if (high != Config.sa868.highPower) {
            Config.sa868.highPower = high;
            SA868::applyConfig();
            Config.writeFile();
        }
        return getPower();
    }

    bool fskAvailable()             { return true; }        // APRS FSK 9600 (G3RUH) por el audio plano del SA868
    bool dapnetAvailable()          { return false; }
    bool cwAvailable()              { return false; }
    void cwSelect(bool)             {}
    void cwKey(bool)                {}
    void cwEnd()                    {}
    bool wsprBegin(uint32_t, int)   { return false; }
    bool wsprStart(uint32_t)        { return false; }
    void wsprTone(uint32_t)         {}
    void wsprEnd()                  {}
    bool lrfhssAvailable()          { return false; }
    int  lrfhssSend(const uint8_t*, size_t) { return -1; }
    bool cwRxBegin()                { return false; }
    float cwRxLevel()               { return -200.0f; }
    void cwRxEnd()                  {}
    bool satRxAvailable()           { return false; }
    bool satRxBegin(const SatRxParams&) { return false; }
    bool satRxTune(int64_t)         { return false; }
    float satRxLevel()              { return -200.0f; }
    int  satRxPoll(uint8_t*, size_t, size_t& len, int&, float&, int&) { len = 0; return 0; }
    void satRxEnd()                 {}
    bool satTxBegin(const SatTxParams&) { return false; }
    bool satTxCarrier(bool, int64_t) { return false; }
    int  satTxAx25(const char*)     { return -1; }
    int  satTxLora(const uint8_t*, size_t, uint32_t& airMs) { airMs = 0; return -1; }
    uint32_t satTxLoraAirMs(size_t) { return 0; }
    void satTxEnd()                 {}

    long profileFreqHz()            { return Config.sa868.rxFreqHz; }
    long liveFreqHz()               { return catRxHz > 0 ? catRxHz : Config.sa868.rxFreqHz; }
    long ppmCorrectionHz(long)      { return 0; }           // el SA868 lleva su propio cristal: la correccion global no se aplica
    double ppmCorrectionHzExact(long) { return 0.0; }

    void retune() {
        catRxHz = catTxHz = 0;
        SA868::applyConfig();
    }

    bool setLiveFreqHz(long hz) {
        if (!SA868::validFreq(hz)) return false;
        if (!SA868::setFrequencies(hz, hz)) return false;
        catRxHz = catTxHz = hz;
        return true;
    }

    bool selectLoraIndex(uint8_t)   { return false; }
    void syncAisProfile()           {}
    bool isAisActive()              { return false; }

    void changeFreq() {
        char f[24];
        snprintf(f, sizeof(f), "%.4f MHz", liveFreqHz() / 1e6);
        displayShow("VHF FREQ>", "", f, "Change it in", "the WebUI (SA868)", "", 2000);
    }

    void setup() {
        if (!SA868::begin()) {
            displayShow("SA868 ERROR", "", "Radio module", "not answering", "", "", 3000);
            return;
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SA868", "AFSK 1200 ready");
    }

    // Cada modo emite solo lo suyo: en esta placa, APRS AFSK 1200 (modo 0) y APRS FSK 9600 G3RUH (modo 1)
    bool aprsTxAllowed() { return Config.operatingMode == MODE_APRS_LORA || Config.operatingMode == MODE_APRS_FSK; }

    // Escuchar antes de transmitir: silenciador abierto o datos AFSK en el aire. Espera con reintentos aleatorios hasta el tope
    // de la WebUI (cadWaitSec) y emite igualmente al agotarlo, como en LoRa
    static void waitClearChannel() {
        const uint32_t start = millis(), maxWait = (uint32_t)Config.cadWaitSec * 1000UL;
        bool shown = false;
        while (SA868::channelBusy()) {
            if (millis() - start > maxWait) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "AFSK Tx", "Channel busy for too long, transmitting anyway");
                return;
            }
            if (!shown) { displayShow("<<< DCD >>>", "Waiting for", "free channel...", 0); shown = true; }
            delay(random(50, 250));
        }
        delay(random(0, 100));                      // persistencia: no emitir todos a la vez cuando el canal se libera
    }

    void sendNewPacket(const String& newPacket) {
        if (!aprsTxAllowed()) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "AFSK Tx", "APRS packet dropped: mode %d is not an APRS mode", (int)Config.operatingMode);
            return;
        }
        if (!SA868::ready()) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "AFSK Tx", "Radio module not ready: %s", newPacket.c_str());
            return;
        }
        if (Config.cadActive) waitClearChannel();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AFSK Tx", "---> %s", newPacket.c_str());

        if (Config.ptt.active) {                    // PTT externo (amplificador)
            digitalWrite(Config.ptt.io_pin, Config.ptt.reverse ? LOW : HIGH);
            delay(Config.ptt.preDelay);
        }
        if (Config.notification.ledTx) digitalWrite(Config.notification.ledTxPin, HIGH);
        if (Config.notification.buzzerActive && Config.notification.txBeep) NOTIFICATION_Utils::beaconTxBeep();

        const bool sent = SA868::transmit(newPacket.c_str());
        transmitFlag = true;
        if (sent) {
            #ifdef HAS_APRS_PAGES
                PAGES_Utils::onTx((int)newPacket.length());
            #endif
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "AFSK Tx", "Packet not valid for AX.25 or too long: %s", newPacket.c_str());
        }

        if (Config.notification.ledTx) digitalWrite(Config.notification.ledTxPin, LOW);
        if (Config.ptt.active) {
            delay(Config.ptt.postDelay);
            digitalWrite(Config.ptt.io_pin, Config.ptt.reverse ? HIGH : LOW);
        }
    }

    void wakeRadio()  { SA868::wake(); }
    void sleepRadio() { SA868::sleep(); }

    // Las tramas llegan ya decodificadas de la tarea de recepcion; se repone la cabecera LoRa APRS que espera el resto de la aplicacion
    ReceivedLoRaPacket receivePacket() {
        ReceivedLoRaPacket rx;
        rx.rssi = 0; rx.snr = 0; rx.freqError = 0;
        if (Config.operatingMode == MODE_SAT) return rx;
        String tnc2;
        int rssi = 0;
        if (SA868::popFrame(tnc2, rssi)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "AFSK Rx", "---> %s", tnc2.c_str());
            rx.text = String("\x3c\xff\x01") + tnc2;
            rx.rssi = rssi;
        }
        return rx;
    }

    ReceivedLoRaPacket receiveFromSleep() { return receivePacket(); }
}

#endif
