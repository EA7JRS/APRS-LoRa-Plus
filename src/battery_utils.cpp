/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
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

#include <Arduino.h>
#include "configuration.h"
#include "battery_utils.h"
#include "board_pinout.h"
#include "power_utils.h"
#include "display.h"
#include "battery_model.h"      // curva Li-ion y tendencia (Manuel - EA7JRS)


#ifdef ADC_CTRL
    uint32_t    adcCtrlTime         = 0;
    uint8_t     measuringState      = 0;
#endif

#ifdef HAS_AXP192
    extern XPowersAXP192 PMU;
#endif
#ifdef HAS_AXP2101
    extern XPowersAXP2101 PMU;
#endif

extern      Configuration           Config;
uint32_t    batteryMeasurmentTime   = 0;
int         averageReadings         = 20;

String      batteryVoltage          = "";
bool        batteryConnected      = false;

extern      String                  batteryChargeDischargeCurrent;

float       lora32BatReadingCorr    = 6.5; // % of correction to higher value to reflect the real battery voltage (adjust this to your needs)

// Tendencia de la bateria (Manuel - EA7JRS): una muestra por minuto para el estado sin PMU, el tiempo que queda y el ahorro
static BatteryModel::Trend  batteryTrend;
static uint32_t             batteryTrendMs  = 0;


namespace BATTERY_Utils {

    String getPercentVoltageBattery(float voltage) {
        int percent = (int)lroundf(BatteryModel::liionPercent(voltage / BATTERY_CELLS));     // curva Li-ion (antes lineal 3,0-4,2 V) (EA7JRS)
        if (percent < 0) percent = 0;
        return (percent < 100) ? (((percent < 10) ? "  ": " ") + String(percent)) : "100";
    }

    String getBatteryInfoVoltage() {
        return batteryVoltage;
    }

    float readBatteryVoltage() {
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            return (PMU.getBattVoltage() / 1000.0);
        #else
            #ifdef BATTERY_PIN
                #if defined(TTGO_T_BEAM_1W)     // calibrated ADC reading: raw analogRead() is too inaccurate at this divider ratio
                    uint32_t milliVoltsSum = 0;
                    analogReadMilliVolts(BATTERY_PIN);  // Dummy Read
                    delay(1);
                    for (int i = 0; i < averageReadings; i++) {
                        milliVoltsSum += analogReadMilliVolts(BATTERY_PIN);
                        delay(3);
                    }
                    return (milliVoltsSum / averageReadings) / 1000.0 * 3.0;   // 300k + 150k divider (150k on the low side)
                #else
                    int sampleSum = 0;
                    analogRead(BATTERY_PIN);    // Dummy Read
                    delay(1);
                    for (int i = 0; i < averageReadings; i++) {
                        sampleSum += analogRead(BATTERY_PIN);
                        delay(3);
                    }
                    int adc_value = sampleSum/averageReadings;
                    double voltage = (adc_value * 3.3 ) / 4095.0;

                    #ifdef LIGHTTRACKER_PLUS_1_0
                        double inputDivider = (1.0 / (560.0 + 100.0)) * 100.0;  // The voltage divider is a 560k + 100k resistor in series, 100k on the low side.
                        return ((voltage / inputDivider) * 1.11029) + 0.14431;
                    #endif
                    #if defined(TTGO_T_Beam_V0_7) || defined(TTGO_LORA32_V2_1_GPS) || defined(TTGO_LORA32_V2_1_915_GPS) || defined(TTGO_LORA32_V2_1_TNC) || defined(TTGO_LORA32_V2_1_915_TNC) || defined(ESP32_DIY_LoRa_SX1278_GPS) || defined(ESP32_DIY_LoRa_915_SX1276_GPS) || defined(ESP32_DIY_1W_LoRa_E22_400M30S_GPS) || defined(ESP32_DIY_1W_LoRa_915_E22_900M30S_GPS) || defined(ESP32_DIY_1W_LoRa_E220_400M30S_GPS) || defined(OE5HWN_MeshCom) || defined(TTGO_T_DECK_GPS) || defined(TTGO_T_DECK_PLUS) || defined(ESP32S3_DIY_LoRa_SX1278_GPS) || defined(ESP32S3_DIY_LoRa_915_SX1276_GPS) || defined(TROY_LoRa_APRS) || defined(RPC_Electronics_1W_LoRa_GPS) || defined(TTGO_LORA32_T3S3_V1_2_GPS)
                        return (2 * (voltage + 0.1)) * (1 + (lora32BatReadingCorr/100)); // (2 x 100k voltage divider) 2 x voltage divider/+0.1 because ESP32 nonlinearity ~100mV ADC offset/extra correction
                    #endif
                    #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC) || defined(HELTEC_WIRELESS_TRACKER) || defined(HELTEC_WSL_V3_GPS_DISPLAY) || defined(HELTEC_WSL_V3_TNC) || defined(ESP32C3_SuperMini_DIY_LoRa_SX1278_GPS) || defined(ESP32C3_SuperMini_DIY_LoRa_915_SX1276_GPS) || defined(WEMOS_ESP32_Bat_DIY_LoRa_SX1278_GPS)
                        double inputDivider = (1.0 / (390.0 + 100.0)) * 100.0;  // The voltage divider is a 390k + 100k resistor in series, 100k on the low side.
                        return (voltage / inputDivider) + 0.285; // Yes, this offset is excessive, but the ADC on the ESP32s3 is quite inaccurate and noisy. Adjust to own measurements.
                    #endif
                    #if defined(HELTEC_V2_GPS) || defined(HELTEC_V2_GPS_915) || defined(HELTEC_V2_TNC) || defined(F4GOH_1W_LoRa_Tracker_E22_400M30S) || defined(F4GOH_1W_LoRa_Tracker_E220_400M30S)
                        double inputDivider = (1.0 / (220.0 + 100.0)) * 100.0;  // The voltage divider is a 220k + 100k resistor in series, 100k on the low side.
                        return (voltage / inputDivider) + 0.285; // Yes, this offset is excessive, but the ADC on the ESP32 is quite inaccurate and noisy. Adjust to own measurements.
                    #endif
                    #if defined(Hydra_1W_LoRa_E22_400M30S_GPS)
                        double inputDivider = (1.0 / (470.0 + 680.0)) * 680.0;  // The voltage divider is a 470k + 680k resistor in series, 680k on the low side.
                        return (voltage / inputDivider) + 0.285; // Yes, this offset is excessive, but the ADC on the ESP32 is quite inaccurate and noisy. Adjust to own measurements.
                    #endif
                #endif  // TTGO_T_BEAM_1W
            #else
                return 0.0;
            #endif
        #endif
    }

