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

#include "nmea0183.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace {

    // ddmm.mmmmm / dddmm.mmmmm (formateado con enteros acotados para evitar -Werror=format-truncation)
    void formatCoordinate(char* out, size_t size, double degrees, bool isLongitude) {
        double absolute = fabs(degrees);
        int    maxWhole = isLongitude ? 180 : 90;
        if (!(absolute <= (double) maxWhole)) absolute = 0.0;

        int  whole        = (int) absolute;
        long scaledMin    = lround((absolute - (double) whole) * 60.0 * 100000.0);
        if (scaledMin >= 6000000L) {                // evita "60.00000" por redondeo
            scaledMin -= 6000000L;
            whole     += 1;
        }
        if (whole > maxWhole) whole = maxWhole;

        int minInt  = (int) (scaledMin / 100000L) % 60;
        int minFrac = (int) (scaledMin % 100000L);

        if (isLongitude) snprintf(out, size, "%03d%02d.%05d", whole % 1000, minInt, minFrac);
        else             snprintf(out, size, "%02d%02d.%05d", whole % 100,  minInt, minFrac);
    }

    void formatTime(char* out, size_t size, const NMEA0183::Gnss& g) {
        if (!g.hasTime) { out[0] = 0; return; }
        snprintf(out, size, "%02u%02u%02u.%02u",
                 (unsigned) g.hour   % 100U,
                 (unsigned) g.minute % 100U,
                 (unsigned) g.second % 100U,
                 (unsigned) g.centis % 100U);
    }

    size_t finish(char lead, const char* body, char* buffer, size_t size) {
        int written = snprintf(buffer, size, "%c%s*%02X\r\n", lead, body, NMEA0183::checksum(body));
        if (written < 0 || (size_t) written >= size) return 0;
        return (size_t) written;
    }

    // ---------- AIS ----------
    struct Bits {
        uint8_t data[56] = {0};             // hasta 448 bits (el mensaje 5 tiene 424)
        int     count    = 0;
        void put(uint32_t value, int bits) {
            for (int i = bits - 1; i >= 0; i--) {
                if (count >= (int) sizeof(data) * 8) return;
                if ((value >> i) & 1U) data[count >> 3] |= (uint8_t) (0x80 >> (count & 7));
                count++;
            }
        }
        void putSigned(int32_t value, int bits) {
            put((uint32_t) value & (bits >= 32 ? 0xFFFFFFFFU : ((1U << bits) - 1U)), bits);
        }
        uint32_t extract(int start, int bits) const {
            uint32_t value = 0;
            for (int i = 0; i < bits; i++) {
                int position = start + i;
                value = (value << 1) | ((data[position >> 3] >> (7 - (position & 7))) & 1U);
            }
            return value;
        }
    };

    uint8_t sixBit(char c) {
        if (c >= 'a' && c <= 'z') c = (char) (c - 32);
        if (c >= 64 && c <= 95) return (uint8_t) (c - 64);
        if (c >= 32 && c <= 63) return (uint8_t) c;
        return 32;
    }

    void putText(Bits& bits, const char* text, int characters) {
        bool ended = false;
        for (int i = 0; i < characters; i++) {
            char c = 0;
            if (!ended) {
                c = text[i];
                if (c == 0) ended = true;
            }
            bits.put(ended ? 0 : sixBit(c), 6);         // 0 = '@' (relleno)
        }
    }

    char armor(uint8_t value) {
        value = (uint8_t) ((value & 0x3F) + 48);
        if (value > 87) value = (uint8_t) (value + 8);
        return (char) value;
    }

    size_t vdm(const Bits& bits, char* buffer, size_t size) {
        int  characters = (bits.count + 5) / 6;
        int  fill       = characters * 6 - bits.count;
        char payload[80];
        if (characters >= (int) sizeof(payload)) return 0;
        for (int i = 0; i < characters; i++) {
            uint32_t value = 0;
            for (int b = 0; b < 6; b++) {
                int position = i * 6 + b;
                value <<= 1;
                if (position < bits.count) value |= (bits.data[position >> 3] >> (7 - (position & 7))) & 1U;
            }
            payload[i] = armor((uint8_t) value);
        }
        payload[characters] = 0;
        char body[96];
        snprintf(body, sizeof(body), "AIVDM,1,1,,B,%s,%d", payload, fill);
        return finish('!', body, buffer, size);
    }

    // Mensaje largo: se reparte en frases VDM de hasta 56 caracteres de carga util (limite de 82 bytes por frase)
    size_t vdmMulti(const Bits& bits, uint8_t sequence, char* buffer, size_t size) {
        const int PART = 56;
        int  characters = (bits.count + 5) / 6;
        int  fill       = characters * 6 - bits.count;
        char payload[96];
        if (characters >= (int) sizeof(payload)) return 0;
        for (int i = 0; i < characters; i++) {
            uint32_t value = 0;
            for (int b = 0; b < 6; b++) {
                int position = i * 6 + b;
                value <<= 1;
                if (position < bits.count) value |= (bits.data[position >> 3] >> (7 - (position & 7))) & 1U;
            }
            payload[i] = armor((uint8_t) value);
        }
        int parts = (characters + PART - 1) / PART;
        size_t used = 0;
        for (int part = 0; part < parts; part++) {
            int first = part * PART;
            int count = characters - first < PART ? characters - first : PART;
            char chunk[PART + 1];
            memcpy(chunk, payload + first, (size_t) count);
            chunk[count] = 0;
            char body[120];
            snprintf(body, sizeof(body), "AIVDM,%d,%d,%u,B,%s,%d", parts, part + 1, (unsigned) (sequence % 10), chunk, part == parts - 1 ? fill : 0);
            char line[100];
            size_t n = finish('!', body, line, sizeof(line));
            if (n == 0 || used + n >= size) return 0;
            memcpy(buffer + used, line, n);
            used += n;
        }
        buffer[used] = 0;
        return used;
    }

    uint32_t fnv1a(const char* text, uint8_t salt) {
        uint32_t hash = 2166136261UL;
        for (const char* p = text; *p; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = (char) (c - 32);
            hash ^= (uint8_t) c;
            hash *= 16777619UL;
        }
        hash ^= salt;
        hash *= 16777619UL;
        return hash;
    }

}

