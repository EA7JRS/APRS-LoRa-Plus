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
#include "iaru_plan.h"
#include "sa868_radio.h"
#include "logger.h"

extern Configuration    Config;
extern logging::Logger  logger;
extern XPowersAXP2101   PMU;            // power_utils.cpp
extern LoraType         *currentLoRaType;

#if ARDUINO_USB_CDC_ON_BOOT
    #define SA_SERIAL Serial0           // UART0 libre: el registro va por el USB nativo
#else
    static HardwareSerial saSerialPort(2);
    #define SA_SERIAL saSerialPort
#endif

namespace SA868 {

    static constexpr uint32_t RX_FS      = 13200;           // AFSK 1200: 11 muestras por bit
    static constexpr uint32_t RX_FS_9600 = 48000;           // G3RUH 9600: 5 muestras por bit
    static constexpr uint32_t TX_FS      = 48000;
    static constexpr size_t   RX_QUEUE   = 4;
    static constexpr uint32_t TX_MAX_MS  = 6000;            // tope de seguridad de una emision

    struct RxItem { char text[AX25G3::MAX_FRAME + 120]; };

    static Firmware             fw          = Firmware::NONE;
    static int                  rev         = 0;
    static bool                 isUhf       = false;
    static bool                 ok          = false;
    static volatile bool        txActive    = false;
    static volatile bool        resetDemod  = false;
    static AFSK::Demod          demod;
    static G3RUH::BaseRx        g3rx;
    static bool                 mode96      = false;        // modo APRS FSK 9600 (G3RUH): audio plano, canal de 25 kHz
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

    static void at1846Freq(long hz) {
        const uint32_t v = (uint32_t)(((uint64_t)hz * 16) / 1000);
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
        if (rev == 21) {                                    // ALDO3: 0 = el amplificador lo gobierna la radio, 1 = el ESP32 (silencio)
            PMU.setALDO3Voltage(3300);
            if (Config.sa868.speaker) PMU.disableALDO3(); else PMU.enableALDO3();
        }
        if (fw == Firmware::OPENRTX) commandOk(Config.sa868.speaker ? "AT+AUDIO=1" : "AT+AUDIO=0", "OK");
    }

    static void applyPower() {
        #ifdef SA868_HL_PIN
        if (rev == 20) {                                    // H/L del modulo: alto = potencia alta
            pinMode(SA868_HL_PIN, OUTPUT);
            digitalWrite(SA868_HL_PIN, Config.sa868.highPower ? HIGH : LOW);
        }
        #endif
    }

