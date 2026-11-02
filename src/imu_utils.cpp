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

// IMU QMI8658 (SPI) de la T-Beam S3 Supreme: acelerometro + giroscopio con ahorro dinamico de energia.
// En reposo apaga el giroscopio (~850 uA -> ~50 uA) y lo reactiva al instante al detectar vibracion o movimiento.

#include "board_pinout.h"
#include "imu_utils.h"

#ifdef TTGO_T_Beam_S3_SUPREME_V3

#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <logger.h>
#include "power_utils.h"

extern logging::Logger  logger;
extern XPowersAXP2101   PMU;

namespace {

    constexpr uint8_t   REG_WHO_AM_I        = 0x00;
    constexpr uint8_t   REG_REVISION        = 0x01;
    constexpr uint8_t   REG_CTRL1           = 0x02;
    constexpr uint8_t   REG_CTRL2           = 0x03;     // Acelerometro: rango y ODR
    constexpr uint8_t   REG_CTRL3           = 0x04;     // Giroscopio: rango y ODR
    constexpr uint8_t   REG_CTRL7           = 0x08;     // bit0 = aEN (accel), bit1 = gEN (gyro)
    constexpr uint8_t   REG_STATUS0         = 0x2E;
    constexpr uint8_t   REG_TEMP_L          = 0x33;     // Temperatura interna (0x33..0x34), 256 LSB/°C
    constexpr uint8_t   REG_ACCEL_X         = 0x35;     // 0x35..0x3A
    constexpr uint8_t   REG_GYRO_X          = 0x3B;     // 0x3B..0x40
    constexpr uint8_t   WHO_AM_I_VALUE      = 0x05;

    constexpr float     ACCEL_LSB_PER_G     = 8192.0f;  // rango +-4 g   (CTRL2 = 0x17: aFS=01 +-4g, aODR=0111 62.5Hz)
    constexpr float     GYRO_LSB_PER_DPS    = 128.0f;   // rango +-256 dps (CTRL3 = 0x47: gFS=100 +-256dps, gODR=0111 62.5Hz)
    constexpr float     TEMP_LSB_PER_C      = 256.0f;

    constexpr uint32_t  SAMPLE_PERIOD_MOVING_MS = 40;   // 25 Hz en movimiento
    constexpr uint32_t  SAMPLE_PERIOD_IDLE_MS   = 100;  // 10 Hz en reposo (reduce trafico SPI un 60%)
    constexpr uint32_t  GYRO_SLEEP_AFTER_MS     = 15000;// apagar giroscopio tras 15 s sin actividad

    constexpr float     BASELINE_ALPHA      = 0.02f;    // referencia lenta de gravedad (~2 s)
    constexpr float     GYRO_BIAS_ALPHA     = 0.02f;    // aprendizaje del offset del giroscopio en reposo
    constexpr float     OUTLIER_G           = 0.15f;
    constexpr float     ACTIVITY_ALPHA      = 0.2f;     // suavizado de la actividad (~0,2 s)
    constexpr float     ACTIVITY_THRESHOLD  = 0.008f;   // g equivalentes
    constexpr float     GYRO_TO_G_WEIGHT    = 0.0015f;  // peso de rotacion en el detector de movimiento
    constexpr float     GYRO_QUIET_DPS      = 4.0f;     // por debajo de 4 dps y sin vibracion se actualiza el bias del gyro
    constexpr uint32_t  QUIET_GAP_MS        = 500;
    constexpr uint32_t  SUSTAINED_MS        = 2500;     // actividad acumulada necesaria para contar como movimiento
    constexpr uint32_t  MOVING_HOLD_MS      = 10000;

    struct MotionDetector {
        float       bx = 0, by = 0, bz = 0;
        float       gbx = 0, gby = 0, gbz = 0;
        bool        baselineInit = false;
        float       activity = 0;
        uint32_t    activeAccum = 0;
        uint32_t    lastActiveSample = 0;
        uint32_t    lastFeed = 0;
        uint32_t    lastMotion = 0;

        void initBias(float ax, float ay, float az, float gx, float gy, float gz, uint32_t now) {
            bx = ax; by = ay; bz = az;
            gbx = gx; gby = gy; gbz = gz;
            baselineInit = true;
            lastFeed = now;
            lastActiveSample = now;
        }

