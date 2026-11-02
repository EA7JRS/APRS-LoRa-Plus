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

// Magnetometro QMC6310 / QMC6309 (I2C0, 0x1C o 0x7C) de la T-Beam S3 Supreme: rumbo de la brujula cuando el GPS no da curso.
// Calibracion automatica de hierro duro: hay que girar el equipo en horizontal una vuelta completa; se guarda en /mag_cal.dat.
// Si el chip no responde o sus datos no son plausibles, queda desactivado.

#include "board_pinout.h"
#include "mag_utils.h"

#ifdef TTGO_T_Beam_S3_SUPREME_V3

#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <TinyGPS++.h>
#include <TimeLib.h>
#include <initializer_list>
#include <math.h>
#include <logger.h>
#include "imu_utils.h"
#include "gps_utils.h"
#include "wmm.h"

#ifndef MAG_HEADING_OFFSET_DEG
    #define MAG_HEADING_OFFSET_DEG  0
#endif
#ifndef MAG_HEADING_INVERT
    #define MAG_HEADING_INVERT      0
#endif
#ifndef MAG_RESET_CALIBRATION
    #define MAG_RESET_CALIBRATION   0
#endif

#ifndef MAG_AUTO_ALIGN
    #define MAG_AUTO_ALIGN              1           // autoalineacion con el rumbo GPS
#endif
#ifndef MAG_ALIGN_MIN_KMPH
    #define MAG_ALIGN_MIN_KMPH          10.0f       // velocidad minima para usar el rumbo GPS
#endif
#ifndef MAG_ALIGN_MAX_OFFSET_DEG
    #define MAG_ALIGN_MAX_OFFSET_DEG    25.0f       // un desfase mayor se considera orientacion de montaje, no se aprende
#endif

extern logging::Logger  logger;
extern TinyGPSPlus      gps;

namespace {

    constexpr uint8_t   ADDRESS_PRIMARY     = 0x1C;
    constexpr uint8_t   ADDRESS_ALTERNATE   = 0x7C;
    constexpr uint8_t   REG_CHIP_ID         = 0x00;
    constexpr uint8_t   REG_DATA            = 0x01;
    constexpr uint8_t   REG_STATUS          = 0x09;
    constexpr uint8_t   REG_CONTROL1        = 0x0A;
    constexpr uint8_t   REG_CONTROL2        = 0x0B;
    constexpr uint8_t   REG_SET_RESET       = 0x29;
    constexpr uint8_t   CHIP_ID_QMC6310     = 0x80;

    constexpr uint32_t  READ_PERIOD_MS      = 100;
    constexpr int       MIN_RANGE           = 500;          // recorrido minimo por eje para calibrar (cuentas)
    constexpr float     SMOOTHING_QUIET     = 0.15f;        // suavizado fuerte cuando el equipo no gira
    constexpr float     SMOOTHING_TURNING   = 0.65f;        // respuesta rapida cuando el giroscopio detecta rotacion
    constexpr float     TURN_RATE_FAST_DPS  = 15.0f;        // velocidad angular para alcanzar el suavizado rapido
    constexpr float     ALIGN_MAX_YAW_DPS   = 2.0f;         // por encima de 2 dps el giroscopio descarta aprendizaje en recta
    constexpr char      CAL_FILE[]          = "/mag_cal.dat";
    constexpr uint16_t  CAL_MAGIC           = 0x4D44;       // cambia al mejorar el algoritmo: invalida calibraciones anteriores

    struct CalFile {
        uint16_t    magic;
        int16_t     minX, maxX, minY, maxY;
    };

    uint8_t     magAddress      = 0;
    bool        magReady        = false;
    bool        calibrated      = false;
    bool        haveRange       = false;
    int16_t     minX = 0, maxX = 0, minY = 0, maxY = 0;
    uint8_t     sectorCount[8]  = {0};
    uint32_t    rangeCheckTime  = 0;
    int         rangeXPrev      = 0;
    int         rangeYPrev      = 0;
    bool        rangeStable     = false;
    float       radiusEma       = 1.0f;
    bool        radiusOk        = true;
    float       smoothCos       = 1.0f;
    float       smoothSin       = 0.0f;
    uint32_t    lastRead        = 0;
    uint32_t    lastLog         = 0;