namespace NMEA0183 {

    uint8_t checksum(const char* body) {
        uint8_t value = 0;
        for (const char* p = body; *p; p++) value ^= (uint8_t) *p;
        return value;
    }

    size_t finish(char lead, const char* body, char* buffer, size_t size) {
        return ::finish(lead, body, buffer, size);
    }

    size_t gga(const Gnss& g, char* buffer, size_t size) {
        char time[24], lat[24], lon[24], body[160];
        formatTime(time, sizeof(time), g);
        if (!g.valid) {
            snprintf(body, sizeof(body), "GNGGA,%s,,,,,0,00,,,M,,M,,", time);
            return finish('$', body, buffer, size);
        }
        formatCoordinate(lat, sizeof(lat), g.lat, false);
        formatCoordinate(lon, sizeof(lon), g.lon, true);
        char altitude[24] = "";
        if (g.hasAltitude) snprintf(altitude, sizeof(altitude), "%.1f", g.altitudeM);
        snprintf(body, sizeof(body), "GNGGA,%s,%s,%c,%s,%c,1,%02u,%.1f,%s,M,,M,,", time, lat, g.lat >= 0 ? 'N' : 'S', lon, g.lon >= 0 ? 'E' : 'W',
                 (unsigned) g.satellites % 100U, g.hdop, altitude);
        return finish('$', body, buffer, size);
    }

