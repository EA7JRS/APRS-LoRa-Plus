/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

#include <ArduinoJson.h>
#include "board_pinout.h"
#include "board_pins.h"
#include "power_utils.h"

namespace BOARD_PINS {

    // state: 'u' = used by the board / chip (the firmware refuses it), 'a' = usable with care (strapping, console UART, PSRAM), 'i' = input only
    struct Entry { int8_t pin; char st; const char* txt; };
    static const int MAX_ENTRIES = 64;
    static Entry  entries[MAX_ENTRIES];
    static int    nEntries = 0;
    static bool   built    = false;

    static void add(int pin, char st, const char* txt) {
        if (pin < 0 || pin > 60 || nEntries >= MAX_ENTRIES) return;
        for (int i = 0; i < nEntries; i++) {
            if (entries[i].pin == pin) {
                if (st == 'u' && entries[i].st != 'u') { entries[i].st = 'u'; entries[i].txt = txt; }     // the board wins over the chip hint
                return;
            }
        }
        entries[nEntries++] = { (int8_t)pin, st, txt };
    }

    static void build() {
        if (built) return;
        built = true;

        // ---- chip: flash, USB, console, strapping ----
        #if defined(CONFIG_IDF_TARGET_ESP32S3)
            for (int g = 26; g <= 32; g++) add(g, 'u', "Flash / PSRAM");
            add(19, 'u', "USB D-");
            add(20, 'u', "USB D+");
            add(0,  'a', "Strapping pin (BOOT)");
            add(3,  'a', "Strapping pin (JTAG)");
            add(45, 'a', "Strapping pin: only with a pull-down or a contact to GND");
            add(46, 'a', "Strapping pin: only with a pull-down or a contact to GND");
            add(43, 'a', "UART0 TX (console)");
            add(44, 'a', "UART0 RX (console)");
            for (int g = 33; g <= 37; g++) add(g, 'a', "Octal PSRAM on some modules (N8R8 / N16R8)");
        #elif defined(CONFIG_IDF_TARGET_ESP32C3)
            for (int g = 11; g <= 17; g++) add(g, 'u', "Flash");
            add(18, 'u', "USB D-");
            add(19, 'u', "USB D+");
            add(2,  'a', "Strapping pin");
            add(8,  'a', "Strapping pin (also the on-board LED on many boards)");
            add(9,  'a', "Strapping pin (BOOT button)");
            add(20, 'a', "UART0 RX (console)");
            add(21, 'a', "UART0 TX (console)");
        #else
            for (int g = 6; g <= 11; g++) add(g, 'u', "Flash");
            add(1,  'a', "UART0 TX (USB serial / log)");
            add(3,  'a', "UART0 RX (USB serial)");
            add(0,  'a', "Strapping pin (BOOT)");
            add(2,  'a', "Strapping pin (on-board LED on many boards)");
            add(12, 'a', "Strapping pin (flash voltage): must be low at boot");
            add(15, 'a', "Strapping pin (boot messages)");
            add(5,  'a', "Strapping pin");
            add(16, 'a', "PSRAM on WROVER modules");
            add(17, 'a', "PSRAM on WROVER modules");
            for (int g = 34; g <= 39; g++) add(g, 'i', "Input only: no output, no pull-up");
        #endif

        // ---- board: everything board_pinout.h declares ----
        #ifdef RADIO_SCLK_PIN
            add(RADIO_SCLK_PIN, 'u', "LoRa SCLK");
        #endif
        #ifdef RADIO_MISO_PIN
            add(RADIO_MISO_PIN, 'u', "LoRa MISO");
        #endif
        #ifdef RADIO_MOSI_PIN
            add(RADIO_MOSI_PIN, 'u', "LoRa MOSI");
        #endif
        #ifdef RADIO_CS_PIN
            add(RADIO_CS_PIN, 'u', "LoRa CS");
        #endif
        #ifdef RADIO_RST_PIN
            add(RADIO_RST_PIN, 'u', "LoRa RESET");
        #endif
        #ifdef RADIO_BUSY_PIN
            add(RADIO_BUSY_PIN, 'u', "LoRa BUSY");
        #endif
        #ifdef RADIO_DIO1_PIN
            add(RADIO_DIO1_PIN, 'u', "LoRa DIO1");
        #endif
        #ifdef RADIO_DIO0_PIN
            add(RADIO_DIO0_PIN, 'u', "LoRa DIO0");
        #endif
        #ifdef RADIO_RXEN
            add(RADIO_RXEN, 'u', "LoRa RX enable");
        #endif
        #ifdef RADIO_TXEN
            add(RADIO_TXEN, 'u', "LoRa TX enable");
        #endif
        #ifdef RADIO_VCC_PIN
            add(RADIO_VCC_PIN, 'u', "LoRa power enable");
        #endif
        #ifdef OLED_SDA
            add(OLED_SDA, 'u', "I2C SDA (display)");
        #endif
        #ifdef OLED_SCL
            add(OLED_SCL, 'u', "I2C SCL (display)");
        #endif
        #ifdef OLED_RST
            add(OLED_RST, 'u', "Display reset");
        #endif
        #ifdef BOARD_I2C_SDA
            add(BOARD_I2C_SDA, 'u', "I2C SDA (board)");
        #endif
        #ifdef BOARD_I2C_SCL
            add(BOARD_I2C_SCL, 'u', "I2C SCL (board)");
        #endif
        #ifdef GPS_RX
            add(GPS_RX, 'u', "GPS UART RX");
        #endif
        #ifdef GPS_TX
            add(GPS_TX, 'u', "GPS UART TX");
        #endif
        #ifdef GPS_PPS
            add(GPS_PPS, 'u', "GPS PPS");
        #endif
        #ifdef GPS_VCC
            add(GPS_VCC, 'u', "GPS power enable");
        #endif
        #ifdef BUTTON_PIN
            add(BUTTON_PIN, 'u', "User button");
        #endif
        #ifdef BUTTON2_PIN
            add(BUTTON2_PIN, 'u', "Button 2");
        #endif
        #ifdef BUTTON3_PIN
            add(BUTTON3_PIN, 'u', "Button 3");
        #endif
        #ifdef BUTTON4_PIN
            add(BUTTON4_PIN, 'u', "Button 4");
        #endif
        #ifdef BUTTON_AP_PIN
            add(BUTTON_AP_PIN, 'u', "Button (WiFi AP)");
        #endif
        #ifdef BATTERY_PIN
            add(BATTERY_PIN, 'u', "Battery voltage (ADC)");
        #endif
        #ifdef ADC_CTRL
            add(ADC_CTRL, 'u', "Battery divider enable");
        #endif
        #ifdef VEXT_CTRL
            add(VEXT_CTRL, 'u', "Vext power control");
        #endif
        #ifdef BOARD_BL_PIN
            add(BOARD_BL_PIN, 'u', "Display backlight");
        #endif
        #ifdef BOARD_POWERON
            add(BOARD_POWERON, 'u', "Board power on");
        #endif
        #ifdef BOARD_SDCARD_CS
            add(BOARD_SDCARD_CS, 'u', "SD card CS");
        #endif
        #ifdef SD_CS_PIN
            add(SD_CS_PIN, 'u', "SD card CS");
        #endif
        #ifdef RTC_INT_PIN
            add(RTC_INT_PIN, 'u', "RTC interrupt");
        #endif
        #ifdef TEMP_PIN
            add(TEMP_PIN, 'u', "Temperature sensor (NTC)");
        #endif
        #ifdef FAN_CTRL_PIN
            add(FAN_CTRL_PIN, 'u', "Fan control");
        #endif
        #ifdef JOYSTICK_UP
            add(JOYSTICK_UP, 'u', "Joystick up");
        #endif
        #ifdef JOYSTICK_DOWN
            add(JOYSTICK_DOWN, 'u', "Joystick down");
        #endif
        #ifdef JOYSTICK_LEFT
            add(JOYSTICK_LEFT, 'u', "Joystick left");
        #endif
        #ifdef JOYSTICK_RIGHT
            add(JOYSTICK_RIGHT, 'u', "Joystick right");
        #endif
        #ifdef HAS_SA868                                    // LilyGO T-TWR Plus: radio FM SA868, encoder, LED y PMU
            add(SA868_TX_PIN, 'u', "SA868 UART TX");
            add(SA868_RX_PIN, 'u', "SA868 UART RX");
            add(SA868_PTT_PIN, 'u', "SA868 PTT");
            add(SA868_PD_PIN, 'u', "SA868 power down");
            add(SA868_HL_PIN, 'u', "SA868 high/low power (Rev2.0)");
            add(SA868_SQL_PIN, 'u', "SA868 squelch (Rev2.1)");
            add(SA868_MIC_SEL_PIN, 'u', "Microphone switch");
            add(SA868_AUDIO_OUT_PIN, 'u', "AFSK audio out (PDM)");
            add(SA868_AUDIO_IN_PIN, 'u', "AFSK audio in (ADC)");
            add(TTWR_ENCODER_A, 'u', "Encoder A");
            add(TTWR_ENCODER_B, 'u', "Encoder B");
            add(TTWR_PIXEL_PIN, 'u', "WS2812 LED");
            add(PMU_IRQ_PIN, 'u', "PMU interrupt");
            add(11, 'u', "TF MOSI"); add(12, 'u', "TF SCK"); add(13, 'u', "TF MISO");
            add(15, 'u', "Microphone ADC (Rev2.1)"); add(45, 'u', "Tone to amplifier (Rev2.1)");
            for (int g = 33; g <= 37; g++) add(g, 'u', "PSRAM (octal)");
        #endif
        #ifdef DAC_I2S_WS
            add(DAC_I2S_WS, 'u', "I2S audio WS");
        #endif
        #ifdef DAC_I2S_DOUT
            add(DAC_I2S_DOUT, 'u', "I2S audio DOUT");
        #endif
        #ifdef DAC_I2S_BCK
            add(DAC_I2S_BCK, 'u', "I2S audio BCK");
        #endif
        #ifdef IMU_SCK_PIN
            add(IMU_SCK_PIN, 'u', "IMU SCK");
        #endif
        #ifdef IMU_MISO_PIN
            add(IMU_MISO_PIN, 'u', "IMU MISO");
        #endif
        #ifdef IMU_MOSI_PIN
            add(IMU_MOSI_PIN, 'u', "IMU MOSI");
        #endif
        #ifdef IMU_CS_PIN
            add(IMU_CS_PIN, 'u', "IMU CS");
        #endif
        #ifdef IMU_INT_PIN
            add(IMU_INT_PIN, 'u', "IMU interrupt");
        #endif
        #ifdef LED_TX
            add(LED_TX, 'u', "Board Tx LED");
        #endif
        #ifdef LED_RX
            add(LED_RX, 'u', "Board Rx LED");
        #endif
        #ifdef LED_BT
            add(LED_BT, 'u', "Board Bluetooth LED");
        #endif
        #ifdef LED_WIFI
            add(LED_WIFI, 'u', "Board WiFi LED");
        #endif
    }