        void feed(float ax, float ay, float az, float gx, float gy, float gz, bool gyroValid, uint32_t now,
                  float& corrGx, float& corrGy, float& corrGz, float& omegaMag) {
            if (!baselineInit) {
                initBias(ax, ay, az, gx, gy, gz, now);
            }
            uint32_t elapsed = now - lastFeed;
            lastFeed = now;
            if (elapsed > 150) elapsed = 150;

            float dx = ax - bx;
            float dy = ay - by;
            float dz = az - bz;
            float accelDev = sqrtf(dx * dx + dy * dy + dz * dz);
            float alpha = (accelDev < OUTLIER_G) ? BASELINE_ALPHA : BASELINE_ALPHA * 0.1f;
            bx += dx * alpha;
            by += dy * alpha;
            bz += dz * alpha;

            if (gyroValid) {
                corrGx = gx - gbx;
                corrGy = gy - gby;
                corrGz = gz - gbz;
                omegaMag = sqrtf(corrGx * corrGx + corrGy * corrGy + corrGz * corrGz);

                if (accelDev < 0.025f && omegaMag < GYRO_QUIET_DPS) {
                    gbx += corrGx * GYRO_BIAS_ALPHA;
                    gby += corrGy * GYRO_BIAS_ALPHA;
                    gbz += corrGz * GYRO_BIAS_ALPHA;
                }
            } else {
                corrGx = 0.0f;
                corrGy = 0.0f;
                corrGz = 0.0f;
                omegaMag = 0.0f;
            }

            float effectiveOmega = (omegaMag > 1.5f) ? (omegaMag - 1.5f) : 0.0f;
            float combinedDev = accelDev + effectiveOmega * GYRO_TO_G_WEIGHT;
            activity += (combinedDev - activity) * ACTIVITY_ALPHA;

            if (activity > ACTIVITY_THRESHOLD) {
                lastActiveSample = now;
                activeAccum += elapsed;
            } else if ((now - lastActiveSample) > QUIET_GAP_MS) {
                activeAccum = 0;
            }
            if (activeAccum >= SUSTAINED_MS) lastMotion = now ? now : 1;
        }
    };

    SPIClass            imuSpi(HSPI);
    SPISettings         imuSettings(1000000, MSBFIRST, SPI_MODE0);
    MotionDetector      detector;
    bool                imuReady        = false;
    bool                singleReads     = true;
    bool                bigEndian       = false;
    bool                gyroEnabled     = true;
    uint32_t            gyroWakeTime    = 0;
    uint32_t            lastSample      = 0;
    float               flatness        = 1.0f;
    constexpr float     FLAT_MIN        = 0.9f;

    float               lastAx = 0.0f, lastAy = 0.0f, lastAz = 1.0f;
    float               lastGx = 0.0f, lastGy = 0.0f, lastGz = 0.0f;
    float               lastOmega = 0.0f;
    float               lastTempC = NAN;

    uint8_t readReg(uint8_t reg) {
        imuSpi.beginTransaction(imuSettings);
        digitalWrite(IMU_CS_PIN, LOW);
        delayMicroseconds(2);
        imuSpi.transfer(0x80 | (reg & 0x7F));
        uint8_t value = imuSpi.transfer(0x00);
        delayMicroseconds(2);
        digitalWrite(IMU_CS_PIN, HIGH);
        imuSpi.endTransaction();
        delayMicroseconds(5);
        return value;
    }

    void writeReg(uint8_t reg, uint8_t value) {
        imuSpi.beginTransaction(imuSettings);
        digitalWrite(IMU_CS_PIN, LOW);
        delayMicroseconds(2);
        imuSpi.transfer(reg & 0x7F);
        imuSpi.transfer(value);
        delayMicroseconds(2);
        digitalWrite(IMU_CS_PIN, HIGH);
        imuSpi.endTransaction();
        delayMicroseconds(20);
    }

    void setGyroPower(bool enable, uint32_t now) {
        if (gyroEnabled == enable) return;
        writeReg(REG_CTRL7, enable ? 0x03 : 0x01);  // 0x03 = Accel + Gyro, 0x01 = Solo Accel (~50 uA)
        gyroEnabled = enable;
        if (enable) {
            gyroWakeTime = now;
        } else {
            lastGx = lastGy = lastGz = lastOmega = 0.0f;
        }
    }

    void readBlockRaw(uint8_t startReg, uint8_t* raw, uint8_t len) {
        if (singleReads) {
            for (uint8_t i = 0; i < len; i++) raw[i] = readReg(startReg + i);
            return;
        }
        imuSpi.beginTransaction(imuSettings);
        digitalWrite(IMU_CS_PIN, LOW);
        delayMicroseconds(2);
        imuSpi.transfer(0x80 | (startReg & 0x7F));
        for (uint8_t i = 0; i < len; i++) raw[i] = imuSpi.transfer(0x00);
        delayMicroseconds(2);
        digitalWrite(IMU_CS_PIN, HIGH);
        imuSpi.endTransaction();
        delayMicroseconds(5);
    }

