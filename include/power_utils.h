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

#ifndef POWER_UTILS_H_
#define POWER_UTILS_H_

#include <Arduino.h>
#include "board_pinout.h"
#if defined(HAS_AXP2101) || defined(HAS_AXP192)
    #include "XPowersLib.h"
#else
    #include <Wire.h>
#endif


namespace POWER_Utils {

    #ifdef ADC_CTRL
        void    adc_ctrl_ON();
        void    adc_ctrl_OFF();
    #endif

    #if defined(HAS_AXP192) || defined(HAS_AXP2101)
        String  getBatteryInfoCurrent();
        float   getBatteryChargeDischargeCurrent();
        void    handleChargingLed();
    #endif

    bool isCharging();
    float getVbusVoltage();         // V (0 si no hay alimentacion externa o no hay PMU)
    float getPmuTemperature();      // grados C del PMU (NAN si no esta disponible)

    void activateGPS();
    void deactivateGPS();

    void activateLoRa();
    void deactivateLoRa();

    void externalPinSetup();
    bool pinReserved(int pin);      // GPIO ocupado por la placa (T-Beam S3 Supreme); manipulador CW y teclado FH-2 lo ignoran

    bool begin(TwoWire &port);
    void setup();

    void lowerCpuFrequency();
    void shutdown();

}

#endif
