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

#include <TinyGPS++.h>
#include <logger.h>
#ifdef LIGHTTRACKER_PLUS_1_0
    #include "Adafruit_SHTC3.h"
#endif
#include "telemetry_utils.h"
#include "configuration.h"
#include "wx_utils.h"
#include "display.h"
#include "gps_utils.h"
#include "baro_utils.h"

#define SEALEVELPRESSURE_HPA (1013.25)
#define CORRECTION_FACTOR (8.2296)      // for meters

#ifdef WX_IMPROVED
    #define WX_READ_INTERVAL_MS 5000        // el barometro alimenta la altitud fusionada
#else
    #define WX_READ_INTERVAL_MS 60000
#endif

extern Configuration    Config;
extern logging::Logger  logger;
extern TinyGPSPlus      gps;

extern uint8_t          wxModuleAddress;

float newHum = NAN, newTemp = NAN, newPress = NAN, newGas = NAN;

uint32_t    sensorLastReading   = 0;
int         wxModuleType        = 0;        // 1=BME280, 2=BMP280, 3=BME680, 4=SHTC3

bool        wxModuleFound       = false;
bool        wxHaveReading       = false;    // hubo al menos una lectura completa


Adafruit_BME280     bme280;
#if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC)
Adafruit_BMP280     bmp280(&Wire1);
#else
Adafruit_BMP280     bmp280;
Adafruit_BME680     bme680;
#endif
#ifdef LIGHTTRACKER_PLUS_1_0
Adafruit_SHTC3 shtc3 = Adafruit_SHTC3();
#endif


namespace {

    // El BME680 calienta su resistencia de gas en cada lectura (autocalentamiento): se mantiene la cadencia de 60 s
    uint32_t readInterval() {
        return (wxModuleType == 3) ? 60000UL : (uint32_t)WX_READ_INTERVAL_MS;
    }

    float outputTemperature() {
        return newTemp + Config.telemetry.temperatureCorrection;
    }

    float outputHumidity() {
        #ifdef WX_IMPROVED
            return BARO_Utils::correctHumidity(newHum, newTemp, outputTemperature());
        #else
            return newHum;
        #endif
    }

    float outputPressure() {
        #ifdef WX_IMPROVED
            return BARO_Utils::reducedPressure(newPress, outputTemperature());
        #else
            return newPress + (gps.altitude.meters() / CORRECTION_FACTOR);
        #endif
    }

    #ifdef WX_IMPROVED
    void feedBarometer() {
        bool gpsGood = gps.altitude.isValid() && gps.altitude.age() < 10000 &&
                       gps.satellites.isValid() && gps.satellites.value() >= 5 &&
                       gps.hdop.isValid() && gps.hdop.hdop() > 0.0 && gps.hdop.hdop() <= 3.0;
        float gpsAltitude = gpsGood ? (float) gps.altitude.meters() : 0.0f;
        if (!gpsGood && GPS_Utils::hasLastAltitude()) BARO_Utils::setFallbackAltitude(GPS_Utils::getLastAltitude());
        BARO_Utils::feed(newPress, outputTemperature(), gpsGood, gpsAltitude, millis());
    }
    #endif

    // Lee el sensor fisico sin construir Strings innecesarios
    void pollSensor() {
        if (!wxModuleFound) return;
        #ifdef LIGHTTRACKER_PLUS_1_0
            sensors_event_t humidity, temp;
            shtc3.getEvent(&humidity, &temp);
            newTemp  = temp.temperature;
            newHum   = humidity.relative_humidity;
            newPress = 0.0f;
        #else
            switch (wxModuleType) {
                case 1: // BME280
                    bme280.takeForcedMeasurement();
                    newTemp  = bme280.readTemperature();
                    newPress = (bme280.readPressure() / 100.0F);
                    newHum   = bme280.readHumidity();
                    break;
                case 2: // BMP280
                    bmp280.takeForcedMeasurement();
                    newTemp  = bmp280.readTemperature();
                    newPress = (bmp280.readPressure() / 100.0F);
                    newHum   = 0.0f;
                    break;
                case 3: // BME680
                    #if !defined(HELTEC_V3_GPS) && !defined(HELTEC_V3_TNC) && !defined(HELTEC_V3_2_GPS) && !defined(HELTEC_V3_2_TNC)
                        bme680.performReading();
                        delay(50);
                        if (bme680.endReading()) {
                            newTemp  = bme680.temperature;
                            newPress = (bme680.pressure / 100.0F);
                            newHum   = bme680.humidity;
                            newGas   = bme680.gas_resistance / 1000.0f; // in Kilo ohms
                        }
                    #endif
                    break;
            }
        #endif
        sensorLastReading = millis() ? millis() : 1;
        wxHaveReading     = !isnan(newTemp) && newTemp > -60.0f && newTemp < 85.0f;
        #ifdef WX_IMPROVED
            if (wxHaveReading && (wxModuleType == 1 || wxModuleType == 2 || wxModuleType == 3)) feedBarometer();
        #endif
    }

}