    int16_t unpackWord(const uint8_t* raw, uint8_t first, bool be) {
        return be ? (int16_t)((raw[first] << 8) | raw[first + 1])
                  : (int16_t)((raw[first + 1] << 8) | raw[first]);
    }

    void unpackVector(const uint8_t* raw, bool be, float scale, float& x, float& y, float& z) {
        x = (float)unpackWord(raw, 0, be) / scale;
        y = (float)unpackWord(raw, 2, be) / scale;
        z = (float)unpackWord(raw, 4, be) / scale;
    }

    void readAccelOnly(float& ax, float& ay, float& az) {
        uint8_t rawAccel[6];
        readBlockRaw(REG_ACCEL_X, rawAccel, 6);
        unpackVector(rawAccel, bigEndian, ACCEL_LSB_PER_G, ax, ay, az);
    }

    void readAccelAndGyro(float& ax, float& ay, float& az, float& gx, float& gy, float& gz, float& tempC) {
        uint8_t rawTemp[2];
        uint8_t rawAccel[6];
        uint8_t rawGyro[6];
        readBlockRaw(REG_TEMP_L,  rawTemp,  2);
        readBlockRaw(REG_ACCEL_X, rawAccel, 6);
        readBlockRaw(REG_GYRO_X,  rawGyro,  6);

        tempC = (float)unpackWord(rawTemp, 0, bigEndian) / TEMP_LSB_PER_C;
        unpackVector(rawAccel, bigEndian, ACCEL_LSB_PER_G,  ax, ay, az);
        unpackVector(rawGyro,  bigEndian, GYRO_LSB_PER_DPS, gx, gy, gz);
    }

    bool plausibleMagnitude(float g) {
        return g >= 0.7f && g <= 1.3f;
    }

    bool checkSamples(bool useSingleReads, float& average) {
        singleReads = useSingleReads;
        float sumLe = 0.0f;
        float sumBe = 0.0f;
        for (uint8_t i = 0; i < 5; i++) {
            uint8_t raw[6];
            readBlockRaw(REG_ACCEL_X, raw, 6);
            float xl, yl, zl, xb, yb, zb;
            unpackVector(raw, false, ACCEL_LSB_PER_G, xl, yl, zl);
            unpackVector(raw, true,  ACCEL_LSB_PER_G, xb, yb, zb);
            sumLe += sqrtf(xl * xl + yl * yl + zl * zl);
            sumBe += sqrtf(xb * xb + yb * yb + zb * zb);
            delay(20);
        }
        float avgLe = sumLe / 5.0f;
        float avgBe = sumBe / 5.0f;
        if (plausibleMagnitude(avgLe)) {
            bigEndian = false;
            average   = avgLe;
            return true;
        }
        if (plausibleMagnitude(avgBe)) {
            bigEndian = true;
            average   = avgBe;
            return true;
        }
        average = avgLe;
        return false;
    }

}

namespace IMU_Utils {

    void setup() {
        #ifdef SD_CS_PIN
            pinMode(SD_CS_PIN, OUTPUT);
            digitalWrite(SD_CS_PIN, HIGH);
        #endif
        pinMode(IMU_CS_PIN, OUTPUT);
        digitalWrite(IMU_CS_PIN, HIGH);
        imuSpi.begin(IMU_SCK_PIN, IMU_MISO_PIN, IMU_MOSI_PIN, -1);

        const uint8_t modes[] = {SPI_MODE0, SPI_MODE3};
        float   average = 0;
        bool    found   = false;

        for (uint8_t m = 0; m < 2 && !found; m++) {
            int modeNumber = (modes[m] == SPI_MODE0) ? 0 : 3;
            imuSettings = SPISettings(1000000, MSBFIRST, modes[m]);

            uint8_t id  = readReg(REG_WHO_AM_I);
            uint8_t rev = readReg(REG_REVISION);
            if (id != WHO_AM_I_VALUE) {
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "IMU",
                           "modo SPI %d: id=0x%02X rev=0x%02X (esperado 0x05)",
                           modeNumber, id, rev);
                continue;
            }

            writeReg(REG_CTRL7, 0x00);
            writeReg(REG_CTRL2, 0x17);              // accel: +-4 g, 62.5 Hz
            writeReg(REG_CTRL3, 0x47);              // gyro:  +-256 dps, 62.5 Hz
            writeReg(REG_CTRL7, 0x03);              // activar accel + gyro
            gyroEnabled  = true;
            gyroWakeTime = millis();
            delay(80);

            if (checkSamples(true, average)) {
                found = true;
                float sumGx = 0.0f, sumGy = 0.0f, sumGz = 0.0f;
                float ax = 0, ay = 0, az = 1.0f, gx = 0, gy = 0, gz = 0, tempC = 25.0f;
                for (uint8_t i = 0; i < 8; i++) {
                    readAccelAndGyro(ax, ay, az, gx, gy, gz, tempC);
                    sumGx += gx; sumGy += gy; sumGz += gz;
                    delay(15);
                }
                lastAx = ax; lastAy = ay; lastAz = az;
                lastTempC = tempC;
                detector.initBias(ax, ay, az, sumGx / 8.0f, sumGy / 8.0f, sumGz / 8.0f, millis());
                logger.log(logging::LoggerLevel::LOGGER_LEVEL_INFO, "IMU",
                           "QMI8658 OK en modo SPI %d (rev 0x%02X, %.2f g, gyro bias %.1f/%.1f/%.1f dps, %.1f C, %s)",
                           modeNumber, rev, average, sumGx / 8.0f, sumGy / 8.0f, sumGz / 8.0f, tempC,
                           bigEndian ? "big-endian" : "little-endian");
                break;
            }
        }