    // Cache de declinacion WMM (para no recalcular armonicos esfericos en cada lectura)
    bool        wmmValid        = false;
    float       wmmDeclDeg      = 0.0f;
    uint32_t    lastWmmCalc     = 0;

    bool readRegs(uint8_t reg, uint8_t* buffer, uint8_t length) {
        Wire.beginTransmission(magAddress);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom(magAddress, length) != length) return false;
        for (uint8_t i = 0; i < length; i++) buffer[i] = Wire.read();
        return true;
    }

    void writeReg(uint8_t reg, uint8_t value) {
        Wire.beginTransmission(magAddress);
        Wire.write(reg);
        Wire.write(value);
        Wire.endTransmission();
    }

    bool readField(int16_t& x, int16_t& y, int16_t& z) {
        uint8_t status;
        if (!readRegs(REG_STATUS, &status, 1) || !(status & 0x01)) return false;
        uint8_t raw[6];
        if (!readRegs(REG_DATA, raw, 6)) return false;
        x = (int16_t)((raw[1] << 8) | raw[0]);
        y = (int16_t)((raw[3] << 8) | raw[2]);
        z = (int16_t)((raw[5] << 8) | raw[4]);
        return true;
    }

    void saveCalibration() {
        File file = SPIFFS.open(CAL_FILE, "w");
        if (!file) return;
        CalFile data = {CAL_MAGIC, minX, maxX, minY, maxY};
        file.write((const uint8_t*)&data, sizeof(data));
        file.close();
        logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "MAG", "Calibracion guardada");
    }

    bool loadCalibration() {
        if (MAG_RESET_CALIBRATION) return false;
        File file = SPIFFS.open(CAL_FILE, "r");
        if (!file) return false;
        CalFile data;
        bool ok = file.read((uint8_t*)&data, sizeof(data)) == sizeof(data) && data.magic == CAL_MAGIC &&
                  (data.maxX - data.minX) >= MIN_RANGE && (data.maxY - data.minY) >= MIN_RANGE;
        file.close();
        if (!ok) return false;
        minX = data.minX; maxX = data.maxX; minY = data.minY; maxY = data.maxY;
        haveRange  = true;
        calibrated = true;
        return true;
    }

    // Solo mientras no esta calibrada: una vez calibrada, los extremos se congelan para que una anomalia
    // magnetica puntual no los degrade. Exige una vuelta completa en horizontal con el recorrido ya estable.
    void updateCalibration(int16_t x, int16_t y, uint32_t now) {
        if (!IMU_Utils::isFlat()) return;   // no registrar muestras de calibracion si la placa esta inclinada
        if (!haveRange) {
            minX = maxX = x;
            minY = maxY = y;
            haveRange = true;
            rangeCheckTime = now;
            return;
        }
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        int rangeX = maxX - minX;
        int rangeY = maxY - minY;

        if (now - rangeCheckTime >= 2000) {
            rangeStable = rangeXPrev > 0 && rangeYPrev > 0 &&
                          rangeX <= rangeXPrev * 1.03f && rangeY <= rangeYPrev * 1.03f;
            rangeXPrev = rangeX;
            rangeYPrev = rangeY;
            rangeCheckTime = now;
        }
        if (rangeX < MIN_RANGE || rangeY < MIN_RANGE) return;
        float cx = (maxX + minX) / 2.0f;
        float cy = (maxY + minY) / 2.0f;
        float dx = x - cx;
        float dy = y - cy;
        float minHalf = (rangeX < rangeY ? rangeX : rangeY) / 2.0f;
        if (sqrtf(dx * dx + dy * dy) >= 0.3f * minHalf) {
            int sector = (int)((atan2f(dy, dx) + (float)M_PI) / ((float)M_PI / 4.0f)) & 7;
            if (sectorCount[sector] < 255) sectorCount[sector]++;
        }
        bool allSectors = true;
        for (uint8_t i = 0; i < 8; i++) {
            if (sectorCount[i] < 3) allSectors = false;
        }
        float ratio = (float)rangeX / (float)rangeY;
        if (allSectors && rangeStable && ratio > 0.4f && ratio < 2.5f) {
            calibrated = true;
            saveCalibration();
        }
    }

    // Radio normalizado del vector horizontal: ~1 si la placa esta horizontal y no hay interferencias
    void updateRadiusCheck(int16_t x, int16_t y) {
        float cx = (maxX + minX) / 2.0f;
        float cy = (maxY + minY) / 2.0f;
        float hx = (maxX - minX) / 2.0f;
        float hy = (maxY - minY) / 2.0f;
        float avg = (hx + hy) / 2.0f;
        float vx = (x - cx) * (avg / hx);
        float vy = (y - cy) * (avg / hy);
        float radius = sqrtf(vx * vx + vy * vy) / avg;
        radiusEma += (radius - radiusEma) * 0.1f;
        bool ok = fabsf(radiusEma - 1.0f) < 0.35f;
        if (ok != radiusOk) {
            radiusOk = ok;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "MAG", ok ? "Campo coherente con la calibracion" : "Campo incoherente (inclinacion o interferencia): rumbo desactivado");
        }
    }

    float computeHeading(int16_t x, int16_t y) {
        float cx = (maxX + minX) / 2.0f;
        float cy = (maxY + minY) / 2.0f;
        float hx = (maxX - minX) / 2.0f;
        float hy = (maxY - minY) / 2.0f;
        float avg = (hx + hy) / 2.0f;
        float vx = (x - cx) * (avg / hx);
        float vy = (y - cy) * (avg / hy);
        float degrees = atan2f(vy, vx) * 180.0f / (float)M_PI;
        if (MAG_HEADING_INVERT) degrees = -degrees;
        degrees += MAG_HEADING_OFFSET_DEG;
        while (degrees < 0) degrees += 360.0f;
        while (degrees >= 360.0f) degrees -= 360.0f;
        return degrees;
    }

    // Declinacion magnetica WMM de respaldo cuando aun no se ha completado la autoalineacion con el GPS
    float wmmDeclinationOffset() {
        uint32_t nowMs = millis();
        if (wmmValid && (nowMs - lastWmmCalc) < 600000UL) return wmmDeclDeg;

        double lat = 0.0, lon = 0.0, altKm = 0.0;
        bool havePos = false;
        if (gps.location.isValid()) {
            lat     = gps.location.lat();
            lon     = gps.location.lng();
            altKm   = GPS_Utils::getAltitudeMeters() / 1000.0;
            havePos = true;
        } else if (GPS_Utils::hasLastPosition()) {
            lat     = GPS_Utils::getLastLat();
            lon     = GPS_Utils::getLastLng();
            altKm   = GPS_Utils::hasLastAltitude() ? (GPS_Utils::getLastAltitude() / 1000.0) : 0.0;
            havePos = true;
        }
        if (!havePos) return wmmValid ? wmmDeclDeg : 0.0f;

        double decYear = 2026.5;
        if (year() >= 2024) {
            decYear = (double)year() + ((double)(month() - 1) + (double)day() / 31.0) / 12.0;
        }
        float decl = 0.0f;
        if (WMM::declination(lat, lon, altKm, decYear, decl)) {
            wmmDeclDeg  = decl;
            wmmValid    = true;
            lastWmmCalc = nowMs ? nowMs : 1;
        }
        return wmmValid ? wmmDeclDeg : 0.0f;
    }


    // ---------------- autoalineacion con el rumbo GPS ----------------
    // El rumbo GPS (norte verdadero) se compara con el del magnetometro cuando el equipo va a mas de 10 km/h en linea recta.
    // Solo se acepta un desfase si es coherente en varios rumbos distintos: una distorsion del vehiculo cambia con el rumbo,
    // un desfase de alineacion/declinacion no. Se guarda en /mag_align.dat.
    inline float MAX_ALIGN_OFFSET() { return MAG_ALIGN_MAX_OFFSET_DEG; }
    constexpr char      ALIGN_FILE[]        = "/mag_align.dat";
    constexpr uint16_t  ALIGN_MAGIC         = 0x4D41;
    constexpr uint8_t   WINDOW              = 5;            // muestras de 1 s sin giros
    constexpr float     MAX_COURSE_SPREAD   = 4.0f;         // grados
    constexpr float     MAX_HEADING_SPREAD  = 8.0f;
    constexpr uint16_t  MIN_SECTOR_SAMPLES  = 15;
    constexpr uint8_t   MIN_SECTORS         = 3;
    constexpr float     MAX_SECTOR_SPREAD   = 8.0f;         // los desfases de cada sector deben coincidir
    constexpr uint16_t  MAX_SECTOR_SAMPLES  = 300;

    struct AlignFile {
        uint16_t    magic;
        float       offset;
    };

    bool        alignKnown      = false;
    float       alignOffset     = 0.0f;
    float       winCourse[WINDOW]   = {0};
    float       winHeading[WINDOW]  = {0};
    uint8_t     winCount        = 0;
    float       alignSectorCos[8]    = {0};
    float       alignSectorSin[8]    = {0};
    uint16_t    alignSectorCount[8]  = {0};
    uint32_t    lastAlignSave   = 0;
    bool        limitWarned     = false;

    float wrap180(float degrees) {
        while (degrees > 180.0f)    degrees -= 360.0f;
        while (degrees <= -180.0f)  degrees += 360.0f;
        return degrees;
    }

    float wrap360(float degrees) {
        while (degrees < 0.0f)      degrees += 360.0f;
        while (degrees >= 360.0f)   degrees -= 360.0f;
        return degrees;
    }

    // Media circular y desviacion maxima respecto a ella
    float circularSpread(const float* values, uint8_t count, float& mean) {
        float c = 0, s = 0;
        for (uint8_t i = 0; i < count; i++) {
            c += cosf(values[i] * (float)M_PI / 180.0f);
            s += sinf(values[i] * (float)M_PI / 180.0f);
        }
        mean = wrap360(atan2f(s, c) * 180.0f / (float)M_PI);
        float spread = 0;
        for (uint8_t i = 0; i < count; i++) {
            float d = fabsf(wrap180(values[i] - mean));
            if (d > spread) spread = d;
        }
        return spread;
    }

    float baseHeadingDeg() {
        float degrees = atan2f(smoothSin, smoothCos) * 180.0f / (float)M_PI;
        return wrap360(degrees);
    }

    void saveAlignment() {
        File file = SPIFFS.open(ALIGN_FILE, "w");
        if (!file) return;
        AlignFile data = {ALIGN_MAGIC, alignOffset};
        file.write((const uint8_t*)&data, sizeof(data));
        file.close();
        lastAlignSave = millis() ? millis() : 1;
    }

    void loadAlignment() {
        if (MAG_RESET_CALIBRATION) return;
        File file = SPIFFS.open(ALIGN_FILE, "r");
        if (!file) return;
        AlignFile data;
        bool ok = file.read((uint8_t*)&data, sizeof(data)) == sizeof(data) && data.magic == ALIGN_MAGIC &&
                  fabsf(data.offset) <= MAG_ALIGN_MAX_OFFSET_DEG;
        file.close();
        if (!ok) return;
        alignKnown  = true;
        alignOffset = data.offset;
    }

    void clearAlignmentState() {
        alignKnown = false;
        alignOffset = 0.0f;
        winCount = 0;
        limitWarned = false;
        for (uint8_t i = 0; i < 8; i++) {
            alignSectorCos[i] = alignSectorSin[i] = 0.0f;
            alignSectorCount[i] = 0;
        }
    }

    void evaluateAlignment() {
        float means[8];
        uint8_t used = 0;
        for (uint8_t i = 0; i < 8; i++) {
            if (alignSectorCount[i] >= MIN_SECTOR_SAMPLES) means[used++] = wrap360(atan2f(alignSectorSin[i], alignSectorCos[i]) * 180.0f / (float)M_PI);
        }
        if (used < MIN_SECTORS) return;
        float candidate360;
        if (circularSpread(means, used, candidate360) > MAX_SECTOR_SPREAD) return;      // depende del rumbo: distorsion, no alineacion
        float candidate = wrap180(candidate360);
        if (fabsf(candidate) > MAX_ALIGN_OFFSET()) {
            if (!limitWarned) {
                limitWarned = true;
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "MAG", "Desfase %.1f grados fuera de limite (+-%.0f): probable orientacion de montaje, no se aplica", candidate, MAX_ALIGN_OFFSET());
            }
            return;
        }
        uint32_t now = millis();
        if (!alignKnown) {
            alignKnown  = true;
            alignOffset = candidate;
            saveAlignment();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "MAG", "Autoalineacion con el rumbo GPS: %+.1f grados (%d rumbos)", alignOffset, used);
            return;
        }
        float difference = wrap180(candidate - alignOffset);
        if (fabsf(difference) < 1.0f) return;
        alignOffset += difference * 0.5f;
        if (fabsf(difference) >= 2.0f && (lastAlignSave == 0 || now - lastAlignSave > 600000UL)) {
            saveAlignment();
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "MAG", "Autoalineacion actualizada: %+.1f grados", alignOffset);
        }
    }

}