    size_t rmc(const Gnss& g, char* buffer, size_t size) {
        char time[24], lat[24], lon[24], date[16] = "", body[160];
        formatTime(time, sizeof(time), g);
        if (g.hasDate) {
            snprintf(date, sizeof(date), "%02u%02u%02u",
                     (unsigned) g.day % 100U, (unsigned) g.month % 100U, (unsigned) (g.year % 100));
        }
        if (!g.valid) {
            snprintf(body, sizeof(body), "GNRMC,%s,V,,,,,,,%s,,,N", time, date);
            return finish('$', body, buffer, size);
        }
        formatCoordinate(lat, sizeof(lat), g.lat, false);
        formatCoordinate(lon, sizeof(lon), g.lon, true);
        snprintf(body, sizeof(body), "GNRMC,%s,A,%s,%c,%s,%c,%.2f,%.2f,%s,,,A", time, lat, g.lat >= 0 ? 'N' : 'S', lon, g.lon >= 0 ? 'E' : 'W',
                 g.sogKnots, g.cogDegrees, date);
        return finish('$', body, buffer, size);
    }

    size_t vtg(const Gnss& g, char* buffer, size_t size) {
        char body[96];
        if (!g.valid) {
            snprintf(body, sizeof(body), "GNVTG,,T,,M,,N,,K,N");
        } else {
            snprintf(body, sizeof(body), "GNVTG,%.2f,T,,M,%.2f,N,%.2f,K,A", g.cogDegrees, g.sogKnots, g.sogKnots * 1.852);
        }
        return finish('$', body, buffer, size);
    }

    size_t gll(const Gnss& g, char* buffer, size_t size) {
        char time[24], lat[24], lon[24], body[128];
        formatTime(time, sizeof(time), g);
        if (!g.valid) {     // sin fix: campos de posicion vacios, estado V y modo N (gpsd NMEA.html)
            snprintf(body, sizeof(body), "GNGLL,,,,,%s,V,N", g.hasTime ? time : "");
            return finish('$', body, buffer, size);
        }
        formatCoordinate(lat, sizeof(lat), g.lat, false);
        formatCoordinate(lon, sizeof(lon), g.lon, true);
        snprintf(body, sizeof(body), "GNGLL,%s,%c,%s,%c,%s,A,A", lat, g.lat >= 0 ? 'N' : 'S', lon, g.lon >= 0 ? 'E' : 'W', time);
        return finish('$', body, buffer, size);
    }

    size_t zda(const Gnss& g, char* buffer, size_t size) {
        if (!g.hasTime || !g.hasDate) return 0;
        char time[24], body[96];
        formatTime(time, sizeof(time), g);
        snprintf(body, sizeof(body), "GNZDA,%s,%02u,%02u,%04u,00,00",
                 time, (unsigned) g.day % 100U, (unsigned) g.month % 100U, (unsigned) g.year % 10000U);
        return finish('$', body, buffer, size);
    }

    size_t mda(const Environment& e, char* buffer, size_t size) {
        if (!e.hasTemperature && !e.hasHumidity && !e.hasPressure) return 0;
        char inches[24] = "", bars[24] = "", air[24] = "", humidity[24] = "", dew[24] = "", body[200];
        if (e.hasPressure) {
            snprintf(inches, sizeof(inches), "%.4f", e.pressureHpa * 0.02952998);
            snprintf(bars, sizeof(bars), "%.4f", e.pressureHpa / 1000.0);
        }
        if (e.hasTemperature) snprintf(air, sizeof(air), "%.1f", e.temperatureC);
        if (e.hasHumidity) snprintf(humidity, sizeof(humidity), "%.1f", e.humidityPct);
        if (e.hasHumidity && e.hasTemperature && e.humidityPct > 0.0) {         // punto de rocio (Magnus)
            double gamma = log(e.humidityPct / 100.0) + (17.62 * e.temperatureC) / (243.12 + e.temperatureC);
            snprintf(dew, sizeof(dew), "%.1f", (243.12 * gamma) / (17.62 - gamma));
        }
        snprintf(body, sizeof(body), "WIMDA,%s,I,%s,B,%s,C,,C,%s,,%s,C,,T,,M,,N,,M", inches, bars, air, humidity, dew);
        return finish('$', body, buffer, size);
    }

