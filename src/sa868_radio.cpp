/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Radio FM SA868 de la LilyGO T-TWR Plus. Ver include/sa868_radio.h
 *
 * Pines y alimentacion: libreria de LilyGO (Xinyuan-LilyGO/T-TWR, utilities.h y TWRClass.cpp, MIT) y esquemas Rev2.0 / Rev2.1.
 * Ordenes del firmware de fabrica: hoja de datos del SA868 de NiceRF. Firmware abierto: OpenRTX/sa8x8-fw; la secuencia de
 * arranque del AT1846S y los registros de frecuencia, canal y modo son los de OpenRTX (AT1846S.h, AT1846S_SA8x8.cpp, GPL-3.0).
 */

#ifdef HAS_SA868

#include <Arduino.h>
#include <Wire.h>
#include "driver/i2s_pdm.h"
#include "esp_adc/adc_continuous.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "XPowersLib.h"
#include "configuration.h"
#include "board_pinout.h"
#include "afsk1200.h"
#include "g3ruh_modem.h"
#include "m17_modem.h"
#include "pocsag_modem.h"
#include "fm_core.h"
#include "ais_core.h"
#include "fm_dsp.h"
#include "cw_morse.h"
#include "driver/sdm.h"
#include "driver/gptimer.h"
#include "iaru_plan.h"
#include "gps_utils.h"
#include "tx_unlock.h"
#include <TinyGPS++.h>
#include "sa868_radio.h"
#include "logger.h"
#include "esp_heap_caps.h"

extern Configuration    Config;
extern logging::Logger  logger;
extern TinyGPSPlus      gps;
extern XPowersAXP2101   PMU;            // power_utils.cpp
extern LoraType         *currentLoRaType;
extern Beacon           *currentBeacon;

#if ARDUINO_USB_CDC_ON_BOOT
    #define SA_SERIAL Serial0           // UART0 libre: el registro va por el USB nativo
#else
    static HardwareSerial saSerialPort(2);
    #define SA_SERIAL saSerialPort
#endif

namespace SA868 {

    static constexpr uint32_t RX_FS      = 13200;           // AFSK 1200: 11 muestras por bit
    static constexpr uint32_t RX_FS_9600 = 48000;           // G3RUH 9600: 5 muestras por bit; M17: 10 muestras por simbolo
    static constexpr uint32_t TX_FS      = 48000;
    static constexpr size_t   RX_QUEUE   = 4;
    static constexpr uint32_t TX_MAX_MS  = 6000;            // tope de seguridad de una emision
    static constexpr long     M17_VHF_HZ = 144875000L;      // M17: sugeridas por la IARU R1 (boletin VHF+ de junio de 2023)
    static constexpr long     M17_UHF_HZ = 433475000L;
    static constexpr uint32_t M17_HEARD_MS = 30000;         // una misma estacion M17 (voz) se pasa a APRS como mucho cada 30 s
    static constexpr long     DAPNET_VHF_HZ = 144862500L;   // DAPNET: 144,8625 MHz en VHF, 439,9875 MHz en UHF
    static constexpr long     DAPNET_UHF_HZ = 439987500L;
    #ifndef SA868_SPK_PIN
        #define SA868_SPK_PIN 45                            // Rev2.1: ESP32 -> amplificador del altavoz (ESP32_PWM_TONE de LilyGO), con ALDO3
    #endif

    struct RxItem { char text[AX25G3::MAX_FRAME + 120]; };

    static Firmware             fw          = Firmware::NONE;
    static int                  rev         = 0;
    static bool                 isUhf       = false;
    static bool                 ok          = false;
    static volatile bool        txActive    = false;
    static volatile bool        resetDemod  = false;
    static AFSK::Demod          demod;
    static G3RUH::BaseRx        g3rx;
    static M17M::Rx             m17rx;
    static bool                 mode96      = false;        // modo APRS FSK 9600 (G3RUH): audio plano, canal de 25 kHz
    static bool                 m17         = false;        // modo M17: audio plano, canal de 25 kHz
    static bool                 flat        = false;        // 9600 o M17
    static long                 curRx       = 0, curTx = 0; // frecuencias en uso
    static float*               m17Syms     = nullptr;      // simbolos de la emision M17 (PSRAM)
    // DAPNET: POCSAG de banda base; los bytes van a una cola y los lee el bucle principal (DAPNET_Utils no es de esta tarea)
    static bool                 dap         = false;
    static PocsagModem::BaseRx  pocRx;
    static QueueHandle_t        dapQueue    = nullptr;
    // AIS: mensajes a una cola que lee el bucle principal
    static bool                 ais         = false;
    static AisCore::Demod       aisRx;
    struct AisItem { uint8_t n; char ch; uint8_t d[AisCore::MAX_MSG_BYTES]; };
    static QueueHandle_t        aisQueue    = nullptr;
    static volatile char        aisCh       = 'A';
    // FM (voz)
    static bool                 fmMode      = false;
    static bool                 fmLoaded    = false;        // ya hay canal (FM_Utils::begin)
    static FmCore::Channel      fmCh;
    static int                  fmSquelch   = 1;            // nivel en uso (el manual o el que elige el automatico)
    static volatile bool        fmOpen      = false;        // silenciador abierto (canal ocupado)
    static volatile bool        fmSoftOpen  = false;        // decision del silenciador por software (tarea de recepcion)
    static volatile int         fmRssiDbm   = 0;
    static volatile bool        fmCtcssOk   = true;         // OpenRTX con TSQL: tono detectado
    static bool                 fmVoiceOn   = false;
    static bool                 fmToneOk    = true;
    static bool                 espAudio    = false;        // Rev2.1: el altavoz lo alimenta el ESP32 (reduccion de ruido / silenciador)
    static FmDsp::NoiseDetector fmNoise;
    static FmDsp::AutoSquelch   fmAuto;
    static FmDsp::NoiseReducer* fmNr        = nullptr;
    static sdm_channel_handle_t spkSdm      = nullptr;
    static gptimer_handle_t     spkTimer    = nullptr;
    static constexpr int        SPK_RING    = 2048;
    static int8_t               spkRing[SPK_RING];
    static volatile uint16_t    spkHead = 0, spkTail = 0;
    // CW (modo MORSE): A1A con el PTT o MCW (tono), escucha por el silenciamiento del ruido; tono local por el altavoz (Rev2.1)
    static bool                 cwMode      = false;
    static volatile bool        cwRx        = false;
    static FmDsp::QuietingDetector cwDet;
    static volatile float       cwLevel     = 0;
    static volatile bool        cwSidetone  = false;        // tono local mientras se manipula (Rev2.1)
    static bool                 cwKeyed     = false, cwSession = false;
    static volatile bool        mcwTone     = false, mcwRun = false;
    static TaskHandle_t         mcwTask     = nullptr;
    static uint16_t             reg30Rx = 0, reg30Tx = 0;  // OpenRTX: registro 0x30 en recepcion / emision (una sola orden por flanco)
    static adc_continuous_handle_t adc      = nullptr;
    static i2s_chan_handle_t    txChan      = nullptr;
    static QueueHandle_t        rxQueue     = nullptr;
    static TaskHandle_t         rxTask      = nullptr;

    // ---- UART del modulo ---------------------------------------------------------------------------------------------

    // Manda una orden y lee la primera linea de respuesta (sin \r\n). false si no llega nada a tiempo
    static bool command(const char* cmd, char* resp, size_t n, uint32_t timeoutMs = 300) {
        while (SA_SERIAL.available()) SA_SERIAL.read();
        SA_SERIAL.print(cmd);
        SA_SERIAL.print("\r\n");
        size_t len = 0;
        const uint32_t start = millis();
        while (millis() - start < timeoutMs) {
            while (SA_SERIAL.available()) {
                char c = (char)SA_SERIAL.read();
                if (c == '\r') continue;
                if (c == '\n') {
                    if (len == 0) continue;
                    resp[len] = 0;
                    return true;
                }
                if (len + 1 < n) resp[len++] = c;
            }
            delay(2);
        }
        if (n) resp[len < n ? len : n - 1] = 0;
        return len > 0;
    }

    static bool commandOk(const char* cmd, const char* expect, uint32_t timeoutMs = 300) {
        char r[48];
        return command(cmd, r, sizeof(r), timeoutMs) && strstr(r, expect) != nullptr;
    }

    // ---- AT1846S por el firmware de OpenRTX --------------------------------------------------------------------------

