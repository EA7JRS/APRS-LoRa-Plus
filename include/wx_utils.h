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

#ifndef WX_UTILS_H_
#define WX_UTILS_H_

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_BME680.h>
#include <Arduino.h>

namespace WX_Utils {

    struct Reading {
        bool    valid;
        bool    hasHumidity;
        bool    hasPressure;
        bool    hasGas;
        float   tempC;
        float   humidity;           // %
        float   pressureHpa;        // a nivel del mar (si WX_IMPROVED) como en la telemetria
        float   gasKohm;
    };
    bool            getReading(Reading& reading);   // ultima lectura en cache; no toca el sensor
    void            refreshForScreen();             // lee el sensor si toca (respeta el intervalo)
    const char*     sensorName();

    void    setup();
    void    update();
    bool    getEnvironment(float& temperatureC, bool& hasTemperature, float& humidityPct, bool& hasHumidity, float& pressureHpa, bool& hasPressure);
    String  readDataSensor(const uint8_t type);

}

#endif