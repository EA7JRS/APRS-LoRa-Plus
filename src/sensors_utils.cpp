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

// Inventario de sensores de la T-Beam S3 Supreme: escanea ambos buses I2C y verifica los IDs de cada periferico.
// Solo lectura: no altera la configuracion de ningun sensor. Sirve para saber que variante de hardware hay en la placa.

#include "board_pinout.h"
#include "sensors_utils.h"

#ifdef TTGO_T_Beam_S3_SUPREME_V3

#include <Arduino.h>
#include <Wire.h>
#include <logger.h>
#include "power_utils.h"

extern logging::Logger  logger;
extern XPowersAXP2101   PMU;
extern uint8_t          wxModuleAddress;

namespace {

    struct KnownDevice {
        uint8_t     address;
        const char* name;
    };

    const KnownDevice bus0Devices[] = {
        {0x1C, "QMC6310 magnetometer"},
        {0x3C, "OLED SH1106 (o QMC6310N)"},
        {0x3D, "OLED SH1106 (alt)"},
        {0x55, "Keyboard I2C"},
        {0x5F, "Keyboard CardKB"},
        {0x76, "BME/BMP280 (alt)"},
        {0x77, "BME280"},
        {0x7C, "QMC6309 magnetometer"}
    };

    const KnownDevice bus1Devices[] = {
        {0x34, "AXP2101 PMU"},
        {0x51, "RTC PCF8563/PCF85063"}
    };

    const char* lookupName(const KnownDevice* list, size_t count, uint8_t address) {
        for (size_t i = 0; i < count; i++) {
            if (list[i].address == address) return list[i].name;
        }
        return "desconocido";
    }

    bool readRegister(TwoWire& bus, uint8_t address, uint8_t reg, uint8_t& value) {
        bus.beginTransmission(address);
        bus.write(reg);
        if (bus.endTransmission(false) != 0) return false;
        if (bus.requestFrom(address, (uint8_t)1) != 1) return false;
        value = bus.read();
        return true;
    }

    // Devuelve true si encontro algun dispositivo en ese bus y deja el resumen en line.
    bool scanBus(TwoWire& bus, const KnownDevice* list, size_t count, char* line, size_t lineSize, bool found[128]) {
        size_t used  = 0;
        bool   any   = false;
        line[0]      = 0;
        for (uint8_t addr = 1; addr < 0x7F; addr++) {
            bus.beginTransmission(addr);
            if (bus.endTransmission() != 0) continue;
            found[addr] = true;
            any         = true;
            int written = snprintf(line + used, lineSize - used, "%s0x%02X %s", used ? ", " : "", addr, lookupName(list, count, addr));
            if (written < 0 || (size_t)written >= lineSize - used) break;
            used += written;
        }
        return any;
    }

    const char* bmeChipName(uint8_t id) {
        switch (id) {
            case 0x60: return "BME280";
            case 0x58: return "BMP280";
            case 0x56:
            case 0x57: return "BMP280 (early)";
            case 0x61: return "BME680";
            default:   return "chip desconocido";
        }
    }

}

namespace SENSORS_Utils {

    void logBoardInventory() {
        char line[200];
        bool found0[128] = {false};
        bool found1[128] = {false};

        if (scanBus(Wire, bus0Devices, sizeof(bus0Devices) / sizeof(bus0Devices[0]), line, sizeof(line), found0)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Sensors", "I2C0: %s", line);
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Sensors", "I2C0: sin dispositivos");
        }
        if (scanBus(Wire1, bus1Devices, sizeof(bus1Devices) / sizeof(bus1Devices[0]), line, sizeof(line), found1)) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Sensors", "I2C1: %s", line);
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Sensors", "I2C1: sin dispositivos");
        }

        // Entorno: BME280 (0x77) o alternativa (0x76), verificando el ID del chip
        uint8_t bmeAddress = found0[0x77] ? 0x77 : (found0[0x76] ? 0x76 : 0);
        if (bmeAddress != 0) {
            if (wxModuleAddress == 0x00) wxModuleAddress = bmeAddress;
            uint8_t chipId = 0;
            if (readRegister(Wire, bmeAddress, 0xD0, chipId)) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Sensors", "Ambiental 0x%02X: %s (id 0x%02X)", bmeAddress, bmeChipName(chipId), chipId);
            }
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Sensors", "BME280 no encontrado (0x77): revisar ALDO1");
        }

        // Magnetometro: 0x1C (QMC6310U/N) o 0x7C (QMC6309); 0x3C se comparte con el OLED
        uint8_t magAddr = found0[0x1C] ? 0x1C : (found0[0x7C] ? 0x7C : 0);
        if (magAddr != 0) {
            uint8_t magId = 0;
            readRegister(Wire, magAddr, 0x00, magId);
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Sensors", "Magnetometro: %s (0x%02X, id 0x%02X)",
                       magAddr == 0x1C ? "QMC6310" : "QMC6309", magAddr, magId);
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Sensors", "Magnetometro no encontrado en 0x1C/0x7C");
        }

        // RTC y PMU en I2C1
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Sensors", "RTC (0x51): %s", found1[0x51] ? "OK" : "no encontrado");
        if (found1[0x34]) {
            bool batt = PMU.isBatteryConnect();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "Sensors", "PMU (0x34): OK, VBUS %.2f V, bateria: %s (%.2f V)",
                       POWER_Utils::getVbusVoltage(), batt ? "conectada" : "no conectada",
                       batt ? (PMU.getBattVoltage() / 1000.0f) : 0.0f);
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "Sensors", "PMU (0x34): no encontrado");
        }
    }

}

#else

namespace SENSORS_Utils {
    void logBoardInventory() {}
}

#endif