    static void poke(uint8_t reg, uint16_t value) {
        char c[32];
        snprintf(c, sizeof(c), "AT+POKE=%u,%u", reg, value);
        if (!commandOk(c, "OK", 150)) logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "SA868", "POKE %u failed", reg);
    }

    static uint16_t peek(uint8_t reg) {
        char c[24], r[24];
        snprintf(c, sizeof(c), "AT+PEEK=%u", reg);
        if (!command(c, r, sizeof(r), 150)) return 0;
        return (uint16_t)strtoul(r, nullptr, 10);
    }

    static void maskSet(uint8_t reg, uint16_t mask, uint16_t value) {
        const uint16_t v = peek(reg);
        poke(reg, (uint16_t)((v & ~mask) | (value & mask)));
    }

    static void reloadConfig() {
        const uint16_t mode = peek(0x30) & 0x0060;
        maskSet(0x30, 0x0060, 0x0000);
        maskSet(0x30, 0x0060, mode);
    }

    static void at1846Init() {
        static const uint16_t seq1[][2] = {
            {0x30, 0x0004}, {0x04, 0x0FD0}, {0x1F, 0x1000}, {0x09, 0x03AC}, {0x24, 0x0001}, {0x31, 0x0031}, {0x33, 0x45F5},
            {0x34, 0x2B89}, {0x3F, 0x3263}, {0x41, 0x470F}, {0x42, 0x1036}, {0x43, 0x00BB}, {0x44, 0x06FF}, {0x47, 0x7F2F},
            {0x4E, 0x0082}, {0x4F, 0x2C62}, {0x53, 0x0094}, {0x54, 0x2A3C}, {0x55, 0x0081}, {0x56, 0x0B02}, {0x57, 0x1C00},
            {0x5A, 0x4935}, {0x58, 0xBCCD}, {0x62, 0x3263}, {0x4E, 0x2082}, {0x63, 0x16AD}, {0x30, 0x40A4} };
        static const uint16_t seq2[][2] = {
            {0x58, 0xBCED}, {0x0A, 0x7BA0}, {0x41, 0x4731}, {0x44, 0x05FF}, {0x59, 0x09D2}, {0x44, 0x05CF}, {0x44, 0x05CC},
            {0x48, 0x1A32}, {0x60, 0x1A32}, {0x3F, 0x29D1}, {0x0A, 0x7BA0}, {0x49, 0x0C96}, {0x33, 0x45F5}, {0x41, 0x470F},
            {0x42, 0x1036}, {0x43, 0x00BB} };
        poke(0x30, 0x0001);                                 // reinicio
        delay(50);
        for (auto& r : seq1) poke((uint8_t)r[0], r[1]);
        delay(50);
        poke(0x30, 0x40A6);                                 // calibracion
        delay(100);
        poke(0x30, 0x4006);
        delay(100);
        for (auto& r : seq2) poke((uint8_t)r[0], r[1]);
        // modo FM (voz): filtros de voz y pre/desenfasis segun la configuracion
        poke(0x33, 0x44A5); poke(0x41, 0x4431); poke(0x42, 0x10F0); poke(0x43, 0x00A9);
        poke(0x58, Config.sa868.filters ? 0xBC05 : 0xBCFD);
        poke(0x44, 0x06FF); poke(0x40, 0x0030);
        maskSet(0x57, 0x0001, 0x0000);                      // sin realimentacion de audio
        maskSet(0x3A, 0x7000, 0x4000);                      // canal de voz: entrada de microfono
        reloadConfig();
    }

    static void at1846Bandwidth(bool wide) {
        static const uint16_t w25[][2] = { {0x15, 0x1F00}, {0x32, 0x7564}, {0x3A, 0x4003}, {0x3F, 0x29D2}, {0x3C, 0x0E1C}, {0x48, 0x1E38},
            {0x62, 0x3767}, {0x65, 0x248A}, {0x66, 0xFF2E}, {0x7F, 0x0001}, {0x06, 0x0024}, {0x07, 0x0214}, {0x08, 0x0224},
            {0x09, 0x0314}, {0x0A, 0x0324}, {0x0B, 0x0344}, {0x0D, 0x1384}, {0x0E, 0x1B84}, {0x0F, 0x3F84}, {0x12, 0xE0EB}, {0x7F, 0x0000} };
        static const uint16_t w12[][2] = { {0x15, 0x1100}, {0x32, 0x4495}, {0x3A, 0x4003}, {0x3F, 0x28D0}, {0x3C, 0x0F1E}, {0x48, 0x1DB6},
            {0x62, 0x1425}, {0x65, 0x2494}, {0x66, 0xEB2E}, {0x7F, 0x0001}, {0x06, 0x0014}, {0x07, 0x020C}, {0x08, 0x0214},
            {0x09, 0x030C}, {0x0A, 0x0314}, {0x0B, 0x0324}, {0x0C, 0x0344}, {0x0D, 0x1344}, {0x0E, 0x1B44}, {0x0F, 0x3F44}, {0x12, 0xE0EB}, {0x7F, 0x0000} };
        if (wide) for (auto& r : w25) poke((uint8_t)r[0], r[1]);
        else      for (auto& r : w12) poke((uint8_t)r[0], r[1]);
        maskSet(0x30, 0x3000, wide ? 0x3000 : 0x0000);
        maskSet(0x3A, 0x7000, 0x4000);                      // la tabla de canal toca el 0x3A: de nuevo la entrada de microfono
        reloadConfig();
    }

    // Correccion global de frecuencia (WebUI > Frequency calibration, ppm): el mismo cristal da la emision y la recepcion del modulo
    long ppmCorrectionHz(long hz) { return (long)llround((double)hz * (double)Config.radioPpm1000 / 1e9); }
    static long corrected(long hz) { return hz + ppmCorrectionHz(hz); }
    // "145.5000" para el AT+DMOSETGROUP (pasos de 100 Hz), ya corregida
    static void niceRfFreq(long hz, char* out, size_t n) {
        const long c = (corrected(hz) + 50) / 100;          // en centenas de Hz, redondeada
        snprintf(out, n, "%03ld.%04ld", c / 10000L, c % 10000L);
    }

    static void at1846Freq(long hz) {
        hz = corrected(hz);
        const uint32_t v = (uint32_t)(((uint64_t)hz * 16 + 500) / 1000);   // pasos de 62,5 Hz
        poke(0x29, (uint16_t)(v >> 16));
        poke(0x2A, (uint16_t)(v & 0xFFFF));
        reloadConfig();
    }

    static void at1846Mode(uint8_t mode) { maskSet(0x30, 0x0060, (uint16_t)(mode << 5)); }    // 0 apagado, 1 RX, 2 TX

    // ---- Firmware y revision -----------------------------------------------------------------------------------------

    static Firmware detectFirmware(int tries) {
        char r[48];
        for (int i = 0; i < tries; i++) {
            if (command("AT+DMOCONNECT", r, sizeof(r), 400)) {
                if (strstr(r, "+DMOCONNECT:0")) return Firmware::NICERF;
                if (command("AT+VERSION", r, sizeof(r), 400) && strstr(r, "sa8x8-fw")) return Firmware::OPENRTX;
            }
            delay(200);
        }
        return Firmware::NONE;
    }

    static void powerRev20(bool on) {                       // Rev2.0: el modulo (y el LED) van por el DC3 de la PMU a 3,4 V
        if (on) { PMU.setDC3Voltage(3400); PMU.enableDC3(); }
        else    PMU.disableDC3();
    }

    static void applySpeaker() {
        if (rev == 21) {                                    // ALDO3: 0 = el amplificador lo alimenta la radio, 1 = el ESP32 (GPIO45)
            PMU.setALDO3Voltage(3300);
            if (Config.sa868.speaker && !espAudio) PMU.disableALDO3(); else PMU.enableALDO3();     // sin altavoz: el ESP32 calla
        }
        if (fw == Firmware::OPENRTX) commandOk(Config.sa868.speaker ? "AT+AUDIO=1" : "AT+AUDIO=0", "OK");
    }

    // H/L del modulo (IO38): alto = potencia alta. La Rev2.0 lo lleva seguro; en la Rev2.1 OpenRTX usa el mismo pin (sin verificar)
    static void applyPowerLevel(bool high) {
        #ifdef SA868_HL_PIN
        pinMode(SA868_HL_PIN, OUTPUT);
        digitalWrite(SA868_HL_PIN, high ? HIGH : LOW);
        #else
        (void)high;
        #endif
    }

    static void applyPower() { applyPowerLevel(Config.sa868.highPower); }

    bool validFreq(long hz) {
        return isUhf ? (hz >= 400000000L && hz <= 480000000L) : (hz >= 136000000L && hz <= 174000000L);
    }

    // Region IARU donde se esta, por la posicion GPS (o la ultima guardada); 0 = no se sabe
    static int txRegion() {
        if (gps.location.isValid() && gps.location.age() < 60000UL) return IARU_Plan::regionAt((float)gps.location.lat(), (float)gps.location.lng());
        if (GPS_Utils::hasLastPosition()) return IARU_Plan::regionAt((float)GPS_Utils::getLastLat(), (float)GPS_Utils::getLastLng());
        return 0;
    }

    // Emision solo en las bandas de aficionado (fijo en el firmware). La region sale de la posicion GPS; sin posicion, solo lo comun a
    // las tres regiones (144-146 y 430-440 MHz): la region elegida en la WebUI no amplia lo que se puede emitir
    bool txAllowed(long hz) {
        if (TX_Unlock::unlocked()) return validFreq(hz);   // desbloqueado con el codigo de este equipo: toda la banda del modulo
        return validFreq(hz) && FmCore::txAllowed(hz, txRegion());
    }

    static bool txBlocked(long hz) {
        if (txAllowed(hz)) return false;
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 2000) {
            lastLog = millis();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "SA868", "TX on %.4f MHz refused: outside the amateur band (IARU region %d from the GPS position, 0 = no position: 144-146 / 430-440 only)", hz / 1e6, txRegion());
        }
        return true;
    }

    // Frecuencias de la otra banda (la configuracion de fabrica es la de VHF): el canal APRS por defecto de la region IARU para la banda
    // del modulo, guardado para que la WebUI muestre lo que de verdad usa la radio
    static void fixBandDefaults() {
        const bool rxOk = validFreq(Config.sa868.rxFreqHz), txOk = validFreq(Config.sa868.txFreqHz);
        if (rxOk && txOk) return;
        long def = IARU_Plan::defaultHz(isUhf ? IARU_Plan::UHF : IARU_Plan::VHF, Config.sa868.iaruRegion);
        if (def <= 0 || !validFreq(def)) def = isUhf ? 432500000L : 144800000L;
        if (!rxOk) Config.sa868.rxFreqHz = (int)def;
        if (!txOk) Config.sa868.txFreqHz = rxOk ? Config.sa868.rxFreqHz : (int)def;
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SA868", "%s module: frequency set to %.4f MHz", isUhf ? "UHF" : "VHF", Config.sa868.rxFreqHz / 1e6);
        Config.writeFile();
    }

    bool setFrequencies(long rxHz, long txHz) {
        if (!ok || !validFreq(rxHz) || !validFreq(txHz)) return false;
        curRx = rxHz; curTx = txHz;
        if (fw == Firmware::OPENRTX) {
            at1846Freq(rxHz);
            at1846Mode(1);
            return true;
        }
        char c[64], ft[16], fr[16];
        niceRfFreq(txHz, ft, sizeof(ft));
        niceRfFreq(rxHz, fr, sizeof(fr));
        snprintf(c, sizeof(c), "AT+DMOSETGROUP=%d,%s,%s,0000,%d,0000", (Config.sa868.wide || flat) ? 1 : 0, ft, fr, ais ? 0 : constrain(Config.sa868.squelch, 0, 8));   // AIS: silenciador abierto (rafagas de 26 ms)
        const bool good = commandOk(c, "+DMOSETGROUP:0", 800);
        if (!good) logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SA868", "%s rejected", c);
        return good;
    }

    bool applyConfig() {
        if (!ok) return false;
        bool good = true;
        long rx = Config.sa868.rxFreqHz, tx = Config.sa868.txFreqHz;
        if (mode96 && currentLoRaType && validFreq(currentLoRaType->fskFrequency)) {
            // 9600 en UHF: la frecuencia FSK del perfil, la misma que usan las demas placas en APRS FSK 9600 (lista IARU FSK)
            rx = tx = currentLoRaType->fskFrequency;
        }
        if (m17) rx = tx = m17FreqHz();
        if (dap) rx = tx = dapnetFreqHz();
        if (ais) rx = tx = (aisCh == 'B') ? AisCore::FREQ_B : AisCore::FREQ_A;
        if (cwMode) rx = tx = cwFreqHz(false);
        if (fmMode && fmLoaded) return fmApply(fmCh, fmSquelch);
        if (!validFreq(rx)) rx = isUhf ? 432500000L : 144800000L;           // no deberia pasar: fixBandDefaults() al arrancar
        if (!validFreq(tx)) tx = rx;
        if (fw == Firmware::OPENRTX) {
            if (flat) {
                // 9600 y M17: como el M17 de OpenRTX en esta placa (radio_setOpmode): registros del modo DMR (sin filtros de voz ni
                // pre/desenfasis: 0x58 = 0xBCFD) y despues el canal de 25 kHz
                static const uint16_t dmr[][2] = { {0x3A, 0x00C2}, {0x33, 0x45F5}, {0x41, 0x4731}, {0x42, 0x1036}, {0x43, 0x00BB},
                                                   {0x58, 0xBCFD}, {0x44, 0x06CC}, {0x40, 0x0031} };
                for (auto& r : dmr) poke((uint8_t)r[0], r[1]);
                reloadConfig();
                at1846Bandwidth(true);
            } else {
                at1846Bandwidth(Config.sa868.wide);
                maskSet(0x58, 0x00F8, Config.sa868.filters ? 0x0000 : 0x00F8);
            }
            if (!flat) maskSet(0x44, 0x00F0, (uint16_t)((constrain(Config.sa868.volume, 1, 8) * 15 / 8) << 4));    // ganancia del DAC de audio
            good = setFrequencies(rx, tx);
        } else {
            good = setFrequencies(rx, tx);
            char c[40];
            snprintf(c, sizeof(c), "AT+DMOSETVOLUME=%d", constrain(Config.sa868.volume, 1, 8));
            good &= commandOk(c, "+DMOSETVOLUME:0");
            const int f = (Config.sa868.filters && !flat) ? 0 : 1;            // 9600 y M17: los tres filtros puenteados (1 = fuera)
            snprintf(c, sizeof(c), "AT+SETFILTER=%d,%d,%d", f, f, f);
            good &= commandOk(c, "+DMOSETFILTER:0");
        }
        applyPower();
        applySpeaker();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SA868", "RX %.4f / TX %.4f MHz, %s, SQ %d, vol %d, %s power: %s",
                   rx / 1e6, tx / 1e6, (Config.sa868.wide || flat) ? (flat ? "25 kHz, flat audio" : "25 kHz") : "12.5 kHz", Config.sa868.squelch, Config.sa868.volume,
                   Config.sa868.highPower ? "high" : "low", good ? "ok" : "ERROR");
        return good;
    }

    // ---- Audio -------------------------------------------------------------------------------------------------------

    static bool audioTxBegin() {
        i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        cc.auto_clear = true;                               // sin datos manda ceros (silencio)
        if (i2s_new_channel(&cc, &txChan, nullptr) != ESP_OK) return false;
        i2s_pdm_tx_config_t pc = {};
        #ifdef I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG
            pc.clk_cfg  = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(TX_FS);
            pc.slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
        #else
            pc.clk_cfg  = I2S_PDM_TX_CLK_DEFAULT_CONFIG(TX_FS);
            pc.slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
        #endif
        pc.gpio_cfg.clk  = I2S_GPIO_UNUSED;                 // modo DAC: solo la linea de datos, filtrada por el RC de la placa
        pc.gpio_cfg.dout = (gpio_num_t)SA868_AUDIO_OUT_PIN;
        #if SOC_I2S_PDM_MAX_TX_LINES > 1
            pc.gpio_cfg.dout2 = I2S_GPIO_UNUSED;
        #endif
        if (i2s_channel_init_pdm_tx_mode(txChan, &pc) != ESP_OK) {
            i2s_del_channel(txChan);
            txChan = nullptr;
            return false;
        }
        return true;
    }

    static void onFrame(const uint8_t* frame, size_t len, void*) {
        RxItem item;
        if (AX25G3::frameToTnc2(frame, len, item.text, sizeof(item.text)) == 0) return;
        if (rxQueue) xQueueSend(rxQueue, &item, 0);         // cola llena: se pierde (el bucle principal va retrasado)
    }

    // ---- M17: lo recibido pasa a APRS (TNC2) -------------------------------------------------------------------------

    static void queueText(const char* text) {
        RxItem item;
        snprintf(item.text, sizeof(item.text), "%s", text);
        if (rxQueue) xQueueSend(rxQueue, &item, 0);
    }

    static void onM17Packet(const M17M::Lsf& lsf, const uint8_t* data, size_t len, void*) {
        static char t[sizeof(RxItem::text)];
        if (M17M::packetToTnc2(lsf, data, len, t, sizeof(t)) == 0) return;
        queueText(t);
    }

    // Emisiones de voz / datos: la cabecera llega al principio y otra vez por el LICH cada 6 tramas (240 ms). Cada estacion se pasa
    // a APRS como mucho cada 30 s (antes si aparece la posicion)
    static void onM17Lsf(const M17M::Lsf& lsf, void*) {
        if (!lsf.stream) return;                            // la del paquete ya va en onM17Packet
        struct Seen { char src[10]; uint32_t ms; bool pos; };
        static Seen seen[8];
        static uint8_t next = 0;
        const uint32_t now = millis();
        Seen* hit = nullptr;
        for (auto& e : seen) if (e.src[0] && strcmp(e.src, lsf.src) == 0) hit = &e;
        if (hit && now - hit->ms < M17_HEARD_MS && (hit->pos || !lsf.hasPosition)) return;
        if (!hit) { hit = &seen[next]; next = (uint8_t)((next + 1) % 8); snprintf(hit->src, sizeof(hit->src), "%s", lsf.src); }
        hit->ms = now; hit->pos = lsf.hasPosition;
        static char t[sizeof(RxItem::text)];
        if (M17M::lsfToTnc2(lsf, t, sizeof(t)) == 0) return;
        queueText(t);
    }

    // ---- DAPNET ------------------------------------------------------------------------------------------------------

    static void onPocByte(uint8_t b, void*) { if (dapQueue) xQueueSend(dapQueue, &b, 0); }

    static void onAis(const uint8_t* m, size_t n, void*) {
        if (!aisQueue || n == 0 || n > AisCore::MAX_MSG_BYTES) return;
        static AisItem it;                                  // solo la tarea de recepcion lo usa
        it.n = (uint8_t)n; it.ch = aisCh;
        memcpy(it.d, m, n);
        xQueueSend(aisQueue, &it, 0);
    }

    bool modeAis() { return ais; }
    bool popAis(uint8_t* m, size_t& n, char& ch) {
        static AisItem it;
        if (!aisQueue || xQueueReceive(aisQueue, &it, 0) != pdTRUE) return false;
        n = it.n; ch = it.ch;
        memcpy(m, it.d, it.n);
        return true;
    }
    bool aisChannel(char ch) {
        if (!ais || (ch != 'A' && ch != 'B')) return false;
        if (ch == aisCh) return true;
        aisCh = ch;
        const long f = ch == 'B' ? AisCore::FREQ_B : AisCore::FREQ_A;
        const bool good = setFrequencies(f, f);
        resetDemod = true;
        return good;
    }
    char aisChannelNow() { return aisCh; }
    uint32_t aisFrames() { return aisRx.frames; }
    uint32_t aisCrcErrors() { return aisRx.crcErrors; }
    bool aisCarrier() { return aisRx.dataCarrier(); }

    long dapnetFreqHz() {
        if (validFreq(Config.sa868.dapnetFreqHz)) return Config.sa868.dapnetFreqHz;
        return isUhf ? DAPNET_UHF_HZ : DAPNET_VHF_HZ;
    }

    size_t popDapnet(uint8_t* out, size_t max) {
        size_t n = 0;
        while (dapQueue && n < max && xQueueReceive(dapQueue, &out[n], 0) == pdTRUE) n++;
        return n;
    }

    // ---- Altavoz desde el ESP32 (Rev2.1): sigma-delta en GPIO45 a 13,2 kHz desde un temporizador ------------------

    static inline void spkPush(int8_t s) {
        const uint16_t next = (uint16_t)((spkHead + 1) % SPK_RING);
        if (next == spkTail) return;                        // lleno: se pierde la muestra (el temporizador va mas lento)
        spkRing[spkHead] = s;
        spkHead = next;
    }

    static bool IRAM_ATTR spkTick(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) {
        int8_t s = 0;
        if (spkTail != spkHead) { s = spkRing[spkTail]; spkTail = (uint16_t)((spkTail + 1) % SPK_RING); }
        sdm_channel_set_pulse_density(spkSdm, s);
        return false;
    }

    static bool speakerBegin() {
        if (spkSdm) return true;
        sdm_config_t sc = {};
        sc.clk_src = SDM_CLK_SRC_DEFAULT;
        sc.gpio_num = SA868_SPK_PIN;
        sc.sample_rate_hz = 1000000;                        // 1 MHz: el filtro RC de la placa deja la voz
        if (sdm_new_channel(&sc, &spkSdm) != ESP_OK) return false;
        sdm_channel_enable(spkSdm);
        gptimer_config_t tc = {};
        tc.clk_src = GPTIMER_CLK_SRC_DEFAULT;
        tc.direction = GPTIMER_COUNT_UP;
        tc.resolution_hz = 1000000;
        if (gptimer_new_timer(&tc, &spkTimer) != ESP_OK) return false;
        gptimer_event_callbacks_t cb = {};
        cb.on_alarm = spkTick;
        gptimer_register_event_callbacks(spkTimer, &cb, nullptr);
        gptimer_alarm_config_t ac = {};
        ac.alarm_count = 1000000 / RX_FS;                   // ~76 us
        ac.reload_count = 0;
        ac.flags.auto_reload_on_alarm = true;
        gptimer_set_alarm_action(spkTimer, &ac);
        gptimer_enable(spkTimer);
        gptimer_start(spkTimer);
        return true;
    }

    static void rxTaskFn(void*) {
        static uint8_t buf[256 * SOC_ADC_DIGI_RESULT_BYTES];
        for (;;) {
            uint32_t got = 0;
            if (adc_continuous_read(adc, buf, sizeof(buf), &got, 100) != ESP_OK) continue;
            if (resetDemod) { if (m17) m17rx.reset(); else if (dap) pocRx.reset(); else if (ais) aisRx.reset(); else if (mode96) g3rx.reset(); else demod.reset(); resetDemod = false; }
            const uint32_t nSamples = got / SOC_ADC_DIGI_RESULT_BYTES;
            static float tonePh = 0;
            auto tone = [&](bool on) -> int8_t {           // 700 Hz para el altavoz (tono local / CW recibida)
                if (!on) return 0;
                tonePh += 2.0f * (float)M_PI * 700.0f / (float)RX_FS;
                if (tonePh > 2.0f * (float)M_PI) tonePh -= 2.0f * (float)M_PI;
                return (int8_t)(sinf(tonePh) * 12.0f * (float)constrain(Config.sa868.volume, 1, 8));
            };
            if (txActive) {                                 // en emision el ADC oye la propia senal: se descarta (el altavoz calla o da el tono local)
                if (espAudio) for (uint32_t k = 0; k < nSamples; k++) spkPush(tone(cwSidetone));
                continue;
            }
            if (cwMode) {                                   // MORSE: escucha CW por el silenciamiento; el altavoz da un tono con la portadora
                for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= got; i += SOC_ADC_DIGI_RESULT_BYTES) {
                    const adc_digi_output_data_t* p = (const adc_digi_output_data_t*)&buf[i];
                    const float x = (float)((int)p->type2.data - 2048) / 2048.0f;
                    if (cwRx && cwDet.feed(x)) cwLevel = cwDet.levelDb();
                    if (espAudio) spkPush(tone(cwRx && cwDet.on()));
                }
                continue;
            }
            const bool gate = fmOpen;
            const float gain = espAudio ? (float)constrain(Config.sa868.volume, 1, 8) / 4.0f : 0.0f;
            for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= got; i += SOC_ADC_DIGI_RESULT_BYTES) {
                const adc_digi_output_data_t* p = (const adc_digi_output_data_t*)&buf[i];
                const int v = (int)p->type2.data;
                const float x = (float)(v - 2048) / 2048.0f;
                if (m17) { m17rx.feed(x); continue; }
                if (dap) { pocRx.feed(x, onPocByte, nullptr); continue; }
                if (ais) { aisRx.feed(x, onAis, nullptr); continue; }
                if (mode96) { g3rx.feed(x, onFrame, nullptr); continue; }
                demod.feed(x, onFrame, nullptr);            // AFSK: tambien en FM (APRS en el canal de voz)
                if (!fmMode) continue;
                if (fmNoise.feed(x)) fmSoftOpen = fmAuto.softUpdate(fmNoise, fmRssiDbm, fw == Firmware::OPENRTX);
                if (espAudio) {
                    float y = fmNr ? fmNr->process(x) : x;
                    y = gate ? y * gain : 0.0f;
                    int s = (int)lroundf(y * 127.0f);
                    spkPush((int8_t)constrain(s, -127, 127));
                }
            }
        }
    }

    static bool audioRxBegin() {
        adc_continuous_handle_cfg_t hc = {};
        hc.max_store_buf_size = 4096;
        hc.conv_frame_size    = 256 * SOC_ADC_DIGI_RESULT_BYTES;
        if (adc_continuous_new_handle(&hc, &adc) != ESP_OK) return false;
        adc_digi_pattern_config_t pat = {};
        pat.atten     = ADC_ATTEN_DB_12;
        pat.channel   = SA868_AUDIO_ADC_CHANNEL;
        pat.unit      = ADC_UNIT_1;
        pat.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
        adc_continuous_config_t cfg = {};
        cfg.pattern_num    = 1;
        cfg.adc_pattern    = &pat;
        cfg.sample_freq_hz = flat ? RX_FS_9600 : RX_FS;
        cfg.conv_mode      = ADC_CONV_SINGLE_UNIT_1;
        cfg.format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
        if (adc_continuous_config(adc, &cfg) != ESP_OK) return false;
        if (m17) {
            if (!m17rx.begin()) return false;
            m17rx.setCallbacks(onM17Lsf, onM17Packet, nullptr);
        } else if (dap) {
            pocRx.begin((float)RX_FS_9600, Config.dapnet.baud, (float)Config.sa868.dapnetHpfHz);
            dapQueue = xQueueCreate(512, 1);
            if (!dapQueue) return false;
        } else if (ais) {
            if (!aisRx.begin((float)RX_FS_9600, (float)Config.ais.hpfHz)) return false;
            aisQueue = xQueueCreate(6, sizeof(AisItem));
            if (!aisQueue) return false;
        } else if (mode96 ? !g3rx.begin(RX_FS_9600) : !demod.begin(RX_FS)) return false;
        if (fmMode) {
            fmNoise.begin((float)RX_FS);
            if (espAudio && Config.fm.nrLevel > 0) {
                fmNr = new FmDsp::NoiseReducer();
                if (fmNr) { fmNr->begin(); fmNr->setStrength(Config.fm.nrLevel / 3.0f); }
            }
        }
        rxQueue = xQueueCreate(RX_QUEUE, sizeof(RxItem));
        if (!rxQueue) return false;
        if (adc_continuous_start(adc) != ESP_OK) return false;
        return xTaskCreatePinnedToCore(rxTaskFn, "afskRx", (m17 || fmMode) ? 12288 : 6144, nullptr, 5, &rxTask, 0) == pdPASS;   // M17: Viterbi; FM: reduccion de ruido
    }

    // ---- Arranque ----------------------------------------------------------------------------------------------------

    bool begin() {
        pinMode(SA868_PTT_PIN, OUTPUT);
        digitalWrite(SA868_PTT_PIN, HIGH);                  // alto = recepcion
        pinMode(SA868_MIC_SEL_PIN, OUTPUT);
        digitalWrite(SA868_MIC_SEL_PIN, LOW);               // microfono hacia la radio
        pinMode(SA868_PD_PIN, OUTPUT);
        digitalWrite(SA868_PD_PIN, HIGH);                   // bajo = modulo apagado
        PMU.disableBLDO1();                                 // microfono apagado: no se mezcla con el audio AFSK
        SA_SERIAL.begin(9600, SERIAL_8N1, SA868_RX_PIN, SA868_TX_PIN);

        // banda: la pantalla de las T-TWR de UHF esta en 0x3D y la de VHF en 0x3C (libreria de LilyGO)
        Wire.beginTransmission(0x3D);
        isUhf = (Wire.endTransmission() == 0);

        const int forced = Config.sa868.revision;
        if (forced == 20) powerRev20(true);
        delay(forced == 20 ? 600 : 300);
        fw = detectFirmware(forced == 0 ? 2 : 6);
        if (forced != 0) rev = forced;
        else if (fw != Firmware::NONE) rev = 21;            // contesta sin el DC3: se alimenta de la bateria
        else {
            powerRev20(true);
            delay(600);
            fw = detectFirmware(6);
            rev = (fw != Firmware::NONE) ? 20 : 0;
        }
        #ifdef SA868_SQL_PIN
        if (rev == 21) pinMode(SA868_SQL_PIN, INPUT_PULLUP);
        #endif
        if (fw == Firmware::NONE) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SA868", "No answer from the radio module (UART %d/%d)", SA868_RX_PIN, SA868_TX_PIN);
            ok = false;
            return false;
        }
        ok = true;
        if (fw == Firmware::OPENRTX) {
            char ver[48] = "", model[32] = "";
            command("AT+VERSION", ver, sizeof(ver));
            command("AT+MODEL", model, sizeof(model));
            if (strstr(model, "UHF")) isUhf = true;
            if (strstr(model, "VHF")) isUhf = false;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SA868", "OpenRTX firmware %s, %s", ver, model);
            at1846Init();
        }
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SA868", "T-TWR Rev%d.%d, %s firmware, %s", rev / 10, rev % 10, firmwareName(), isUhf ? "UHF" : "VHF");
        fixBandDefaults();
        mode96 = (Config.operatingMode == MODE_APRS_FSK);
        m17    = (Config.operatingMode == MODE_M17);
        dap    = (Config.operatingMode == MODE_DAPNET);
        ais    = (Config.operatingMode == MODE_AIS);
        if (ais) aisCh = Config.ais.channel == 1 ? 'B' : 'A';
        fmMode = (Config.operatingMode == MODE_FM || Config.operatingMode == MODE_SAT);    // SAT: el VFO sigue al satelite (FM_Utils)
        cwMode = (Config.operatingMode == MODE_MORSE);
        flat   = mode96 || m17 || dap || ais;
        if (cwMode) espAudio = rev == 21;                   // Rev2.1: tono local y tono de la CW recibida por el altavoz
        if (fmMode) {
            PMU.setBLDO1Voltage(3300);                      // microfono de la placa encendido: voz
            PMU.enableBLDO1();
            espAudio = rev == 21 && (Config.fm.nrLevel > 0 || Config.fm.squelchAuto);
        }
        if (flat) logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SA868", "%s: flat audio, 25 kHz channel", modemName());
        if (m17) {
            m17Syms = (float*)heap_caps_malloc(M17M::maxPacketSymbols() * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!m17Syms) m17Syms = (float*)malloc(M17M::maxPacketSymbols() * sizeof(float));
        }
        applyConfig();
        if (!audioTxBegin()) logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SA868", "PDM audio output failed");
        if (!audioRxBegin()) logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SA868", "ADC audio input failed");
        if (espAudio) {
            if (speakerBegin()) logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SA868", "Speaker fed by the ESP32 (GPIO%d): %s%s", SA868_SPK_PIN,
                                           Config.fm.squelchAuto ? "auto squelch" : "squelch", Config.fm.nrLevel ? ", noise reduction" : "");
            else { espAudio = false; logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SA868", "Speaker output (GPIO%d) failed", SA868_SPK_PIN); }
            applySpeaker();
        }
        return true;
    }

    bool ready()              { return ok; }
    Firmware firmware()       { return fw; }
    int  revision()           { return rev; }
    bool uhf()                { return isUhf; }
    float audioLevel()        { return demod.level(); }
    uint32_t framesOk()       { return m17 ? m17rx.packets : (mode96 ? g3rx.frames : demod.frames); }
    bool mode9600()           { return mode96; }
    bool modeM17()            { return m17; }
    const char* modemName()   { return m17 ? "M17" : (dap ? "POCSAG" : ais ? "AIS" : (fmMode ? (Config.operatingMode == MODE_SAT ? "SAT FM" : "FM voice") : (cwMode ? (Config.sa868.cwMcw ? "CW (MCW)" : "CW (A1A)") : (mode96 ? "G3RUH 9600" : "AFSK 1200")))); }
    bool modeFm()             { return fmMode; }
    bool modeDapnet()         { return dap; }
    long rxFreqHz()           { return curRx ? curRx : Config.sa868.rxFreqHz; }
    long txFreqHz()           { return curTx ? curTx : Config.sa868.txFreqHz; }
    uint32_t m17Lsfs()        { return m17rx.lsfs; }
    uint32_t m17CrcErrors()   { return m17rx.crcErrors; }

    long m17FreqHz() {
        if (validFreq(Config.sa868.m17FreqHz)) return Config.sa868.m17FreqHz;
        return isUhf ? M17_UHF_HZ : M17_VHF_HZ;
    }

    const char* firmwareName() {
        switch (fw) {
            case Firmware::NICERF:  return "NiceRF";
            case Firmware::OPENRTX: return "OpenRTX";
            default:                return "none";
        }
    }

    // ---- Emision -----------------------------------------------------------------------------------------------------

    // Pulsa el PTT, saca el audio que da fill() (muestras a 48 kHz; 0 = fin) y vuelve a recepcion
    template <typename Fill> static void keyAndPlay(Fill fill) {
        if (txBlocked(txFreqHz())) return;                  // seguridad: nunca fuera de banda
        txActive = true;
        if (fw == Firmware::OPENRTX) {
            at1846Freq(txFreqHz());
            at1846Mode(2);
        }
        if (fmMode && rev == 21) PMU.disableBLDO1();        // Rev2.1: con MIC_CH_SEL en alto el microfono iria al ADC del ESP32
        digitalWrite(SA868_MIC_SEL_PIN, HIGH);              // el ESP32 hacia la entrada de microfono del modulo
        digitalWrite(SA868_PTT_PIN, LOW);
        i2s_channel_enable(txChan);
        static int16_t buf[512];
        const uint32_t start = millis();
        size_t n;
        while ((n = fill(buf, 512)) > 0) {
            size_t written = 0;
            i2s_channel_write(txChan, buf, n * sizeof(int16_t), &written, 200);
            if (millis() - start > TX_MAX_MS) break;
        }
        memset(buf, 0, sizeof(buf));                        // empuja el final por el DMA antes de soltar el PTT
        for (int i = 0; i < 6; i++) { size_t w; i2s_channel_write(txChan, buf, sizeof(buf), &w, 100); }

        digitalWrite(SA868_PTT_PIN, HIGH);
        i2s_channel_disable(txChan);
        digitalWrite(SA868_MIC_SEL_PIN, LOW);
        if (fmMode) PMU.enableBLDO1();
        if (fw == Firmware::OPENRTX) {
            at1846Freq(rxFreqHz());
            at1846Mode(1);
        }
        resetDemod = true;
        txActive = false;
    }

    // M17: preambulo, LSF, tramas de paquete y EOT. Delante, portadora sin modular (TXDELAY menos los 40 ms del preambulo)
    static bool m17Send(const char* src, const char* dst, const uint8_t* data, size_t len) {
        if (!ok || !txChan || !m17Syms) return false;
        const uint8_t can = (uint8_t)constrain(Config.sa868.m17Can, 0, 15);
        size_t ns = M17M::packetSymbols(src, dst, can, data, len, m17Syms, M17M::maxPacketSymbols());
        if (ns == 0 && currentBeacon) ns = M17M::packetSymbols(currentBeacon->callsign.c_str(), dst, can, data, len, m17Syms, M17M::maxPacketSymbols());
        if (ns == 0) return false;
        M17M::BasebandTx tx;
        tx.start(m17Syms, ns, constrain(Config.sa868.txLevelM17, 1, 100) / 100.0f);
        const int lead = Config.sa868.txDelayMs > 40 ? Config.sa868.txDelayMs - 40 : 0;
        size_t silence = (size_t)lead * (TX_FS / 1000);
        keyAndPlay([&](int16_t* out, size_t max) -> size_t {
            if (silence > 0) {
                const size_t k = silence < max ? silence : max;
                memset(out, 0, k * sizeof(int16_t));
                silence -= k;
                return k;
            }
            return tx.fill(out, max);
        });
        return true;
    }

    bool m17Sms(const char* dst, const char* text) {
        if (!m17 || !currentBeacon) return false;
        static uint8_t pk[M17M::MAX_PACKET];
        const size_t n = M17M::smsPacket(text, pk, sizeof(pk));
        if (n == 0) return false;
        char to[12] = "@ALL";
        if (dst && dst[0]) {
            size_t k = 0;
            for (; dst[k] && k + 1 < sizeof(to); k++) to[k] = (char)toupper((unsigned char)dst[k]);
            to[k] = 0;
        }
        return m17Send(currentBeacon->callsign.c_str(), to, pk, n);
    }

    bool transmit(const char* tnc2) {
        if (txBlocked(txFreqHz())) return false;           // fuera de la banda de aficionado: no se emite
        if (!ok || !txChan) return false;
        if (m17) {
            // APRS en un paquete M17 de protocolo APRS a "@ALL"; en el LSF, el origen de la trama (o el indicativo del perfil)
            static uint8_t pk[M17M::MAX_PACKET];
            const size_t n = M17M::tnc2ToPacket(tnc2, pk, sizeof(pk));
            if (n == 0) return false;
            char src[12] = "";
            const char* gt = strchr(tnc2, '>');
            if (gt && gt - tnc2 < (int)sizeof(src)) { memcpy(src, tnc2, gt - tnc2); src[gt - tnc2] = 0; }
            return m17Send(src, "@ALL", pk, n);
        }
        uint8_t frame[AX25G3::MAX_FRAME];
        const size_t fl = AX25G3::tnc2ToFrame(tnc2, frame, sizeof(frame));
        if (fl == 0) return false;
        static uint8_t bits[1400];                          // AFSK: bits de tono; G3RUH: bytes en el aire
        size_t nb = 0;
        if (mode96) {
            // una bandera son 8 bits a 9600 baudios: 0,83 ms. Protocolo del perfil FSK, como lora_utils.cpp en las demas placas:
            // 1..3 FX.25 (16 / 32 / 64), 4 IL2P; si no cabe, AX.25
            const size_t lead = (size_t)((Config.sa868.txDelayMs * G3RUH::BAUD + 7999) / 8000);
            const int fec = currentLoRaType ? currentLoRaType->fskFec : 0;
            if (fec >= 1 && fec <= 3) nb = AX25G3::g3ruhEncodeFx25(frame, fl, bits, sizeof(bits), lead, 16 << (fec - 1));
            if (fec == 4)             nb = AX25G3::g3ruhEncodeIl2p(frame, fl, bits, sizeof(bits), lead);
            if (nb == 0)              nb = AX25G3::g3ruhEncode(frame, fl, bits, sizeof(bits), lead);
        } else {
            // una bandera son 8 bits a 1200 baudios: 6,67 ms. Protocolo: sa868.fec (0 AX.25, 1..3 FX.25, 4 IL2P)
            const size_t lead = (size_t)((Config.sa868.txDelayMs * AFSK::BAUD + 7999) / 8000);
            const size_t tail = (size_t)((Config.sa868.txTailMs  * AFSK::BAUD + 7999) / 8000);
            nb = AFSK::encodeBits(frame, fl, bits, sizeof(bits), lead, tail ? tail : 1, Config.sa868.fec) / 8;
        }
        if (nb == 0) return false;

        if (mode96) {
            G3RUH::BaseTx g3tx;
            g3tx.start(bits, nb, TX_FS, constrain(Config.sa868.txLevel9600, 1, 100) / 100.0f);
            keyAndPlay([&](int16_t* out, size_t max) { return g3tx.fill(out, max); });
        } else {
            AFSK::ToneGen gen;
            gen.start(bits, nb * 8, TX_FS, constrain(Config.sa868.txLevel, 1, 100) / 100.0f);
            keyAndPlay([&](int16_t* out, size_t max) { return gen.fill(out, max); });
        }
        return true;
    }

    // ---- Portadora de prueba (calibracion de frecuencia) --------------------------------------------------------------

    bool carrier(bool on) {
        static bool keyed = false;
        if (on) {
            if (!ok || txActive || txBlocked(txFreqHz())) return false;
            txActive = true;
            if (fw == Firmware::OPENRTX) { at1846Freq(txFreqHz()); at1846Mode(2); }
            if (fmMode && rev == 21) PMU.disableBLDO1();
            digitalWrite(SA868_MIC_SEL_PIN, HIGH);          // entrada del ESP32 en silencio: portadora sin modular
            digitalWrite(SA868_PTT_PIN, LOW);
            keyed = true;
            return true;
        }
        if (!keyed) return false;
        digitalWrite(SA868_PTT_PIN, HIGH);
        digitalWrite(SA868_MIC_SEL_PIN, LOW);
        if (fmMode) PMU.enableBLDO1();
        if (fw == Firmware::OPENRTX) { at1846Freq(rxFreqHz()); at1846Mode(1); }
        keyed = false;
        resetDemod = true;
        txActive = false;
        return true;
    }

    // ---- CW (modo MORSE): telegrafia A1A o MCW en VHF / UHF ----------------------------------------------------------

    static bool cwChannel(long hz, bool high, int squelch) {
        FmCore::Channel c;
        FmCore::clearChannel(c, hz);
        c.toneMode = FmCore::TONE_OFF; c.wide = false; c.high = high; c.shift = 0;
        const bool good = fmApply(c, squelch);
        if (fw == Firmware::OPENRTX) {                      // valor del registro 0x30 en RX y en TX: un solo POKE por flanco
            const uint16_t v = (uint16_t)(peek(0x30) & ~0x0060);
            reg30Rx = (uint16_t)(v | (1 << 5)); reg30Tx = (uint16_t)(v | (2 << 5));
        }
        return good;
    }

    // frecuencia CW de la banda del modulo: la de la configuracion si cae dentro; si no, 144,050 (VHF) / 432,050 MHz (UHF)
    long cwFreqHz(bool beacon) {
        const long f = beacon ? Config.cw.beaconFreq : Config.cw.keyFreq;
        if (validFreq(f)) return f;
        return isUhf ? 432050000L : 144050000L;
    }

    bool cwSelect(bool beacon) {
        if (!ok) return false;
        const int p = beacon ? Config.cw.beaconPower : Config.cw.keyPower;
        return cwChannel(cwFreqHz(beacon), p > 31, Config.sa868.squelch);
    }

    static void mcwTaskFn(void*) {
        static int16_t b[256];
        float ph = 0;
        const float amp = constrain(Config.sa868.txLevel, 1, 100) / 100.0f * 32000.0f;
        const float step = 2.0f * (float)M_PI * constrain(Config.sa868.cwToneHz, 300, 1500) / (float)TX_FS;
        float env = 0;
        while (mcwRun) {
            for (int i = 0; i < 256; i++) {
                env += ((mcwTone ? 1.0f : 0.0f) - env) * 0.02f;     // flancos de ~1 ms: sin clics
                b[i] = (int16_t)(amp * env * sinf(ph));
                ph += step; if (ph > 2.0f * (float)M_PI) ph -= 2.0f * (float)M_PI;
            }
            size_t w = 0;
            i2s_channel_write(txChan, b, sizeof(b), &w, 100);
        }
        mcwTask = nullptr;
        vTaskDelete(nullptr);
    }

    // A1A: la portadora se enciende y se apaga (PTT con NiceRF; registro de modo con OpenRTX). MCW: PTT puesto toda la sesion y
    // tono de cwToneHz por el microfono del modulo
    void cwKey(bool on) {
        if (!ok) return;
        if (on && !cwSession && !cwKeyed && txBlocked(txFreqHz())) return;
        cwSidetone = on;
        if (Config.sa868.cwMcw) {
            if (on && !cwSession) {
                if (!txChan) return;
                cwSession = true; txActive = true;
                if (fw == Firmware::OPENRTX) poke(0x30, reg30Tx);
                digitalWrite(SA868_MIC_SEL_PIN, HIGH);
                digitalWrite(SA868_PTT_PIN, LOW);
                i2s_channel_enable(txChan);
                mcwRun = true;
                xTaskCreatePinnedToCore(mcwTaskFn, "mcw", 3072, nullptr, 6, &mcwTask, 1);
            }
            mcwTone = on;
            return;
        }
        if (on == cwKeyed) return;
        cwKeyed = on;
        if (on) {
            txActive = true;
            digitalWrite(SA868_MIC_SEL_PIN, HIGH);          // entrada del ESP32 en silencio: portadora pura
            if (fw == Firmware::OPENRTX) poke(0x30, reg30Tx);
            digitalWrite(SA868_PTT_PIN, LOW);
        } else {
            digitalWrite(SA868_PTT_PIN, HIGH);
            if (fw == Firmware::OPENRTX) poke(0x30, reg30Rx);
            txActive = false;
        }
    }

    void cwEnd() {
        cwSidetone = false;
        if (cwKeyed) cwKey(false);
        if (cwSession) {
            mcwTone = false; mcwRun = false;
            for (int i = 0; i < 40 && mcwTask; i++) delay(5);
            digitalWrite(SA868_PTT_PIN, HIGH);
            i2s_channel_disable(txChan);
            if (fw == Firmware::OPENRTX) poke(0x30, reg30Rx);
            cwSession = false; txActive = false;
        }
        digitalWrite(SA868_MIC_SEL_PIN, LOW);
        resetDemod = true;
    }

    // Escucha: frecuencia de Telegrafia con el silenciador del modulo abierto (se oye el soplido; la portadora lo calla)
    bool cwRxBegin() {
        if (!ok) return false;
        cwChannel(cwFreqHz(false), Config.cw.keyPower > 31, 0);
        cwDet.begin((float)RX_FS);
        cwLevel = 0;
        cwRx = true;
        return true;
    }

    float cwRxLevel() { return -120.0f + cwLevel; }         // dB por encima del "suelo" (soplido): el decodificador solo mira diferencias

    void cwRxEnd() {
        cwRx = false;
        cwChannel(cwFreqHz(false), Config.cw.keyPower > 31, Config.sa868.squelch);
    }

    // ---- Modo FM (voz) -----------------------------------------------------------------------------------------------

    // Subtono del AT+DMOSETGROUP de NiceRF: "0000" sin tono, "0001".."0038" CTCSS, "023N" / "023I" DCS
    static void niceRfSub(const FmCore::Channel& c, bool rx, char* out, size_t n) {
        snprintf(out, n, "0000");
        if (c.toneMode == FmCore::TONE_DCS) { snprintf(out, n, "%03u%c", (unsigned)c.dcs, c.dcsInv ? 'I' : 'N'); return; }
        const bool on = c.toneMode == FmCore::TONE_TSQL || (!rx && c.toneMode == FmCore::TONE_ENC);
        if (!on) return;
        const int code = FmCore::niceRfCtcss(c.tone);
        if (code > 0) snprintf(out, n, "%04d", code);
        else fmToneOk = false;
    }

    bool fmToneSupported(const FmCore::Channel& c) {
        if (c.toneMode == FmCore::TONE_DCS) return fw != Firmware::OPENRTX;                     // OpenRTX no programa el DCS
        if (c.toneMode == FmCore::TONE_ENC || c.toneMode == FmCore::TONE_TSQL) return fw == Firmware::OPENRTX || FmCore::niceRfCtcss(c.tone) > 0;
        return true;
    }

    bool fmApply(const FmCore::Channel& c, int squelch) {
        if (!ok) return false;
        const long rx = c.rxHz, tx = FmCore::txHz(c);
        if (!validFreq(rx) || !validFreq(tx)) return false;
        fmCh = c; fmLoaded = true;
        fmSquelch = constrain(squelch, 0, 8);
        fmToneOk = fmToneSupported(c);
        curRx = rx; curTx = tx;
        bool good = true;
        // con el ESP32 al altavoz y el silenciador automatico, el del modulo va al minimo (SQL de la Rev2.1 = portadora)
        const int moduleSql = (espAudio && Config.fm.squelchAuto) ? 1 : fmSquelch;
        if (fw == Firmware::OPENRTX) {
            at1846Bandwidth(c.wide);
            maskSet(0x58, 0x00F8, Config.sa868.filters ? 0x0000 : 0x00F8);
            maskSet(0x44, 0x00F0, (uint16_t)((constrain(Config.sa868.volume, 1, 8) * 15 / 8) << 4));
            at1846Freq(rx);
            // CTCSS (registros de OpenRTX, AT1846S.h): 0x4A tono de emision en centesimas de Hz, 0x4D el de recepcion, 0x5B umbral
            maskSet(0x4E, 0x0600, 0x0000);
            maskSet(0x3A, 0x001F, 0x0000);
            poke(0x4A, 0); poke(0x4D, 0);
            if (c.toneMode == FmCore::TONE_ENC || c.toneMode == FmCore::TONE_TSQL) {
                poke(0x4A, (uint16_t)(c.tone * 10)); poke(0x4B, 0); poke(0x4C, 0);
                maskSet(0x4E, 0x0600, 0x0600);
            }
            if (c.toneMode == FmCore::TONE_TSQL) {
                poke(0x4D, (uint16_t)(c.tone * 10));
                poke(0x5B, FmCore::ctcssThreshold(c.tone));
                maskSet(0x3A, 0x001F, 0x0008);
            }
            at1846Mode(1);
        } else {
            char txs[8], rxs[8], cmd[80], ft[16], fr[16];
            niceRfSub(c, false, txs, sizeof(txs));
            niceRfSub(c, true, rxs, sizeof(rxs));
            niceRfFreq(tx, ft, sizeof(ft));
            niceRfFreq(rx, fr, sizeof(fr));
            snprintf(cmd, sizeof(cmd), "AT+DMOSETGROUP=%d,%s,%s,%s,%d,%s", c.wide ? 1 : 0, ft, fr, txs, moduleSql, rxs);
            good = commandOk(cmd, "+DMOSETGROUP:0", 800);
            if (!good) logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SA868", "%s rejected", cmd);
            char v[40];
            snprintf(v, sizeof(v), "AT+DMOSETVOLUME=%d", constrain(Config.sa868.volume, 1, 8));
            commandOk(v, "+DMOSETVOLUME:0");
            const int f = Config.sa868.filters ? 0 : 1;
            snprintf(v, sizeof(v), "AT+SETFILTER=%d,%d,%d", f, f, f);
            commandOk(v, "+DMOSETFILTER:0");
        }
        applyPowerLevel(c.high);
        if (!fmToneOk) logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "FM", "%s not available with the %s firmware: sent without it",
                                   c.toneMode == FmCore::TONE_DCS ? "DCS" : "This CTCSS tone", firmwareName());
        return good;
    }

    // Doppler: solo las frecuencias (sin volumen, filtros ni potencia), para resintonizar cada pocos segundos sin cortes largos
    bool fmRetune(long rx, long tx) {
        if (!ok || !fmLoaded || txActive || !validFreq(rx) || !validFreq(tx)) return false;
        fmCh.rxHz = rx;
        if (tx != rx) { fmCh.shift = tx > rx ? 1 : -1; fmCh.offsetHz = labs(tx - rx); } else fmCh.shift = 0;
        curRx = rx; curTx = tx;
        if (fw == Firmware::OPENRTX) { at1846Freq(rx); return true; }
        char txs[8], rxs[8], cmd[80], ft[16], fr[16];
        niceRfSub(fmCh, false, txs, sizeof(txs));
        niceRfSub(fmCh, true, rxs, sizeof(rxs));
        niceRfFreq(tx, ft, sizeof(ft));
        niceRfFreq(rx, fr, sizeof(fr));
        const int moduleSql = (espAudio && Config.fm.squelchAuto) ? 1 : fmSquelch;
        snprintf(cmd, sizeof(cmd), "AT+DMOSETGROUP=%d,%s,%s,%s,%d,%s", fmCh.wide ? 1 : 0, ft, fr, txs, moduleSql, rxs);
        return commandOk(cmd, "+DMOSETGROUP:0", 800);
    }

    const FmCore::Channel& fmChannel() { return fmCh; }
    int  fmSquelchLevel() { return fmSquelch; }
    bool fmBusy() { return fmOpen; }
    int  fmRssi() { return fmRssiDbm; }
    bool fmTransmitting() { return txActive; }
    bool fmSpeakerFromEsp() { return espAudio; }
    bool fmLastToneOk() { return fmToneOk; }

    // Voz: microfono de la placa hacia el modulo y PTT. Con 1750 Hz, medio segundo de tono antes (apertura de repetidores antiguos)
    bool fmVoice(bool on) {
        if (!ok) return false;
        if (on) {
            if (txActive || txBlocked(curTx)) return false;
            if (fmCh.toneMode == FmCore::TONE_1750) {
                static const uint16_t hz[1] = { 1750 }, ms[1] = { 500 };
                fmPlayTones(hz, ms, 1);
            }
            txActive = true;
            if (fw == Firmware::OPENRTX) { at1846Freq(curTx); at1846Mode(2); }
            digitalWrite(SA868_MIC_SEL_PIN, LOW);
            digitalWrite(SA868_PTT_PIN, LOW);
            fmVoiceOn = true;
            return true;
        }
        if (!fmVoiceOn) return false;
        digitalWrite(SA868_PTT_PIN, HIGH);
        if (fw == Firmware::OPENRTX) { at1846Freq(curRx); at1846Mode(1); }
        fmVoiceOn = false;
        resetDemod = true;
        txActive = false;
        return true;
    }

    bool fmVoiceActive() { return fmVoiceOn; }

    // Tonos desde el ESP32 en el canal de voz (pitidos de la EAI, sondeo de ARTS, CW, 1750 Hz); hz = 0: portadora sin modular
    bool fmPlayTones(const uint16_t* hz, const uint16_t* ms, size_t n) {
        if (!ok || !txChan || txActive || n == 0 || txBlocked(txFreqHz())) return false;
        size_t seg = 0, left = (size_t)ms[0] * (TX_FS / 1000);
        float ph = 0;
        const float amp = constrain(Config.sa868.txLevel, 1, 100) / 100.0f * 32000.0f;
        keyAndPlay([&](int16_t* out, size_t max) -> size_t {
            size_t k = 0;
            while (k < max) {
                while (left == 0) { if (++seg >= n) return k; left = (size_t)ms[seg] * (TX_FS / 1000); ph = 0; }
                const float step = 2.0f * (float)M_PI * hz[seg] / (float)TX_FS;
                out[k++] = hz[seg] ? (int16_t)(amp * sinf(ph)) : 0;
                ph += step; if (ph > 2.0f * (float)M_PI) ph -= 2.0f * (float)M_PI;
                left--;
            }
            return k;
        });
        return true;
    }

    // Indicativo en CW (tono de 800 Hz por el microfono del modulo)
    bool fmCwId(const char* text, int wpm) {
        static uint16_t hz[400], ms[400];
        size_t n = 0;
        const uint16_t dot = (uint16_t)(1200 / constrain(wpm, 5, 40));
        hz[n] = 0; ms[n++] = 200;
        for (const char* p = text; *p && n + 20 < 400; p++) {
            if (*p == ' ') { hz[n] = 0; ms[n++] = (uint16_t)(dot * 4); continue; }
            const char* pat = CWMorse::pattern(*p);
            if (!pat) continue;
            for (const char* e = pat; *e; e++) {
                hz[n] = 800; ms[n++] = (uint16_t)(*e == '-' ? dot * 3 : dot);
                hz[n] = 0;   ms[n++] = dot;
            }
            ms[n - 1] = (uint16_t)(dot * 3);
        }
        return fmPlayTones(hz, ms, n);
    }

    // APRS en la frecuencia APRS (sa868.rxFreqHz / txFreqHz, sin tono) y vuelta al canal de voz
    bool fmAprsTransmit(const char* tnc2) {
        if (!ok || txActive) return false;
        const FmCore::Channel saved = fmCh;
        FmCore::Channel a;
        FmCore::clearChannel(a, Config.sa868.rxFreqHz);
        const long tx = Config.sa868.txFreqHz;
        if (tx != a.rxHz) { a.shift = tx > a.rxHz ? 1 : -1; a.offsetHz = labs(tx - a.rxHz); } else a.shift = 0;
        a.toneMode = FmCore::TONE_OFF; a.wide = Config.sa868.wide; a.high = Config.sa868.highPower;
        if (!fmApply(a, fmSquelch)) { fmApply(saved, fmSquelch); return false; }
        const bool sent = transmit(tnc2);
        fmApply(saved, fmSquelch);
        return sent;
    }

    // Cada 50 ms desde el bucle principal: RSSI y CTCSS (OpenRTX), estado del silenciador, altavoz, nivel automatico
    void fmPoll() {
        if (!fmMode || !ok || txActive) return;
        const uint32_t now = millis();
        static uint32_t last = 0;
        if (now - last < 50) return;
        last = now;
        bool open;
        if (fw == Firmware::OPENRTX) {
            fmRssiDbm = -137 + (int)(peek(0x1B) >> 8);
            fmCtcssOk = fmCh.toneMode != FmCore::TONE_TSQL || (peek(0x1C) & 0x0100) != 0;
            if (Config.fm.squelchAuto) open = fmSoftOpen;
            else open = fmSquelch == 0 || fmRssiDbm >= -128 + 4 * fmSquelch;                       // nivel 1 = -124 dBm ... 8 = -96 dBm
            open = open && fmCtcssOk;
            static bool spk = true;
            if (!espAudio && Config.sa868.speaker && open != spk) { spk = open; commandOk(open ? "AT+AUDIO=1" : "AT+AUDIO=0", "OK"); }
        } else {
            #ifdef SA868_SQL_PIN
            const bool carrier = rev == 21 ? digitalRead(SA868_SQL_PIN) == LOW : demod.level() > 0.02f;
            #else
            const bool carrier = demod.level() > 0.02f;
            #endif
            open = carrier && (!Config.fm.squelchAuto || !espAudio || fmSoftOpen);
            // Rev2.0 (sin ESP32 al altavoz): el automatico ajusta el nivel del modulo
            if (Config.fm.squelchAuto && !espAudio) {
                const int nl = fmAuto.levelUpdate(now, carrier, carrier && fmNoise.noise(), fmSquelch);
                if (nl >= 0 && nl != fmSquelch) { fmSquelch = nl; fmApply(fmCh, fmSquelch); }
            }
        }
        fmOpen = open;
    }

    // ---- Recepcion ---------------------------------------------------------------------------------------------------

    bool popFrame(String& tnc2, int& rssiOut) {
        if (!rxQueue) return false;
        RxItem item;
        if (xQueueReceive(rxQueue, &item, 0) != pdTRUE) return false;
        tnc2 = item.text;
        rssiOut = rssi();
        return true;
    }

    bool channelBusy() {
        #ifdef SA868_SQL_PIN
        if (rev == 21 && digitalRead(SA868_SQL_PIN) == LOW) return true;    // silenciador abierto: hay portadora
        #endif
        if (fmMode) return fmOpen || demod.dataCarrier();
        if (dap) return pocRx.carrier();
        return m17 ? m17rx.dataCarrier() : (mode96 ? g3rx.dataCarrier() : demod.dataCarrier());
    }

    int rssi() {
        if (fw != Firmware::OPENRTX) return 0;
        return -137 + (int)(peek(0x1B) >> 8);
    }

    void sleep() {
        if (adc) adc_continuous_stop(adc);
        digitalWrite(SA868_PD_PIN, LOW);
    }

    void wake() {
        digitalWrite(SA868_PD_PIN, HIGH);
        delay(300);
        if (fw == Firmware::OPENRTX) at1846Init();
        applyConfig();
        resetDemod = true;
        if (adc) adc_continuous_start(adc);
    }
}

#endif