    size_t xdr(const Environment& e, char* buffer, size_t size) {
        if (!e.hasTemperature && !e.hasHumidity && !e.hasPressure) return 0;
        char body[160];
        size_t used = (size_t) snprintf(body, sizeof(body), "WIXDR");
        if (e.hasTemperature) used += (size_t) snprintf(body + used, sizeof(body) - used, ",C,%.1f,C,TEMP", e.temperatureC);
        if (e.hasHumidity)    used += (size_t) snprintf(body + used, sizeof(body) - used, ",H,%.1f,P,HUMID", e.humidityPct);
        if (e.hasPressure)    used += (size_t) snprintf(body + used, sizeof(body) - used, ",P,%.5f,B,BARO", e.pressureHpa / 1000.0);
        return finish('$', body, buffer, size);
    }

    uint32_t mmsiFromCallsign(const char* callsign, uint16_t mid, uint8_t attempt) {
        if (mid > 999) mid = 999;
        return 800000000UL + (uint32_t) mid * 100000UL + (fnv1a(callsign, attempt) % 100000UL);
    }

    size_t aisType18(uint32_t mmsi, double lat, double lon, double sogKnots, double cogDegrees, int second, char* buffer, size_t size, bool carrierSense) {
        Bits bits;
        bits.put(18, 6);                                    // tipo de mensaje
        bits.put(0, 2);                                     // repeticion
        bits.put(mmsi, 30);
        bits.put(0, 8);                                     // reservado
        uint32_t sog = 1023;
        if (sogKnots >= 0) { long v = lround(sogKnots * 10.0); sog = (uint32_t) (v > 1022 ? 1022 : v); }
        bits.put(sog, 10);
        bits.put(0, 1);                                     // precision de posicion
        bool valid = lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
        bits.putSigned(valid ? (int32_t) lround(lon * 600000.0) : 108600000, 28);   // 181 grados = no disponible
        bits.putSigned(valid ? (int32_t) lround(lat * 600000.0) : 54600000, 27);    // 91 grados = no disponible
        uint32_t cog = 3600;
        if (cogDegrees >= 0) { long v = lround(cogDegrees * 10.0); cog = (uint32_t) (v > 3599 ? 3599 : v); }
        bits.put(cog, 12);
        bits.put(511, 9);                                   // rumbo verdadero no disponible
        bits.put(second >= 0 && second <= 59 ? (uint32_t) second : 60U, 6);
        bits.put(0, 2);                                     // reservado
        bits.put(carrierSense ? 1 : 0, 1);                  // 1 = clase B "CS", 0 = clase B "SO"
        bits.put(0, 1);                                     // sin pantalla
        bits.put(0, 1);                                     // sin DSC
        bits.put(1, 1);                                     // toda la banda
        bits.put(0, 1);                                     // sin mensaje 22
        bits.put(0, 1);                                     // modo autonomo
        bits.put(0, 1);                                     // RAIM
        bits.put(carrierSense ? 1 : 0, 1);                  // selector de estado de comunicaciones (1 = ITDMA para CS, 0 = SOTDMA para SO)
        bits.put(carrierSense ? 0x30006UL : 0UL, 19);       // valor fijo ITU-R M.1371-6 (0x30006) para Clase B "CS"
        return vdm(bits, buffer, size);
    }

    size_t aisType24A(uint32_t mmsi, const char* name, char* buffer, size_t size) {
        Bits bits;
        bits.put(24, 6);
        bits.put(0, 2);
        bits.put(mmsi, 30);
        bits.put(0, 2);                                     // parte A
        putText(bits, name, 20);
        bits.put(0, 8);                                     // 8 bits spare hasta 168 bits (28 caracteres de 6 bits sin relleno)
        return vdm(bits, buffer, size);
    }

    size_t aisType24B(uint32_t mmsi, uint8_t shipType, const char* callsign, char* buffer, size_t size) {
        return aisType24B(mmsi, shipType, callsign, 0, 0, 0, 0, buffer, size);
    }