    void obtainBatteryInfo() {
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            batteryConnected = PMU.isBatteryConnect();
            if (batteryConnected) {
                batteryVoltage                  = String(readBatteryVoltage(), 2);
                batteryChargeDischargeCurrent   = String(POWER_Utils::getBatteryChargeDischargeCurrent(), 0);
            }
        #else
            batteryVoltage = String(readBatteryVoltage(), 2);
            batteryConnected = batteryVoltage.toFloat() > 1.5;
        #endif
        // una muestra por minuto para la tendencia (EA7JRS)
        if (batteryConnected && (batteryTrendMs == 0 || millis() - batteryTrendMs >= 60000UL)) {
            batteryTrendMs = millis();
            batteryTrend.add(millis() / 1000UL, batteryVoltage.toFloat() / BATTERY_CELLS);
        }
    }

    // ---------- Estado, tiempo que queda y ahorro automatico (Manuel - EA7JRS) ----------
    float cellVoltage() {
        return batteryConnected ? batteryVoltage.toFloat() / BATTERY_CELLS : 0.0f;
    }

    float percent() {
        return batteryConnected ? BatteryModel::liionPercent(cellVoltage()) : -1.0f;
    }

    bool trend(float& voltsPerHour, float& percentPerHour) {
        return batteryTrend.slope(millis() / 1000UL, 30UL * 60UL, 10UL * 60UL, voltsPerHour, percentPerHour);    // ultima media hora, al menos 10 min
    }

    uint8_t state() {
        if (!batteryConnected) return BatteryModel::ST_NONE;
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            if (PMU.isCharging()) return BatteryModel::ST_CHARGING;
            if (PMU.isVbusIn()) return BatteryModel::ST_FULL;
            return BatteryModel::ST_DISCHARGING;
        #else
            float vph = 0, pph = 0;
            const bool have = trend(vph, pph);
            return BatteryModel::classify(true, cellVoltage(), have, vph);
        #endif
    }

    float hoursLeft() {
        float vph = 0, pph = 0;
        if (!trend(vph, pph)) return -1.0f;
        return BatteryModel::hoursLeft((BatteryModel::State)state(), percent(), pph);
    }

    // Ahorro: descargando y por debajo de Config.battery.saveBelow (%): la pantalla se apaga sola (como el modo eco)
    bool saving() {
        if (Config.battery.saveBelow == 0 || !batteryConnected) return false;
        if (state() != BatteryModel::ST_DISCHARGING) return false;
        return percent() < Config.battery.saveBelow;
    }

    void monitor() {
        #if defined(HAS_AXP192) || defined(HAS_AXP2101)
            if (batteryMeasurmentTime == 0 || (millis() - batteryMeasurmentTime) > 1 * 1000){
                obtainBatteryInfo();
                POWER_Utils::handleChargingLed();
                batteryMeasurmentTime = millis();
            }
        #elif defined(BATTERY_PIN)
            if (batteryMeasurmentTime == 0 || (millis() - batteryMeasurmentTime) > 30 * 1000){ //At least 30 seconds have to pass between measurements
                #ifdef ADC_CTRL
                    switch(measuringState){
                        case 0:     // Initial Measurement
                            POWER_Utils::adc_ctrl_ON();
                            adcCtrlTime = millis();
                            delay(50);
                            obtainBatteryInfo();
                            POWER_Utils::adc_ctrl_OFF();
                            measuringState = 1;
                            break;
                        case 1:     //ADC_CTRL_ON State
                            POWER_Utils::adc_ctrl_ON();
                            adcCtrlTime = millis();
                            measuringState = 2;
                            break;
                        case 2:     // Measurement State
                            if((millis() - adcCtrlTime) > 50){ //At least 50ms have to pass after ADC_Ctrl Mosfet is turned on for voltage to stabilize
                                obtainBatteryInfo();
                                POWER_Utils::adc_ctrl_OFF();
                                measuringState = 1;

                                if (batteryVoltage.toFloat() < (Config.battery.sleepVoltage - 0.1) * BATTERY_CELLS) {
                                    displayShow("!BATTERY!", "", "LOW BATTERY VOLTAGE!",5000);
                                    POWER_Utils::shutdown();
                                }
                            }
                            break;
                    }
                #else
                    obtainBatteryInfo();
                #endif
                batteryMeasurmentTime = millis();
            }
        #endif
    }

}