    bool validFreq(long hz) {
        return isUhf ? (hz >= 400000000L && hz <= 480000000L) : (hz >= 134000000L && hz <= 174000000L);
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
        if (fw == Firmware::OPENRTX) {
            at1846Freq(rxHz);
            at1846Mode(1);
            return true;
        }
        char c[64];
        snprintf(c, sizeof(c), "AT+DMOSETGROUP=%d,%03ld.%04ld,%03ld.%04ld,0000,%d,0000", (Config.sa868.wide || mode96) ? 1 : 0,
                 txHz / 1000000L, (txHz % 1000000L) / 100L, rxHz / 1000000L, (rxHz % 1000000L) / 100L, constrain(Config.sa868.squelch, 0, 8));
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
        if (!validFreq(rx)) rx = isUhf ? 432500000L : 144800000L;           // no deberia pasar: fixBandDefaults() al arrancar
        if (!validFreq(tx)) tx = rx;
        if (fw == Firmware::OPENRTX) {
            if (mode96) {
                // 9600: como el M17 de OpenRTX en esta placa (radio_setOpmode): registros del modo DMR (sin filtros de voz ni
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
            if (!mode96) maskSet(0x44, 0x00F0, (uint16_t)((constrain(Config.sa868.volume, 1, 8) * 15 / 8) << 4));    // ganancia del DAC de audio
            good = setFrequencies(rx, tx);
        } else {
            good = setFrequencies(rx, tx);
            char c[40];
            snprintf(c, sizeof(c), "AT+DMOSETVOLUME=%d", constrain(Config.sa868.volume, 1, 8));
            good &= commandOk(c, "+DMOSETVOLUME:0");
            const int f = (Config.sa868.filters && !mode96) ? 0 : 1;          // 9600: los tres filtros puenteados (1 = fuera)
            snprintf(c, sizeof(c), "AT+SETFILTER=%d,%d,%d", f, f, f);
            good &= commandOk(c, "+DMOSETFILTER:0");
        }
        applyPower();
        applySpeaker();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SA868", "RX %.4f / TX %.4f MHz, %s, SQ %d, vol %d, %s power: %s",
                   rx / 1e6, tx / 1e6, (Config.sa868.wide || mode96) ? (mode96 ? "25 kHz, 9600 flat audio" : "25 kHz") : "12.5 kHz", Config.sa868.squelch, Config.sa868.volume,
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

    static void rxTaskFn(void*) {
        static uint8_t buf[256 * SOC_ADC_DIGI_RESULT_BYTES];
        for (;;) {
            uint32_t got = 0;
            if (adc_continuous_read(adc, buf, sizeof(buf), &got, 100) != ESP_OK) continue;
            if (resetDemod) { if (mode96) g3rx.reset(); else demod.reset(); resetDemod = false; }
            if (txActive) continue;                         // en emision el ADC oye la propia senal: se descarta
            for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= got; i += SOC_ADC_DIGI_RESULT_BYTES) {
                const adc_digi_output_data_t* p = (const adc_digi_output_data_t*)&buf[i];
                const int v = (int)p->type2.data;
                const float x = (float)(v - 2048) / 2048.0f;
                if (mode96) g3rx.feed(x, onFrame, nullptr); else demod.feed(x, onFrame, nullptr);
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
        cfg.sample_freq_hz = mode96 ? RX_FS_9600 : RX_FS;
        cfg.conv_mode      = ADC_CONV_SINGLE_UNIT_1;
        cfg.format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
        if (adc_continuous_config(adc, &cfg) != ESP_OK) return false;
        if (mode96 ? !g3rx.begin(RX_FS_9600) : !demod.begin(RX_FS)) return false;
        rxQueue = xQueueCreate(RX_QUEUE, sizeof(RxItem));
        if (!rxQueue) return false;
        if (adc_continuous_start(adc) != ESP_OK) return false;
        return xTaskCreatePinnedToCore(rxTaskFn, "afskRx", 6144, nullptr, 5, &rxTask, 0) == pdPASS;
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
        if (mode96) logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "SA868", "APRS FSK 9600 (G3RUH): flat audio, 25 kHz channel");
        applyConfig();
        if (!audioTxBegin()) logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SA868", "PDM audio output failed");
        if (!audioRxBegin()) logger.log(logging::LoggerLevel::LOGGER_LEVEL_ERROR, "SA868", "ADC audio input failed");
        return true;
    }

    bool ready()              { return ok; }
    Firmware firmware()       { return fw; }
    int  revision()           { return rev; }
    bool uhf()                { return isUhf; }
    float audioLevel()        { return demod.level(); }
    uint32_t framesOk()       { return mode96 ? g3rx.frames : demod.frames; }
    bool mode9600()           { return mode96; }

    const char* firmwareName() {
        switch (fw) {
            case Firmware::NICERF:  return "NiceRF";
            case Firmware::OPENRTX: return "OpenRTX";
            default:                return "none";
        }
    }

    // ---- Emision -----------------------------------------------------------------------------------------------------

    bool transmit(const char* tnc2) {
        if (!ok || !txChan) return false;
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

        txActive = true;
        if (fw == Firmware::OPENRTX) {
            at1846Freq(Config.sa868.txFreqHz);
            at1846Mode(2);
        }
        digitalWrite(SA868_MIC_SEL_PIN, HIGH);              // el ESP32 hacia la entrada de microfono del modulo
        digitalWrite(SA868_PTT_PIN, LOW);
        i2s_channel_enable(txChan);

        AFSK::ToneGen gen;
        G3RUH::BaseTx g3tx;
        if (mode96) g3tx.start(bits, nb, TX_FS, constrain(Config.sa868.txLevel9600, 1, 100) / 100.0f);
        else        gen.start(bits, nb * 8, TX_FS, constrain(Config.sa868.txLevel, 1, 100) / 100.0f);
        static int16_t buf[512];
        const uint32_t start = millis();
        size_t n;
        while ((n = mode96 ? g3tx.fill(buf, 512) : gen.fill(buf, 512)) > 0) {
            size_t written = 0;
            i2s_channel_write(txChan, buf, n * sizeof(int16_t), &written, 200);
            if (millis() - start > TX_MAX_MS) break;
        }
        memset(buf, 0, sizeof(buf));                        // empuja el final por el DMA antes de soltar el PTT
        for (int i = 0; i < 6; i++) { size_t w; i2s_channel_write(txChan, buf, sizeof(buf), &w, 100); }

        digitalWrite(SA868_PTT_PIN, HIGH);
        i2s_channel_disable(txChan);
        digitalWrite(SA868_MIC_SEL_PIN, LOW);
        if (fw == Firmware::OPENRTX) {
            at1846Freq(Config.sa868.rxFreqHz);
            at1846Mode(1);
        }
        resetDemod = true;
        txActive = false;
        return true;
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
        return mode96 ? g3rx.dataCarrier() : demod.dataCarrier();
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