    size_t aisType24B(uint32_t mmsi, uint8_t shipType, const char* callsign, uint16_t toBow, uint16_t toStern, uint8_t toPort, uint8_t toStarboard, char* buffer, size_t size) {
        Bits bits;
        bits.put(24, 6);
        bits.put(0, 2);
        bits.put(mmsi, 30);
        bits.put(1, 2);                                     // parte B
        bits.put(shipType, 8);
        putText(bits, "APR", 3);                            // identificador del fabricante (3 caracteres)
        bits.put(0, 4);                                     // modelo
        bits.put(0, 20);                                    // numero de serie
        putText(bits, callsign, 7);
        bits.put(toBow > 511 ? 511 : toBow, 9);             // dimensiones (0 = no disponibles)
        bits.put(toStern > 511 ? 511 : toStern, 9);
        bits.put(toPort > 63 ? 63 : toPort, 6);
        bits.put(toStarboard > 63 ? 63 : toStarboard, 6);
        bits.put(0, 6);                                     // reservado
        return vdm(bits, buffer, size);
    }

    size_t aisType1(uint32_t mmsi, uint8_t navStatus, double lat, double lon, double sogKnots, double cogDegrees, int second, char* buffer, size_t size, double headingDegrees) {
        Bits bits;
        bits.put(1, 6);                                     // informe de posicion de clase A
        bits.put(0, 2);                                     // repeticion
        bits.put(mmsi, 30);
        bits.put(navStatus > 15 ? 15 : navStatus, 4);       // estado de navegacion (0 motor, 1 fondeado, 5 amarrado, 8 a vela, 15 no definido)
        bits.putSigned(-128, 8);                            // velocidad de giro no disponible
        uint32_t sog = 1023;
        if (sogKnots >= 0) { long v = lround(sogKnots * 10.0); sog = (uint32_t) (v > 1022 ? 1022 : v); }
        bits.put(sog, 10);
        bits.put(0, 1);                                     // precision de posicion
        bool valid = lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
        bits.putSigned(valid ? (int32_t) lround(lon * 600000.0) : 108600000, 28);
        bits.putSigned(valid ? (int32_t) lround(lat * 600000.0) : 54600000, 27);
        uint32_t cog = 3600;
        if (cogDegrees >= 0) { long v = lround(cogDegrees * 10.0); cog = (uint32_t) (v > 3599 ? 3599 : v); }
        bits.put(cog, 12);
        bits.put(headingDegrees >= 0 ? (uint32_t) (lround(headingDegrees) % 360) : 511U, 9);   // rumbo verdadero (511 = no disponible)
        bits.put(second >= 0 && second <= 59 ? (uint32_t) second : 60U, 6);
        bits.put(0, 2);                                     // maniobra especial: no disponible
        bits.put(0, 3);                                     // reservado
        bits.put(0, 1);                                     // RAIM
        bits.put(0, 19);                                    // estado de comunicaciones
        return vdm(bits, buffer, size);
    }

    size_t aisType5(uint32_t mmsi, const char* callsign, const char* name, uint8_t shipType, uint16_t toBow, uint16_t toStern, uint8_t toPort,
                    uint8_t toStarboard, uint8_t draughtDm, uint8_t sequence, char* buffer, size_t size) {
        Bits bits;
        bits.put(5, 6);                                     // datos estaticos y de viaje de clase A
        bits.put(0, 2);
        bits.put(mmsi, 30);
        bits.put(0, 2);                                     // version de AIS
        bits.put(0, 30);                                    // numero IMO no disponible
        putText(bits, callsign, 7);
        putText(bits, name, 20);
        bits.put(shipType, 8);
        bits.put(toBow > 511 ? 511 : toBow, 9);
        bits.put(toStern > 511 ? 511 : toStern, 9);
        bits.put(toPort > 63 ? 63 : toPort, 6);
        bits.put(toStarboard > 63 ? 63 : toStarboard, 6);
        bits.put(1, 4);                                     // dispositivo de posicion: GPS
        bits.put(0, 4);                                     // ETA: mes 0 = no disponible
        bits.put(0, 5);                                     //      dia 0
        bits.put(24, 5);                                    //      hora 24
        bits.put(60, 6);                                    //      minuto 60
        bits.put(draughtDm, 8);                             // calado en decimas de metro
        putText(bits, "", 20);                              // destino no disponible
        bits.put(1, 1);                                     // DTE: no disponible
        bits.put(0, 1);                                     // reservado
        return vdmMulti(bits, sequence, buffer, size);
    }