namespace MAG_Utils {

    void setup() {
        for (uint8_t candidate : {ADDRESS_PRIMARY, ADDRESS_ALTERNATE}) {
            Wire.beginTransmission(candidate);
            if (Wire.endTransmission() == 0) {
                magAddress = candidate;
                break;
            }
        }
        if (magAddress == 0) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "MAG", "Magnetometro no encontrado en 0x1C ni 0x7C");
            return;
        }
        uint8_t chipId = 0;
        readRegs(REG_CHIP_ID, &chipId, 1);
        if (chipId == CHIP_ID_QMC6310) writeReg(REG_SET_RESET, 0x06);
        writeReg(REG_CONTROL2, 0x04);               // rango +-12 G, set/reset activo
        writeReg(REG_CONTROL1, 0x07);               // modo continuo, 50 Hz
        delay(30);

        // Plausibilidad: algun dato nuevo, sin saturar y con magnitud en un rango amplio
        bool valid = false;
        for (uint8_t i = 0; i < 10 && !valid; i++) {
            int16_t x, y, z;
            if (readField(x, y, z)) {
                float magnitude = sqrtf((float)x * x + (float)y * y + (float)z * z);
                valid = magnitude > 100.0f && magnitude < 20000.0f && abs(x) < 30000 && abs(y) < 30000 && abs(z) < 30000;
            }
            if (!valid) delay(20);
        }
        if (!valid) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "MAG", "0x%02X (id 0x%02X): datos no plausibles, desactivado", magAddress, chipId);
            return;
        }
        magReady = true;
        #if MAG_AUTO_ALIGN
            if (MAG_RESET_CALIBRATION) SPIFFS.remove(ALIGN_FILE);
            loadAlignment();
        #endif
        if (loadCalibration()) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "MAG", "0x%02X (id 0x%02X) OK, calibracion cargada", magAddress, chipId);
        } else {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "MAG", "0x%02X (id 0x%02X) OK, gira el equipo en horizontal para calibrar", magAddress, chipId);
        }
    }

    void update() {
        if (!magReady) return;
        uint32_t now = millis();
        if (now - lastRead < READ_PERIOD_MS) return;
        lastRead = now;
        int16_t x, y, z;
        if (!readField(x, y, z)) return;
        if (!calibrated) updateCalibration(x, y, now);
        if (!calibrated) return;
        updateRadiusCheck(x, y);

        // Suavizado adaptativo guiado por la velocidad angular del giroscopio (si el IMU esta disponible)
        float alpha = SMOOTHING_QUIET;
        if (IMU_Utils::isReady()) {
            float yawRate = fabsf(IMU_Utils::getYawRateDps());
            float ratio   = yawRate / TURN_RATE_FAST_DPS;
            if (ratio > 1.0f) ratio = 1.0f;
            alpha = SMOOTHING_QUIET + (SMOOTHING_TURNING - SMOOTHING_QUIET) * ratio;
        }

        float radians = computeHeading(x, y) * (float)M_PI / 180.0f;
        smoothCos += (cosf(radians) - smoothCos) * alpha;
        smoothSin += (sinf(radians) - smoothSin) * alpha;
        if (now - lastLog > 5000) {
            lastLog = now;
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_DEBUG, "MAG", "heading %d (x %d, y %d)", (int)heading(), x, y);
        }
    }

    bool isReady() {
        return magReady;
    }

    bool isCalibrated() {
        return magReady && calibrated;
    }

    bool isHeadingValid() {
        return magReady && calibrated && radiusOk && IMU_Utils::isFlat();
    }

    float heading() {
        // Si ya hay autoalineacion GPS aprendida, incluye declinacion + montaje; si no, aplica la declinacion WMM local
        float offset = alignKnown ? alignOffset : wmmDeclinationOffset();
        return wrap360(baseHeadingDeg() + offset);
    }

    // Compara el rumbo GPS (norte verdadero) con el del magnetometro a mas de MAG_ALIGN_MIN_KMPH, en linea recta y con
    // el equipo horizontal; aprende el desfase (declinacion + alineacion) solo si es coherente en varios rumbos.
    // OJO: supone que el eje de referencia de la placa apunta en el sentido de la marcha.
    void feedGpsCourse(float courseDeg, float speedKmph, bool gpsGood) {
        #if MAG_AUTO_ALIGN
            if (!magReady || !calibrated) return;
            bool straightByGyro = !IMU_Utils::isReady() || (fabsf(IMU_Utils::getYawRateDps()) <= ALIGN_MAX_YAW_DPS);
            if (!gpsGood || speedKmph < MAG_ALIGN_MIN_KMPH || !isHeadingValid() || !straightByGyro) {
                winCount = 0;
                return;
            }
            if (winCount == WINDOW) {
                for (uint8_t i = 1; i < WINDOW; i++) {
                    winCourse[i - 1]  = winCourse[i];
                    winHeading[i - 1] = winHeading[i];
                }
                winCount = WINDOW - 1;
            }
            winCourse[winCount]  = courseDeg;
            winHeading[winCount] = baseHeadingDeg();
            winCount++;
            if (winCount < WINDOW) return;

            float meanCourse, meanHeading;
            if (circularSpread(winCourse, WINDOW, meanCourse) > MAX_COURSE_SPREAD) return;     // no va en linea recta
            if (circularSpread(winHeading, WINDOW, meanHeading) > MAX_HEADING_SPREAD) return;   // el equipo se mueve respecto al vehiculo
            float delta  = wrap180(meanCourse - meanHeading) * (float)M_PI / 180.0f;
            uint8_t sector = ((int)(meanCourse / 45.0f)) & 7;
            alignSectorCos[sector] += cosf(delta);
            alignSectorSin[sector] += sinf(delta);
            alignSectorCount[sector]++;
            if (alignSectorCount[sector] > MAX_SECTOR_SAMPLES) {                                     // olvida lo antiguo
                alignSectorCos[sector]   *= 0.5f;
                alignSectorSin[sector]   *= 0.5f;
                alignSectorCount[sector] /= 2;
            }
            evaluateAlignment();
        #else
            (void)courseDeg; (void)speedKmph; (void)gpsGood;
        #endif
    }

    bool isAligned() {
        return alignKnown;
    }

    float alignmentOffset() {
        return alignKnown ? alignOffset : wmmDeclinationOffset();
    }

    void resetAlignment() {
        clearAlignmentState();
        SPIFFS.remove(ALIGN_FILE);
    }

}

#else

namespace MAG_Utils {
    void  setup() {}
    void  update() {}
    bool  isReady() { return false; }
    bool  isCalibrated() { return false; }
    bool  isHeadingValid() { return false; }
    float heading() { return 0; }
    void  feedGpsCourse(float, float, bool) {}
    bool  isAligned() { return false; }
    float alignmentOffset() { return 0; }
    void  resetAlignment() {}
}

#endif