    bool inputOnly(int pin) {
        #if !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32C3)
            return pin >= 34 && pin <= 39;
        #else
            (void)pin;
            return false;
        #endif
    }

    bool reserved(int pin) {
        build();
        for (int i = 0; i < nEntries; i++) {
            if (entries[i].pin == pin) return entries[i].st == 'u';
        }
        return false;
    }

    String json() {
        build();
        JsonDocument doc;
        #if defined(CONFIG_IDF_TARGET_ESP32S3)
            doc["chip"] = "esp32s3"; doc["lo"] = 0; doc["hi"] = 48;
            JsonArray skip = doc["skip"].to<JsonArray>();
            for (int g = 22; g <= 25; g++) skip.add(g);
            JsonArray adc = doc["adc1"].to<JsonArray>(); adc.add(1); adc.add(10);
        #elif defined(CONFIG_IDF_TARGET_ESP32C3)
            doc["chip"] = "esp32c3"; doc["lo"] = 0; doc["hi"] = 21;
            doc["skip"].to<JsonArray>();
            JsonArray adc = doc["adc1"].to<JsonArray>(); adc.add(0); adc.add(4);
        #else
            doc["chip"] = "esp32"; doc["lo"] = 0; doc["hi"] = 39;
            JsonArray skip = doc["skip"].to<JsonArray>();
            skip.add(20); skip.add(24); skip.add(28); skip.add(29); skip.add(30); skip.add(31);
            JsonArray adc = doc["adc1"].to<JsonArray>(); adc.add(32); adc.add(39);
        #endif
        JsonObject pins = doc["pins"].to<JsonObject>();
        for (int i = 0; i < nEntries; i++) {
            JsonArray e = pins[String((int)entries[i].pin)].to<JsonArray>();
            e.add(String((char)entries[i].st));
            e.add(entries[i].txt);
        }
        String out;
        serializeJson(doc, out);
        return out;
    }

}