    int AisTable::track(const char* callsign, double lat, double lon, double sogKnots, double cogDeg, uint32_t nowMs, bool& isNew) {
        isNew = false;
        int freeSlot = -1, oldest = 0;
        for (int i = 0; i < MAX_STATIONS; i++) {
            if (stations_[i].used) {
                if (strncmp(stations_[i].callsign, callsign, sizeof(stations_[i].callsign) - 1) == 0) {
                    stations_[i].lat = lat;
                    stations_[i].lon = lon;
                    stations_[i].sogKnots = sogKnots;
                    stations_[i].cogDeg = cogDeg;
                    stations_[i].heardAt = nowMs;
                    return i;
                }
                if ((int32_t) (nowMs - stations_[i].heardAt) > (int32_t) (nowMs - stations_[oldest].heardAt)) oldest = i;
            } else if (freeSlot < 0) {
                freeSlot = i;
            }
        }
        int slot = freeSlot >= 0 ? freeSlot : oldest;
        AisStation& s = stations_[slot];
        s = AisStation();
        s.used = true;
        strncpy(s.callsign, callsign, sizeof(s.callsign) - 1);
        s.callsign[sizeof(s.callsign) - 1] = 0;
        for (uint8_t attempt = 0; attempt < 255; attempt++) {        // MMSI unico dentro de la tabla
            uint32_t candidate = mmsiFromCallsign(s.callsign, mid_, attempt);
            bool taken = false;
            for (int i = 0; i < MAX_STATIONS; i++) {
                if (i != slot && stations_[i].used && stations_[i].mmsi == candidate) { taken = true; break; }
            }
            if (!taken) { s.mmsi = candidate; break; }
        }
        s.lat = lat;
        s.lon = lon;
        s.sogKnots = sogKnots;
        s.cogDeg = cogDeg;
        s.heardAt = nowMs;
        isNew = true;
        return slot;
    }

    int AisTable::nextRefresh(uint32_t nowMs, uint32_t refreshMs, uint32_t ttlMs) {
        for (int i = 0; i < MAX_STATIONS; i++) {
            if (!stations_[i].used) continue;
            if (nowMs - stations_[i].heardAt > ttlMs) continue;
            if (nowMs - stations_[i].emittedAt >= refreshMs) return i;
        }
        return -1;
    }

    bool AisTable::staticDue(int index, uint32_t nowMs, uint32_t periodMs) const {
        return !stations_[index].staticSent || (nowMs - stations_[index].staticAt) >= periodMs;
    }

    void AisTable::setInfo(int index, const AIS_Vessel::Info& info) {
        if (index < 0 || index >= MAX_STATIONS) return;
        // Evitar que dos ranuras distintas compartan el mismo MMSI real si cambio el SSID
        for (int i = 0; i < MAX_STATIONS; i++) {
            if (i != index && stations_[i].used && stations_[i].mmsi == info.mmsi) {
                stations_[i].used = false;
            }
        }
        AisStation& s = stations_[index];
        bool same = s.hasInfo && s.info.mmsi == info.mmsi && s.info.aisClass == info.aisClass && s.info.length == info.length &&
                    s.info.beam == info.beam && s.info.draughtDm == info.draughtDm && s.info.shipType == info.shipType &&
                    strncmp(s.info.name, info.name, sizeof(s.info.name)) == 0;
        if (same) return;
        s.hasInfo    = true;
        s.info       = info;
        s.mmsi       = info.mmsi;                       // el MMSI real sustituye al sintetico
        s.staticSent = false;                           // los datos nuevos se envian enseguida
    }

    void AisTable::markEmitted(int index, uint32_t nowMs) {
        stations_[index].emittedAt = nowMs;
    }

    void AisTable::markStatic(int index, uint32_t nowMs) {
        stations_[index].staticAt = nowMs;
        stations_[index].staticSent = true;
    }

}