namespace WX_Utils {

    void setup() {
        #ifdef WX_IMPROVED
            bool wantSensor = true;     // En placas con barometro integrado (Supreme V3), activar siempre para altimetro, pantallas y NMEA
        #else
            bool wantSensor = Config.telemetry.active;
        #endif
        if (wantSensor) {
            #ifdef LIGHTTRACKER_PLUS_1_0
                if (!shtc3.begin()) {
                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BME", " SHTC3 sensor not found");
                    return;
                }
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BME", " SHTC3 sensor found");
                wxModuleFound = true;
                wxModuleType = 4;
            #else
                if (wxModuleAddress != 0x00) {
                    #if defined(HELTEC_V3_GPS) || defined(HELTEC_V3_TNC) || defined(HELTEC_V3_2_GPS) || defined(HELTEC_V3_2_TNC)
                        if (bme280.begin(wxModuleAddress, &Wire1)) {
                            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BME", " BME280 sensor found");
                            wxModuleType = 1;
                            wxModuleFound = true;
                        }
                    #else
                        if (bme280.begin(wxModuleAddress)) {
                            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BME", " BME280 sensor found");
                            wxModuleType = 1;
                            wxModuleFound = true;
                        }
                        if (!wxModuleFound) {
                            if (bme680.begin(wxModuleAddress)) {
                                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BME", " BME680 sensor found");
                                wxModuleType = 3;
                                wxModuleFound = true;
                            }
                        }
                    #endif
                    if (!wxModuleFound) {
                        if (bmp280.begin(wxModuleAddress)) {
                            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BME", " BMP280 sensor found");
                            wxModuleType = 2;
                            wxModuleFound = true;
                        }
                    }
                    if (!wxModuleFound) {
                        if (Config.telemetry.active) {
                            displayShow("ERROR", "BME/BMP sensor active", "but no sensor found...", 2000);
                            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "BME", " BME/BMP sensor Active in config but not found! Check Wiring");
                        }
                    } else {
                        switch (wxModuleType) {
                            case 1:
                                bme280.setSampling(Adafruit_BME280::MODE_FORCED,
                                            Adafruit_BME280::SAMPLING_X1,
                                            Adafruit_BME280::SAMPLING_X1,
                                            Adafruit_BME280::SAMPLING_X1,
                                            Adafruit_BME280::FILTER_OFF
                                            );
                                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BME", " BME280 Module init done!");
                                break;
                            case 2:
                                bmp280.setSampling(Adafruit_BMP280::MODE_FORCED,
                                            Adafruit_BMP280::SAMPLING_X1,
                                            Adafruit_BMP280::SAMPLING_X1,
                                            Adafruit_BMP280::FILTER_OFF
                                            );
                                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BMP", " BMP280 Module init done!");
                                break;
                            case 3:
                                #if !defined(HELTEC_V3_GPS) && !defined(HELTEC_V3_TNC) && !defined(HELTEC_V3_2_GPS) && !defined(HELTEC_V3_2_TNC)
                                    bme680.setTemperatureOversampling(BME680_OS_1X);
                                    bme680.setHumidityOversampling(BME680_OS_1X);
                                    bme680.setPressureOversampling(BME680_OS_1X);
                                    bme680.setIIRFilterSize(BME680_FILTER_SIZE_0);
                                    bme680.setGasHeater(320, 150);
                                    logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "BME", " BME680 Module init done!");
                                #endif
                                break;
                        }
                        pollSensor();   // Primera lectura inmediata al arrancar
                    }
                }
            #endif
        }
    }

    String formatSensorValueforScreen(float value, int width, const String& errorValue) {
        String valueString = String((int)value);
        if ((int)valueString.length() > width) return errorValue;
        while ((int)valueString.length() < width) {
            valueString = " " + valueString;  // Left-padding
        }
        return valueString;
    }

    String readDataSensor(const uint8_t type) {
        if (!wxHaveReading || (millis() - sensorLastReading) > readInterval()) {
            pollSensor();
        }

        String sensorTelemetry;
        if (!wxHaveReading || isnan(newTemp) || isnan(newHum) || isnan(newPress) || (!wxModuleFound)) {
            Serial.println("WX Sensor data failed/not found");
            sensorTelemetry = ((type == 1) ? " - C    - %    - hPa" : "");
        } else {
            if (type == 0) {
                sensorTelemetry = TELEMETRY_Utils::generateEncodedTelemetryBytes(outputTemperature(), false, 2); // temperature
                if (wxModuleType == 1 || wxModuleType == 3 || wxModuleType == 4) {
                    sensorTelemetry += TELEMETRY_Utils::generateEncodedTelemetryBytes(outputHumidity(), false, 0); // humidity
                }
                if (wxModuleType == 1 || wxModuleType == 2 || wxModuleType == 3) {
                    sensorTelemetry += TELEMETRY_Utils::generateEncodedTelemetryBytes(outputPressure(), false, 3); // pressure
                }
                if (wxModuleType == 3) {
                    sensorTelemetry += TELEMETRY_Utils::generateEncodedTelemetryBytes(newGas, false, 0); // gas
                }
            } else {    // show it in (Oled) Screen
                sensorTelemetry = formatSensorValueforScreen(outputTemperature(), 3, "-99");
                sensorTelemetry += "C   ";
                if (wxModuleType == 1 || wxModuleType == 3 || wxModuleType == 4) {
                    sensorTelemetry += formatSensorValueforScreen(outputHumidity(), 2, "-9");
                } else {
                    sensorTelemetry += "__";
                }
                sensorTelemetry += "%   ";
                if (wxModuleType == 1 || wxModuleType == 2 || wxModuleType == 3) {
                    sensorTelemetry += formatSensorValueforScreen(outputPressure(), 4, "-999");
                } else {
                    sensorTelemetry += "____";
                }
                sensorTelemetry += "hPa";
            }
        }
        return sensorTelemetry;
    }

    bool getReading(Reading& reading) {
        reading.valid = reading.hasHumidity = reading.hasPressure = reading.hasGas = false;
        reading.tempC = reading.humidity = reading.pressureHpa = reading.gasKohm = 0;
        if (!wxModuleFound || !wxHaveReading || wxModuleType < 1 || wxModuleType > 4) return false;
        if (isnan(newTemp)) return false;
        reading.tempC = outputTemperature();
        if ((wxModuleType == 1 || wxModuleType == 3 || wxModuleType == 4) && !isnan(newHum)) {
            reading.hasHumidity = true;
            reading.humidity    = outputHumidity();
        }
        if ((wxModuleType == 1 || wxModuleType == 2 || wxModuleType == 3) && !isnan(newPress) && newPress > 300.0f) {
            reading.hasPressure = true;
            reading.pressureHpa = outputPressure();
        }
        if (wxModuleType == 3 && !isnan(newGas)) {
            reading.hasGas  = true;
            reading.gasKohm = newGas;
        }
        reading.valid = true;
        return true;
    }

    void refreshForScreen() {
        if (wxModuleFound && (!wxHaveReading || (millis() - sensorLastReading) > readInterval())) pollSensor();
    }

    const char* sensorName() {
        switch (wxModuleType) {
            case 1: return "BME280";
            case 2: return "BMP280";
            case 3: return "BME680";
            case 4: return "SHTC3";
            default: return "none";
        }
    }

    // Mantiene el barometro al dia aunque la pantalla y la telemetria no lo pidan (altitud fusionada)
    void update() {
        #ifdef WX_IMPROVED
            if (wxModuleFound && (!wxHaveReading || (millis() - sensorLastReading) > readInterval())) pollSensor();
        #endif
    }

    // Valores numericos (con las mismas correcciones que la pantalla y la telemetria) para la salida NMEA
    bool getEnvironment(float& temperatureC, bool& hasTemperature, float& humidityPct, bool& hasHumidity, float& pressureHpa, bool& hasPressure) {
        hasTemperature = hasHumidity = hasPressure = false;
        if (!wxModuleFound) return false;
        if (!wxHaveReading || (millis() - sensorLastReading) > readInterval()) pollSensor();
        if (!wxHaveReading) return false;
        temperatureC   = outputTemperature();
        hasTemperature = !isnan(temperatureC);
        hasHumidity    = (wxModuleType == 1 || wxModuleType == 3 || wxModuleType == 4) && !isnan(newHum);
        humidityPct    = hasHumidity ? outputHumidity() : 0.0f;
        hasPressure    = (wxModuleType == 1 || wxModuleType == 2 || wxModuleType == 3) && !isnan(newPress) && newPress > 300.0f;
        pressureHpa    = hasPressure ? outputPressure() : 0.0f;
        return hasTemperature || hasHumidity || hasPressure;
    }

}