        if (!found) {
            logger.log(logging::LoggerLevel::LOGGER_LEVEL_WARN, "IMU", "QMI8658 sin datos plausibles, desactivado");
            imuSpi.end();
            return;
        }
        imuReady = true;
    }

    void update() {
        if (!imuReady) return;
        uint32_t now = millis();
        uint32_t period = gyroEnabled ? SAMPLE_PERIOD_MOVING_MS : SAMPLE_PERIOD_IDLE_MS;
        if (now - lastSample < period) return;
        lastSample = now;

        float ax, ay, az, rawGx = 0.0f, rawGy = 0.0f, rawGz = 0.0f, tempC = lastTempC;
        bool gyroSettled = gyroEnabled && ((now - gyroWakeTime) >= 60);

        if (gyroEnabled) {
            readAccelAndGyro(ax, ay, az, rawGx, rawGy, rawGz, tempC);
            lastTempC = tempC;
        } else {
            readAccelOnly(ax, ay, az);
        }
        lastAx = ax;
        lastAy = ay;
        lastAz = az;

        detector.feed(ax, ay, az, rawGx, rawGy, rawGz, gyroSettled, now, lastGx, lastGy, lastGz, lastOmega);

        float magnitude = sqrtf(ax * ax + ay * ay + az * az);
        if (magnitude > 0.5f) flatness += ((fabsf(az) / magnitude) - flatness) * 0.1f;

        // Gestion de energia del giroscopio: encender al instante si hay actividad, apagar tras 15 s de calma
        if (!gyroEnabled && detector.activity > ACTIVITY_THRESHOLD) {
            setGyroPower(true, now);
        } else if (gyroEnabled && !movingNow() && (now - detector.lastActiveSample) > GYRO_SLEEP_AFTER_MS) {
            setGyroPower(false, now);
        }
    }

    bool isReady() {
        return imuReady;
    }

    bool isFlat() {
        return !imuReady || flatness >= FLAT_MIN;
    }

    bool movedSince(uint32_t sinceMs) {
        return detector.lastMotion != 0 && (int32_t)(detector.lastMotion - sinceMs) > 0;
    }

    bool movingNow() {
        return detector.lastMotion != 0 && (millis() - detector.lastMotion) < MOVING_HOLD_MS;
    }

    void getAccelG(float& x, float& y, float& z) {
        x = lastAx;
        y = lastAy;
        z = lastAz;
    }

    void getGyroDps(float& x, float& y, float& z) {
        x = lastGx;
        y = lastGy;
        z = lastGz;
    }

    float getAngularSpeedDps() {
        return (imuReady && gyroEnabled) ? lastOmega : 0.0f;
    }

    float getYawRateDps() {
        return (imuReady && gyroEnabled) ? lastGz : 0.0f;
    }

    float getTemperatureC() {
        return imuReady ? lastTempC : NAN;
    }

}

#else

namespace IMU_Utils {
    void  setup() {}
    void  update() {}
    bool  isReady() { return false; }
    bool  movedSince(uint32_t) { return false; }
    bool  isFlat() { return true; }
    bool  movingNow() { return false; }
    void  getAccelG(float& x, float& y, float& z) { x = 0.0f; y = 0.0f; z = 1.0f; }
    void  getGyroDps(float& x, float& y, float& z) { x = 0.0f; y = 0.0f; z = 0.0f; }
    float getAngularSpeedDps() { return 0.0f; }
    float getYawRateDps() { return 0.0f; }
    float getTemperatureC() { return NAN; }
}

#endif