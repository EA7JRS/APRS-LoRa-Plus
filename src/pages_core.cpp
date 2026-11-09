/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
 * Copyright (C) 2026 Manuel - EA7JRS (modificaciones y ampliaciones)
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

#include "aprs_devices.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pages_core.h"

namespace PAGES_Core {

    // ---------- Geodesia ----------
    static const double kPi        = 3.14159265358979323846;
    static const double kDegToRad  = kPi / 180.0;
    static const double kRadToDeg  = 180.0 / kPi;
    static const double kEarthKm   = 6371.0088;

    double distanceKm(double lat1, double lon1, double lat2, double lon2) {
        double dLat = (lat2 - lat1) * kDegToRad;
        double dLon = (lon2 - lon1) * kDegToRad;
        double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * kDegToRad) * cos(lat2 * kDegToRad) * sin(dLon / 2) * sin(dLon / 2);
        if (a > 1.0) a = 1.0;
        return 2.0 * kEarthKm * asin(sqrt(a));
    }

    double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
        double p1 = lat1 * kDegToRad, p2 = lat2 * kDegToRad;
        double dLon = (lon2 - lon1) * kDegToRad;
        double y = sin(dLon) * cos(p2);
        double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dLon);
        double b = atan2(y, x) * kRadToDeg;
        b = fmod(b + 360.0, 360.0);
        return b;
    }

    const char* cardinal16(double deg) {
        static const char* const names[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
        double d = fmod(deg, 360.0);
        if (d < 0) d += 360.0;
        int index = (int)((d + 11.25) / 22.5) % 16;
        return names[index];
    }

    float wrap360(float deg) {
        deg = fmodf(deg, 360.0f);
        if (deg < 0) deg += 360.0f;
        if (deg >= 360.0f) deg -= 360.0f;
        return deg;
    }

    float wrap180(float deg) {
        deg = wrap360(deg);
        if (deg > 180.0f) deg -= 360.0f;
        return deg;
    }

    float dewPointC(float tempC, float humidityPct) {
        if (humidityPct < 1.0f) humidityPct = 1.0f;
        if (humidityPct > 100.0f) humidityPct = 100.0f;
        const float a = 17.62f, b = 243.12f;
        float gamma = logf(humidityPct / 100.0f) + (a * tempC) / (b + tempC);
        return (b * gamma) / (a - gamma);
    }

    // ---------- Estaciones ----------
    Kind classify(int type, char symbol, int speedKmh, bool hasPosition) {
        switch (type) {
            case 1: return KIND_MSG;
            case 2: return KIND_STATUS;
            case 3: return KIND_TELEMETRY;
            case 5: return KIND_OBJECT;
            case 0:
            case 4:
                if (!hasPosition) return KIND_UNKNOWN;
                if (symbol == '_') return KIND_WX;
                if (symbol == '#') return KIND_DIGI;
                if (symbol == '&') return KIND_IGATE;
                return (speedKmh >= 3) ? KIND_MOBILE : KIND_FIXED;
        }
        return KIND_UNKNOWN;
    }

    const char* kindName(Kind kind) {
        switch (kind) {
            case KIND_FIXED:     return "FIXED";
            case KIND_MOBILE:    return "MOBILE";
            case KIND_OBJECT:    return "OBJECT";
            case KIND_WX:        return "WX";
            case KIND_DIGI:      return "DIGI";
            case KIND_IGATE:     return "IGATE";
            case KIND_MSG:       return "MSG";
            case KIND_STATUS:    return "STATUS";
            case KIND_TELEMETRY: return "TELEM";
            default:             return "?";
        }
    }

    void HeardList::clear() {
        count_ = 0;
        total_ = 0;
    }

    void HeardList::update(const char* call, char symbol, int type, bool hasPosition, float lat, float lon,
                           int altitude, int speed, int course, int rssi, float snr, uint32_t nowMs,
                           const char* tocall, const char* path, const char* comment, const char* status, char table) {
        if (call == nullptr || call[0] == '\0') return;
        total_++;

        int slot = -1;
        for (uint8_t i = 0; i < count_; i++) {
            if (strncmp(items_[i].call, call, sizeof(items_[i].call) - 1) == 0) { slot = i; break; }
        }
        bool isNew = false;
        if (slot < 0) {
            isNew = true;
            if (count_ < CAPACITY) {
                slot = count_++;
            } else {                                    // lista llena: se sustituye la mas antigua
                slot = 0;
                for (uint8_t i = 1; i < count_; i++) {
                    if ((int32_t)(items_[i].lastMs - items_[slot].lastMs) < 0) slot = i;
                }
            }
            memset(&items_[slot], 0, sizeof(Station));
            snprintf(items_[slot].call, sizeof(items_[slot].call), "%s", call);
            items_[slot].symbol = ' ';
            items_[slot].table = '/';
        }

        Station& s = items_[slot];
        s.type   = (uint8_t)type;
        s.rssi   = (int16_t)rssi;
        s.snr    = snr;
        s.lastMs = nowMs;
        if (s.packets < 65535) s.packets++;

        if (status) { snprintf(s.status, sizeof(s.status), "%s", status); s.status[sizeof(s.status) - 1] = '\0'; }
        if (path && path[0]) { snprintf(s.path, sizeof(s.path), "%s", path); s.path[sizeof(s.path) - 1] = '\0'; }
        if (tocall && tocall[0]) {
            const char* rest = comment;
            char device[sizeof(s.device)];
            deviceName(tocall, type, comment ? comment : "", device, sizeof(device), &rest);
            snprintf(s.device, sizeof(s.device), "%s", device);
            s.device[sizeof(s.device) - 1] = '\0';
            if (comment) comment = rest;
        }
        if (comment && (type == 0 || type == 4 || type == 2 || type == 5)) {
            char clean[sizeof(s.comment)];
            if (type == 5) objectName(comment, clean, sizeof(clean));
            else cleanComment(comment, clean, sizeof(clean));
            if (type == 5 || clean[0]) {
                if (type == 0) {
                    WxData wx;
                    if (parseWx(comment, wx)) s.wx = wx;
                }
                snprintf(s.comment, sizeof(s.comment), "%s", clean);
                s.comment[sizeof(s.comment) - 1] = '\0';
            }
        }

        if (hasPosition) {
            s.hasPosition = true;
            s.lat      = lat;
            s.lon      = lon;
            s.altitude = (int16_t)altitude;
            s.speed    = (int16_t)speed;
            s.course   = (int16_t)course;
            s.posMs    = nowMs;
            if (symbol != '\0' && symbol != ' ') { s.symbol = symbol; s.table = table; }
            s.kind = classify(type, s.symbol, speed, true);
        } else if (isNew) {
            s.kind = classify(type, ' ', 0, false);
        }
    }

    int HeardList::indexOf(const char* call) const {
        if (call == nullptr || call[0] == '\0') return -1;
        for (uint8_t i = 0; i < count_; i++) {
            const Station* s = byRecent(i);
            if (s && strncmp(s->call, call, sizeof(s->call) - 1) == 0) return i;
        }
        return -1;
    }

    const Station* HeardList::byRecent(uint8_t position) const {
        if (position >= count_) return nullptr;
        uint8_t order[CAPACITY];
        for (uint8_t i = 0; i < count_; i++) order[i] = i;
        for (uint8_t i = 1; i < count_; i++) {          // insercion por antiguedad (seguro ante el desbordamiento de millis)
            uint8_t v = order[i];
            int j = i - 1;
            while (j >= 0 && (int32_t)(items_[order[j]].lastMs - items_[v].lastMs) < 0) {
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = v;
        }
        return &items_[order[position]];
    }

    // ---------- Perfil de altitud ----------
    void AltProfile::clear() {
        count_ = 0;
        stepMs_ = BASE_STEP_MS;
        started_ = false;
        haveLast_ = false;
        vspeed_ = 0;
        ascent_ = descent_ = 0;
        haveRef_ = false;
    }

    void AltProfile::add(float altitudeM, uint32_t nowMs) {
        if (!(altitudeM > -1000.0f && altitudeM < 20000.0f)) return;      // descarta NaN e imposibles

        if (!haveLast_) {
            lastAlt_ = altitudeM;
            lastAltMs_ = nowMs;
            haveLast_ = true;
        } else if (nowMs - lastAltMs_ >= BASE_STEP_MS) {
            float dtMin = (float)(nowMs - lastAltMs_) / 60000.0f;
            float instant = (altitudeM - lastAlt_) / dtMin;
            vspeed_ = vspeed_ * 0.75f + instant * 0.25f;
            lastAlt_ = altitudeM;
            lastAltMs_ = nowMs;
        }

        if (!haveRef_) {
            refAlt_ = altitudeM;
            haveRef_ = true;
        } else if (altitudeM - refAlt_ >= 3.0f) {
            ascent_ += altitudeM - refAlt_;
            refAlt_ = altitudeM;
        } else if (refAlt_ - altitudeM >= 3.0f) {
            descent_ += refAlt_ - altitudeM;
            refAlt_ = altitudeM;
        }

        if (started_ && (nowMs - lastAddMs_) < stepMs_) return;
        if (count_ >= CAPACITY) {                                           // se promedian parejas y se duplica el intervalo
            for (uint8_t i = 0; i < CAPACITY / 2; i++) values_[i] = (values_[2 * i] + values_[2 * i + 1]) * 0.5f;
            count_ = CAPACITY / 2;
            stepMs_ *= 2;
        }
        values_[count_++] = altitudeM;
        lastAddMs_ = nowMs;
        started_ = true;
    }

    float AltProfile::minimum() const {
        if (count_ == 0) return 0;
        float m = values_[0];
        for (uint8_t i = 1; i < count_; i++) if (values_[i] < m) m = values_[i];
        return m;
    }

    float AltProfile::maximum() const {
        if (count_ == 0) return 0;
        float m = values_[0];
        for (uint8_t i = 1; i < count_; i++) if (values_[i] > m) m = values_[i];
        return m;
    }

    // ---------- Tendencia barometrica ----------
    void PressureTrend::clear() {
        count_ = 0;
        started_ = false;
    }

    void PressureTrend::add(float pressureHpa, uint32_t nowMs) {
        if (!(pressureHpa > 300.0f && pressureHpa < 1100.0f)) return;
        if (started_ && (nowMs - lastMs_) < STEP_MS) return;
        if (count_ >= CAPACITY) {
            for (uint8_t i = 1; i < CAPACITY; i++) values_[i - 1] = values_[i];
            count_ = CAPACITY - 1;
        }
        values_[count_++] = pressureHpa;
        lastMs_ = nowMs;
        started_ = true;
    }

    bool PressureTrend::delta3h(float& deltaHpa) const {
        if (count_ < 7) return false;                                       // menos de 1 h de historia
        float spanMin = (float)(count_ - 1) * (STEP_MS / 60000.0f);
        float delta = values_[count_ - 1] - values_[0];
        deltaHpa = delta * (180.0f / spanMin);
        if (spanMin > 180.0f) deltaHpa = delta;                             // nunca ocurre con CAPACITY=19; por seguridad
        return true;
    }

    Trend PressureTrend::classifyDelta(float d) {
        // Umbrales por 3 h (terminologia del Met Office): lento hasta 1,5 hPa; normal hasta 3,5; rapido por encima.
        // Por debajo de 0,5 hPa se considera estable (ruido del sensor).
        float a = fabsf(d);
        if (a < 0.5f) return TREND_STEADY;
        if (d > 0) return (a < 1.6f) ? TREND_RISING_SLOW : (a < 3.6f) ? TREND_RISING : TREND_RISING_FAST;
        return (a < 1.6f) ? TREND_FALLING_SLOW : (a < 3.6f) ? TREND_FALLING : TREND_FALLING_FAST;
    }

    Trend PressureTrend::trend() const {
        float d;
        if (!delta3h(d)) return TREND_UNKNOWN;
        return classifyDelta(d);
    }

    const char* PressureTrend::trendName(Trend t) {
        switch (t) {
            case TREND_FALLING_FAST: return "FALLING FAST";
            case TREND_FALLING:      return "FALLING";
            case TREND_FALLING_SLOW: return "FALLING SLOW";
            case TREND_STEADY:       return "STEADY";
            case TREND_RISING_SLOW:  return "RISING SLOW";
            case TREND_RISING:       return "RISING";
            case TREND_RISING_FAST:  return "RISING FAST";
            default:                 return "WAIT 1h";
        }
    }

    const char* PressureTrend::trendShort(Trend t) {
        switch (t) {
            case TREND_FALLING_FAST: return "FALL FAST";
            case TREND_FALLING:      return "FALLING";
            case TREND_FALLING_SLOW: return "FALL SLOW";
            case TREND_STEADY:       return "STEADY";
            case TREND_RISING_SLOW:  return "RISE SLOW";
            case TREND_RISING:       return "RISING";
            case TREND_RISING_FAST:  return "RISE FAST";
            default:                 return "WAIT 1h";
        }
    }

    // ---------- Registro de mensajes ----------
    void MessageLog::copy(char* destination, size_t size, const char* source) {
        size_t i = 0;
        for (; source && source[i] && i + 1 < size; i++) {
            char c = source[i];
            destination[i] = (c == '\n' || c == '\r' || c == '|') ? ' ' : c;
        }
        destination[i] = '\0';
    }

    void MessageLog::push(const MsgEntry& entry) {
        if (count_ == CAPACITY) {                                // lleno: se descarta el mas antiguo
            for (uint8_t i = 1; i < CAPACITY; i++) items_[i - 1] = items_[i];
            count_--;
        }
        items_[count_++] = entry;
    }

    void MessageLog::append(const MsgEntry& entry) {
        push(entry);
    }

    void MessageLog::addReceived(const char* call, const char* text, uint32_t epoch) {
        MsgEntry e;
        memset(&e, 0, sizeof(e));
        e.dir = 'R';
        e.epoch = epoch;
        copy(e.call, sizeof(e.call), call);
        copy(e.text, sizeof(e.text), text);
        push(e);
    }

    void MessageLog::addSent(const char* call, const char* text, uint32_t epoch, uint16_t ackId) {
        MsgEntry e;
        memset(&e, 0, sizeof(e));
        e.dir = 'T';
        e.flags = MSG_READ | (ackId > 0 ? MSG_ACK_REQ : 0);
        e.epoch = epoch;
        e.ackId = ackId;
        copy(e.call, sizeof(e.call), call);
        copy(e.text, sizeof(e.text), text);
        push(e);
    }

    uint8_t MessageLog::unread() const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < count_; i++) if (items_[i].dir == 'R' && !(items_[i].flags & MSG_READ)) n++;
        return n;
    }

    const MsgEntry* MessageLog::byRecent(uint8_t position) const {
        if (position >= count_) return nullptr;
        return &items_[count_ - 1 - position];
    }

    bool MessageLog::markAcked(const char* call, uint16_t ackId) {
        for (int i = count_ - 1; i >= 0; i--) {
            MsgEntry& e = items_[i];
            if (e.dir == 'T' && e.ackId == ackId && ackId > 0 && strcmp(e.call, call) == 0) {
                e.flags = (e.flags | MSG_ACKED) & ~MSG_FAILED;
                return true;
            }
        }
        return false;
    }

    bool MessageLog::markFailed(const char* call, uint16_t ackId) {
        for (int i = count_ - 1; i >= 0; i--) {
            MsgEntry& e = items_[i];
            if (e.dir == 'T' && e.ackId == ackId && ackId > 0 && strcmp(e.call, call) == 0) {
                if (!(e.flags & MSG_ACKED)) e.flags |= MSG_FAILED;
                return true;
            }
        }
        return false;
    }

    bool MessageLog::markRead(uint8_t position) {
        if (position >= count_) return false;
        MsgEntry& e = items_[count_ - 1 - position];
        if (e.flags & MSG_READ) return false;
        e.flags |= MSG_READ;
        return true;
    }

    bool MessageLog::markAllRead() {
        bool changed = false;
        for (uint8_t i = 0; i < count_; i++) {
            if (!(items_[i].flags & MSG_READ)) { items_[i].flags |= MSG_READ; changed = true; }
        }
        return changed;
    }

    bool MessageLog::removeRecent(uint8_t position) {
        if (position >= count_) return false;
        uint8_t index = count_ - 1 - position;
        for (uint8_t i = index + 1; i < count_; i++) items_[i - 1] = items_[i];
        count_--;
        return true;
    }

    MsgState MessageLog::stateOf(const MsgEntry& e) {
        if (e.dir == 'R') return (e.flags & MSG_READ) ? MSTATE_READ : MSTATE_UNREAD;
        if (e.flags & MSG_ACKED) return MSTATE_ACKED;
        if (e.flags & MSG_FAILED) return MSTATE_FAILED;
        if (e.flags & MSG_ACK_REQ) return MSTATE_WAITING;
        return MSTATE_SENT;
    }

    void MessageLog::format(const MsgEntry& e, char* out, size_t size) {
        snprintf(out, size, "%c|%lu|%u|%u|%s|%s", e.dir, (unsigned long)e.epoch, (unsigned)e.flags, (unsigned)e.ackId, e.call, e.text);
    }

    bool MessageLog::parse(const char* line, MsgEntry& e) {
        memset(&e, 0, sizeof(e));
        if (!line || (line[0] != 'R' && line[0] != 'T') || line[1] != '|') return false;
        e.dir = line[0];
        const char* p = line + 2;
        char* end;
        unsigned long epoch = strtoul(p, &end, 10);
        if (*end != '|') return false;
        p = end + 1;
        unsigned long flags = strtoul(p, &end, 10);
        if (*end != '|') return false;
        p = end + 1;
        unsigned long ackId = strtoul(p, &end, 10);
        if (*end != '|') return false;
        p = end + 1;
        const char* bar = strchr(p, '|');
        if (!bar) return false;
        size_t callLength = (size_t)(bar - p);
        if (callLength == 0 || callLength >= sizeof(e.call)) return false;
        memcpy(e.call, p, callLength);
        e.call[callLength] = '\0';
        copy(e.text, sizeof(e.text), bar + 1);
        while (e.text[0] && (e.text[strlen(e.text) - 1] == ' ')) e.text[strlen(e.text) - 1] = '\0';
        e.epoch = (uint32_t)epoch;
        e.flags = (uint8_t)flags;
        e.ackId = (uint16_t)ackId;
        return true;
    }

    bool wrapNext(const char* text, size_t& position, size_t width, char* out, size_t outSize) {
        size_t length = strlen(text);
        while (position < length && text[position] == ' ') position++;
        if (position >= length) return false;
        size_t remaining = length - position;
        size_t take = remaining;
        if (take > width) {
            take = width;
            for (size_t i = width; i > width / 2; i--) {         // corta en el ultimo espacio de la segunda mitad
                if (text[position + i] == ' ') { take = i; break; }
            }
        }
        if (take >= outSize) take = outSize - 1;
        memcpy(out, text + position, take);
        out[take] = '\0';
        while (take > 0 && out[take - 1] == ' ') out[--take] = '\0';
        position += (take == 0) ? 1 : take;
        return true;
    }

    // Fecha civil UTC a partir de segundos (algoritmo de Howard Hinnant)
    static void civil(uint32_t epoch, int& year, int& month, int& day, int& hour, int& minute) {
        int64_t days = (int64_t)(epoch / 86400UL);
        uint32_t secondsOfDay = epoch % 86400UL;
        hour = (int)(secondsOfDay / 3600);
        minute = (int)((secondsOfDay % 3600) / 60);
        days += 719468;
        int64_t era = (days >= 0 ? days : days - 146096) / 146097;
        unsigned dayOfEra = (unsigned)(days - era * 146097);
        unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
        year = (int)(yearOfEra + era * 400);
        unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
        unsigned mp = (5 * dayOfYear + 2) / 153;
        day = (int)(dayOfYear - (153 * mp + 2) / 5 + 1);
        month = (int)(mp < 10 ? mp + 3 : mp - 9);
        if (month <= 2) year++;
    }

    void formatMessageTime(uint32_t epoch, uint32_t nowEpoch, char* out, size_t size) {
        if (epoch == 0) { snprintf(out, size, "--:--"); return; }
        int y, mo, d, h, mi;
        civil(epoch, y, mo, d, h, mi);
        if (nowEpoch != 0 && epoch / 86400UL != nowEpoch / 86400UL) snprintf(out, size, "%02d/%02d", d, mo);
        else snprintf(out, size, "%02d:%02d", h, mi);
    }

    void formatMessageDateTime(uint32_t epoch, char* out, size_t size) {
        if (epoch == 0) { snprintf(out, size, "--/--/---- --:--"); return; }
        int y, mo, d, h, mi;
        civil(epoch, y, mo, d, h, mi);
        snprintf(out, size, "%02d/%02d/%04d %02d:%02d", d, mo, y, h, mi);
    }

    // ---------- Calidad del aire ----------
    void IaqEstimator::clear() {
        baseline_ = 0;
        firstMs_ = lastMs_ = 0;
        samples_ = 0;
        index_ = 0;
    }

    void IaqEstimator::add(float gasKohm, float humidityPct, uint32_t nowMs) {
        if (!(gasKohm > 0.0f) || isnan(gasKohm)) return;
        if (samples_ == 0) {
            firstMs_ = nowMs;
            baseline_ = gasKohm;
        } else {
            // la linea base sube al instante con el aire limpio y baja despacio (~4 %/h) para seguir al sensor
            float hours = (nowMs - lastMs_) / 3600000.0f;
            baseline_ *= (1.0f - 0.04f * hours);
            if (gasKohm > baseline_) baseline_ = gasKohm;
        }
        lastMs_ = nowMs;
        if (samples_ < 65535) samples_++;

        // gas: 75 puntos; resistencia entre 5 kOhm y la linea base
        const float lower = 5.0f;
        float upper = baseline_ > lower + 1.0f ? baseline_ : lower + 1.0f;
        float ratio = (gasKohm - lower) / (upper - lower);
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        float gasScore = 75.0f * ratio;

        // humedad: 25 puntos, optimo 40 %
        float h = humidityPct;
        if (isnan(h)) h = 40.0f;
        if (h < 0.0f) h = 0.0f;
        if (h > 100.0f) h = 100.0f;
        float humScore = (h >= 40.0f) ? ((100.0f - h) / 60.0f) * 25.0f : (h / 40.0f) * 25.0f;

        index_ = (100.0f - (gasScore + humScore)) * 5.0f;
        if (index_ < 0.0f) index_ = 0.0f;
        if (index_ > 500.0f) index_ = 500.0f;
    }

    bool IaqEstimator::ready() const {
        return samples_ >= WARMUP_MIN_SAMPLES && (lastMs_ - firstMs_) >= WARMUP_MS;
    }

    uint32_t IaqEstimator::warmupRemainingSec(uint32_t nowMs) const {
        if (samples_ == 0) return WARMUP_MS / 1000;
        uint32_t elapsed = nowMs - firstMs_;
        return elapsed >= WARMUP_MS ? 0 : (WARMUP_MS - elapsed) / 1000;
    }

    const char* IaqEstimator::labelFor(float index) {
        if (index <= 50.0f)  return "Great";
        if (index <= 100.0f) return "Good";
        if (index <= 150.0f) return "Fair";
        if (index <= 200.0f) return "Poor";
        if (index <= 300.0f) return "Bad";
        return "Awful";
    }

    const char* IaqEstimator::label() const {
        return labelFor(index_);
    }

    // ---------- Fusion de rumbo ----------
    void HeadingFusion::reset() {
        valid_ = false;
        haveLastMeasured_ = false;
        sinceMeasureSec_ = 0;
        source_ = HSRC_NONE;
        votes_ = 0;
    }

    float HeadingFusion::update(float dt, bool gyroValid, float gyroDps, bool magValid, float magHeading,
                                bool gpsValid, float gpsCourse, float speedKmh) {
        if (!(dt > 0.0f)) dt = 0.01f;
        if (dt > 1.0f) dt = 1.0f;

        // aprendizaje del sentido del giroscopio: se compara con la variacion del magnetometro
        if (magValid && gyroValid) {
            if (haveLastMeasured_ && fabsf(gyroDps) > 20.0f) {
                float rate = wrap180(magHeading - lastMeasured_) / dt;
                if (fabsf(rate) > 10.0f && fabsf(rate) < 720.0f) {
                    votes_ += (gyroDps * rate > 0) ? 1 : -1;
                    if (votes_ > 40) votes_ = 40;
                    if (votes_ < -40) votes_ = -40;
                    if (votes_ >= 5) gyroSign_ = 1;
                    else if (votes_ <= -5) gyroSign_ = -1;
                }
            }
        }
        if (magValid) { lastMeasured_ = magHeading; haveLastMeasured_ = true; }
        else haveLastMeasured_ = false;

        // prediccion con el giroscopio
        if (valid_ && gyroValid) heading_ = wrap360(heading_ + (float)gyroSign_ * gyroDps * dt);

        // medida: rumbo GPS si hay velocidad suficiente; si no, magnetometro
        bool haveMeasurement = false;
        float measurement = 0, tc = 0.3f;
        HeadingSource src = HSRC_NONE;
        if (gpsValid && speedKmh >= 8.0f) {
            measurement = gpsCourse; src = HSRC_GPS; tc = gyroValid ? 1.0f : 0.5f; haveMeasurement = true;
        } else if (magValid) {
            measurement = magHeading; src = HSRC_MAG; tc = gyroValid ? 0.5f : 0.25f; haveMeasurement = true;
        }

        if (haveMeasurement) {
            if (!valid_) {
                heading_ = wrap360(measurement);
                valid_ = true;
            } else {
                float alpha = 1.0f - expf(-dt / tc);
                heading_ = wrap360(heading_ + alpha * wrap180(measurement - heading_));
            }
            sinceMeasureSec_ = 0;
            source_ = src;
        } else if (valid_) {
            sinceMeasureSec_ += dt;
            float limit = gyroValid ? 10.0f : 3.0f;
            if (sinceMeasureSec_ >= limit) {
                valid_ = false;
                source_ = HSRC_NONE;
            } else {
                source_ = gyroValid ? HSRC_GYRO : source_;
            }
        } else {
            source_ = HSRC_NONE;
        }
        return heading_;
    }

    // ---------- Formato ----------
    void formatAge(uint32_t ageMs, char* out, size_t size) {
        uint32_t s = ageMs / 1000;
        if (s < 60) snprintf(out, size, "%us", (unsigned)s);
        else if (s < 3600) snprintf(out, size, "%um", (unsigned)(s / 60));
        else if (s < 86400) snprintf(out, size, "%uh", (unsigned)(s / 3600));
        else snprintf(out, size, "%ud", (unsigned)(s / 86400));
    }

    void formatDistance(double km, char* out, size_t size) {
        if (km < 0) km = 0;
        if (km < 1.0) snprintf(out, size, "%dm", (int)lround(km * 1000.0));
        else if (km < 10.0) snprintf(out, size, "%.2fkm", km);
        else if (km < 100.0) snprintf(out, size, "%.1fkm", km);
        else snprintf(out, size, "%.0fkm", km);
    }

    void formatUptime(uint32_t sec, char* out, size_t size) {
        if (sec >= 86400) snprintf(out, size, "%ud%02uh", (unsigned)(sec / 86400), (unsigned)((sec % 86400) / 3600));
        else if (sec >= 3600) snprintf(out, size, "%uh%02um", (unsigned)(sec / 3600), (unsigned)((sec % 3600) / 60));
        else snprintf(out, size, "%um%02us", (unsigned)(sec / 60), (unsigned)(sec % 60));
    }

    const char* hdopQuality(float hdop) {
        if (!(hdop > 0)) return "NONE";
        if (hdop <= 1.0f) return "IDEAL";
        if (hdop <= 2.0f) return "EXCEL";
        if (hdop <= 5.0f) return "GOOD";
        if (hdop <= 10.0f) return "MODER";
        if (hdop <= 20.0f) return "FAIR";
        return "POOR";
    }

    namespace {
        // Lee n cifras (con signo opcional); devuelve false si hay puntos o espacios (dato ausente)
        bool readNumber(const char* p, int digits, int& value) {
            int sign = 1;
            if (*p == '-') { sign = -1; p++; digits--; }
            int v = 0;
            for (int i = 0; i < digits; i++) {
                if (p[i] < '0' || p[i] > '9') return false;
                v = v * 10 + (p[i] - '0');
            }
            value = v * sign;
            return true;
        }
    }

    bool parseWx(const char* text, WxData& out) {
        memset(&out, 0, sizeof(out));
        out.tempC = out.humidity = out.pressureHpa = out.windDir = out.windKmh = out.gustKmh = out.rainMm10 = WX_NA;
        if (text == nullptr) return false;
        const char* p = text;
        int v;
        // viento al inicio: "DDD/SSS" (SSS en mph)
        if (strlen(p) >= 7 && p[3] == '/') {
            if (readNumber(p, 3, v)) out.windDir = (int16_t)v;
            if (readNumber(p + 4, 3, v)) out.windKmh = (int16_t)lroundf(v * 1.609f);
            p += 7;
        }
        int found = (out.windDir != WX_NA || out.windKmh != WX_NA) ? 1 : 0;
        while (*p) {
            char tag = *p++;
            int digits = 0;
            switch (tag) {
                case 'c': case 's': case 'g': case 'r': case 'p': case 'P': digits = 3; break;
                case 't': digits = 3; break;
                case 'h': digits = 2; break;
                case 'b': digits = 5; break;
                default: break;
            }
            if (digits == 0) { if (found) break; else continue; }
            if (strlen(p) < (size_t)digits) break;
            bool ok = readNumber(p, digits, v);
            if (!ok && tag != 'c' && tag != 's' && tag != 'g') {
                // Si no son digitos ni puntos de dato ausente ("..."), seguir buscando mas adelante
                if (!(p[0] == '.' || p[0] == ' ')) continue;
            }
            p += digits;
            if (!ok) continue;
            found++;
            switch (tag) {
                case 'c': out.windDir = (int16_t)v; break;
                case 's': out.windKmh = (int16_t)lroundf(v * 1.609f); break;
                case 'g': out.gustKmh = (int16_t)lroundf(v * 1.609f); break;
                case 't': out.tempC = (int16_t)lroundf((v - 32) * 5.0f / 9.0f); break;
                case 'r': out.rainMm10 = (int16_t)lroundf(v * 2.54f); break;       // centesimas de pulgada -> decimas de mm
                case 'h': out.humidity = (int16_t)(v == 0 ? 100 : v); break;
                case 'b': out.pressureHpa = (int16_t)lroundf(v / 10.0f); break;
                default: break;
            }
        }
        out.valid = out.tempC != WX_NA || out.humidity != WX_NA || out.pressureHpa != WX_NA || out.windKmh != WX_NA;
        return out.valid;
    }

    const char* miceStatusName(const char* bits) {
        if (bits == nullptr) return "";
        static const char* const names[8] = { "Emergency!", "Priority", "Special", "Committed", "Returning", "In Service", "En Route", "Off Duty" };
        if (strlen(bits) != 3) return "";
        int index = 0;
        for (int i = 0; i < 3; i++) {
            if (bits[i] != '0' && bits[i] != '1') return "";
            index = index * 2 + (bits[i] - '0');
        }
        return names[index];
    }

    namespace {
        // Coincidencia de un tocall con un patron: ? cualquier caracter, n digito, * el resto. Devuelve literales coincidentes o -1.
        int tocallMatch(const char* pattern, const char* tocall) {
            int literal = 0;
            const char* p = pattern;
            const char* t = tocall;
            for (; *p; p++, t++) {
                if (*p == '*') return literal;
                if (*t == '\0') return -1;
                if (*p == '?') continue;
                if (*p == 'n') { if (*t < '0' || *t > '9') return -1; continue; }
                if (*p != *t) return -1;
                literal++;
            }
            return (*t == '\0' || *t == ' ') ? literal : -1;
        }
    }

    void deviceName(const char* tocall, int type, const char* comment, char* out, size_t size, const char** rest) {
        if (size == 0) return;
        out[0] = '\0';
        if (rest) *rest = comment;
        if (type == 4) {                                        // Mic-E: el tocall lleva la latitud, el equipo va en el comentario
            const char* name = "Mic-E";
            size_t used = 0;
            size_t len = comment ? strlen(comment) : 0;
            bool found = false;
            // 1) Formato nuevo: sufijo de 2 caracteres al FINAL del comentario (el espacio final puede haberse recortado: "_" = VX-8)
            if (len >= 2) {
                for (int i = 0; i < APRS_MICE_SUFFIX_COUNT && !found; i++) {
                    if (comment[len - 2] == APRS_MICE_SUFFIXES[i].suffix[0] && comment[len - 1] == APRS_MICE_SUFFIXES[i].suffix[1]) {
                        name = APRS_MICE_SUFFIXES[i].name; used = 2; found = true;
                    }
                }
            }
            if (!found && len >= 1 && comment[len - 1] == '_') { name = "Yaesu VX-8"; used = 1; found = true; }
            if (found) {                                        // comentario sin el tipo inicial (` o ') ni el sufijo
                size_t start = (len > used && (comment[0] == '`' || comment[0] == '\'')) ? 1 : 0;
                static char cut[80];
                size_t n = (len - used > start) ? len - used - start : 0;
                if (n >= sizeof(cut)) n = sizeof(cut) - 1;
                memcpy(cut, comment + start, n);
                cut[n] = '\0';
                if (rest) *rest = cut;
                snprintf(out, size, "%s", name);
                return;
            }
            // 2) Formato clasico: prefijo '>' o ']' al inicio y sufijo opcional ('=', '^', '&') al final
            size_t usedEnd = 0;
            if (len >= 1) {
                for (int i = 0; i < APRS_MICE_LEGACY_COUNT; i++) {
                    const AprsMiceLegacy& l = APRS_MICE_LEGACY[i];
                    if (comment[0] != l.prefix) continue;
                    if (l.suffix == 0) { if (!found) { name = l.name; used = 1; found = true; } }
                    else if (len >= 2 && comment[len - 1] == l.suffix) { name = l.name; used = 1; usedEnd = 1; found = true; break; }
                }
            }
            if (found && usedEnd) {
                static char cut2[80];
                size_t n = len - used - usedEnd;
                if (n >= sizeof(cut2)) n = sizeof(cut2) - 1;
                memcpy(cut2, comment + used, n);
                cut2[n] = '\0';
                if (rest) *rest = cut2;
                snprintf(out, size, "%s", name);
                return;
            }
            snprintf(out, size, "%s", name);
            if (rest && comment) *rest = comment + used;
            return;
        }
        struct Entry { const char* pattern; const char* name; };
        static const Entry own[] = {                            // nombres propios del proyecto (tienen prioridad sobre la lista oficial)
            { "APLRT?", "LoRa Tracker" }, { "APLRG?", "LoRa iGate" }, { "APLG??", "LoRa iGate" }, { "APLT??", "LoRa Tracker" },
            { "APLR??", "LoRa APRS" }
        };
        for (size_t i = 0; i < sizeof(own) / sizeof(own[0]); i++) {
            if (tocallMatch(own[i].pattern, tocall) >= 0) { snprintf(out, size, "%s", own[i].name); return; }
        }
        const char* best = nullptr;                             // lista oficial: gana el patron con mas caracteres literales
        int bestScore = -1;
        for (int i = 0; i < APRS_TOCALL_COUNT; i++) {
            int score = tocallMatch(APRS_TOCALLS[i].pattern, tocall);
            if (score > bestScore) { bestScore = score; best = APRS_TOCALLS[i].name; }
        }
        if (best) { snprintf(out, size, "%s", best); return; }
        snprintf(out, size, "%.6s", tocall);                    // desconocido: se muestra el tocall tal cual
    }

    void cleanComment(const char* in, char* out, size_t size) {
        if (size == 0) return;
        size_t n = 0;
        if (in) {
            for (const char* p = in; *p && n + 1 < size; ) {
                // Quitar altitud APRS "/A=001234"
                if (p[0] == '/' && p[1] == 'A' && p[2] == '=' && strlen(p) >= 9) { p += 9; continue; }
                // Quitar bloque de telemetria Base91 "|...|"
                if (p[0] == '|') {
                    const char* closeBar = strchr(p + 1, '|');
                    if (closeBar && (closeBar - p) <= 16) { p = closeBar + 1; continue; }
                }
                // Si viene etiqueta nautica "AIS:C224...,12x4x1.8,36,NOMBRE;", extraer el nombre legible
                if (strncmp(p, "AIS:", 4) == 0) {
                    const char* semi = strchr(p + 4, ';');
                    if (semi) {
                        const char* lastComma = nullptr;
                        for (const char* q = p + 4; q < semi; q++) {
                            if (*q == ',') lastComma = q;
                        }
                        if (lastComma && lastComma + 1 < semi) {
                            for (const char* q = lastComma + 1; q < semi && n + 1 < size; q++) {
                                out[n++] = *q;
                            }
                            if (n + 1 < size && semi[1] && semi[1] != ' ') out[n++] = ' ';
                        }
                        p = semi + 1;
                        continue;
                    }
                }
                char c = *p++;
                out[n++] = (c < 32 || c > 126) ? ' ' : c;
            }
        }
        while (n > 0 && out[n - 1] == ' ') n--;
        out[n] = '\0';
        const char* start = out;
        while (*start == ' ') start++;
        if (start != out) memmove(out, start, strlen(start) + 1);
    }

    void objectName(const char* payload, char* out, size_t size) {
        if (size == 0) return;
        size_t n = 0;
        if (payload) {
            for (int i = 0; i < 9 && payload[i] && n + 1 < size; i++) out[n++] = (payload[i] < 32 || payload[i] > 126) ? ' ' : payload[i];
        }
        while (n > 0 && out[n - 1] == ' ') n--;
        out[n] = '\0';
    }

    uint8_t buildStationView(const HeardList& list, uint8_t sort, uint8_t filter, bool haveMe, double myLat, double myLon, uint8_t* out) {
        double dist[HeardList::CAPACITY];
        uint8_t n = 0;
        for (uint8_t i = 0; i < list.count(); i++) {
            const Station* s = list.byRecent(i);
            if (!s) break;
            bool pass = true;
            switch (filter) {
                case FILTER_POSITION: pass = s->hasPosition; break;
                case FILTER_MOBILE:   pass = s->kind == KIND_MOBILE; break;
                case FILTER_FIXED:    pass = s->kind == KIND_FIXED; break;
                case FILTER_WX:       pass = s->kind == KIND_WX; break;
                case FILTER_DIGI:     pass = s->kind == KIND_DIGI || s->kind == KIND_IGATE; break;
                default: break;
            }
            if (!pass) continue;
            dist[n] = (s->hasPosition && haveMe) ? distanceKm(myLat, myLon, s->lat, s->lon) : 1e12;
            out[n++] = i;
        }
        if (sort == SORT_RECENT) return n;
        for (uint8_t i = 1; i < n; i++) {                       // insercion estable
            uint8_t v = out[i];
            double dv = dist[i];
            int j = i - 1;
            while (j >= 0) {
                bool before;
                if (sort == SORT_NEAREST) before = dist[j] > dv;
                else before = strcmp(list.byRecent(out[j])->call, list.byRecent(v)->call) > 0;
                if (!before) break;
                out[j + 1] = out[j];
                dist[j + 1] = dist[j];
                j--;
            }
            out[j + 1] = v;
            dist[j + 1] = dv;
        }
        return n;
    }

    const char* unitSystemName(uint8_t units) {
        switch (units) {
            case UNITS_IMPERIAL: return "imperial";
            case UNITS_NAUTICAL: return "nautical";
            default:             return "metric";
        }
    }

    const char* coordFormatName(uint8_t format) {
        switch (format) {
            case COORD_DDM: return "DDM";
            case COORD_DMS: return "DMS";
            default:        return "DD";
        }
    }

    void formatDistanceUnits(double km, uint8_t units, char* out, size_t size) {
        if (km < 0) km = 0;
        if (units == UNITS_IMPERIAL) {
            double miles = km * 0.621371;
            if (miles < 0.19) snprintf(out, size, "%dft", (int)lround(km * 3280.84));
            else if (miles < 10.0) snprintf(out, size, "%.2fmi", miles);
            else if (miles < 100.0) snprintf(out, size, "%.1fmi", miles);
            else snprintf(out, size, "%.0fmi", miles);
        } else if (units == UNITS_NAUTICAL) {
            double nm = km * 0.539957;
            if (nm < 0.1) snprintf(out, size, "%dm", (int)lround(km * 1000.0));
            else if (nm < 10.0) snprintf(out, size, "%.2fnm", nm);
            else if (nm < 100.0) snprintf(out, size, "%.1fnm", nm);
            else snprintf(out, size, "%.0fnm", nm);
        } else {
            formatDistance(km, out, size);
        }
    }

    double speedValue(double kmh, uint8_t units) {
        if (units == UNITS_IMPERIAL) return kmh * 0.621371;
        if (units == UNITS_NAUTICAL) return kmh * 0.539957;
        return kmh;
    }

    const char* speedUnit(uint8_t units) {
        if (units == UNITS_IMPERIAL) return "mph";
        if (units == UNITS_NAUTICAL) return "kn";
        return "km/h";
    }

    double altitudeValue(double meters, uint8_t units) {
        return units == UNITS_IMPERIAL ? meters * 3.28084 : meters;
    }

    const char* altitudeUnit(uint8_t units) {
        return units == UNITS_IMPERIAL ? "ft" : "m";
    }

    void formatSpeedUnits(double kmh, uint8_t units, char* out, size_t size) {
        snprintf(out, size, "%.0f%s", speedValue(kmh, units), speedUnit(units));
    }

    void formatAltitudeUnits(double meters, uint8_t units, char* out, size_t size) {
        snprintf(out, size, "%.0f%s", altitudeValue(meters, units), altitudeUnit(units));
    }

    void formatCoordinate(double degrees, bool isLatitude, uint8_t format, char* out, size_t size, int ddDecimals) {
        char hemisphere = isLatitude ? (degrees >= 0 ? 'N' : 'S') : (degrees >= 0 ? 'E' : 'W');
        double value = fabs(degrees);
        if (format == COORD_DMS) {
            int deg = (int)value;
            double minutesFull = (value - deg) * 60.0;
            int min = (int)minutesFull;
            double sec = (minutesFull - min) * 60.0;
            if (sec >= 59.95) { sec = 0.0; min++; }             // el redondeo a una decima no debe dar 60.0
            if (min >= 60) { min = 0; deg++; }
            snprintf(out, size, "%d %02d'%04.1f\"%c", deg, min, sec, hemisphere);
        } else if (format == COORD_DDM) {
            int deg = (int)value;
            double minutes = (value - deg) * 60.0;
            if (minutes >= 59.9995) { minutes = 0.0; deg++; }
            snprintf(out, size, "%d %06.3f%c", deg, minutes, hemisphere);
        } else {
            snprintf(out, size, "%.*f%c", ddDecimals, value, hemisphere);
        }
    }

    namespace {
        // dias desde 1970-01-01 para una fecha civil (algoritmo de Howard Hinnant)
        int64_t daysFromCivil(int y, int m, int d) {
            y -= m <= 2;
            int64_t era = (y >= 0 ? y : y - 399) / 400;
            int64_t yoe = y - era * 400;
            int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + doe - 719468;
        }
        // ultimo domingo del mes (dia del mes)
        int lastSunday(int year, int month) {
            int lastDay = 31;                               // marzo y octubre tienen 31 dias
            int64_t days = daysFromCivil(year, month, lastDay);
            int weekday = (int)(((days % 7) + 11) % 7);     // 1970-01-01 fue jueves (4); 0 = domingo
            return lastDay - weekday;
        }
    }

    void civilFromEpoch(int64_t epoch, int& year, int& month, int& day) {
        int64_t days = epoch >= 0 ? epoch / 86400 : -((-epoch + 86399) / 86400);
        days += 719468;
        int64_t era = (days >= 0 ? days : days - 146096) / 146097;
        int64_t doe = days - era * 146097;
        int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        int64_t y = yoe + era * 400;
        int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        int64_t mp = (5 * doy + 2) / 153;
        day = (int)(doy - (153 * mp + 2) / 5 + 1);
        month = (int)(mp < 10 ? mp + 3 : mp - 9);
        year = (int)(y + (month <= 2));
    }

    int weekdayIndex(int64_t epoch) {
        int64_t days = epoch >= 0 ? epoch / 86400 : -((-epoch + 86399) / 86400);
        return (int)(((days % 7) + 11) % 7);                 // 1970-01-01 fue jueves
    }

    const char* weekdayName(int index) {
        static const char* const names[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
        return names[((index % 7) + 7) % 7];
    }

    bool euDstActive(int64_t utcEpoch) {
        int year, month, day;
        civilFromEpoch(utcEpoch, year, month, day);
        int64_t start = daysFromCivil(year, 3, lastSunday(year, 3)) * 86400 + 3600;     // 01:00 UTC
        int64_t end   = daysFromCivil(year, 10, lastSunday(year, 10)) * 86400 + 3600;
        return utcEpoch >= start && utcEpoch < end;
    }

    int zoneOffsetMinutes(uint8_t mode, int manualHalfHours, int64_t utcEpoch, bool* dst) {
        if (dst) *dst = false;
        if (mode == ZONE_UTC) return 0;
        if (mode == ZONE_MANUAL) return manualHalfHours * 30;
        bool summer = euDstActive(utcEpoch);
        if (dst) *dst = summer;
        return 60 + (summer ? 60 : 0);
    }

    const char* zoneAbbreviation(uint8_t mode, bool dst) {
        if (mode == ZONE_UTC) return "UTC";
        if (mode == ZONE_AUTO_EU) return dst ? "CEST" : "CET";
        return "";
    }

    int sunTimes(double latDeg, double lonDeg, int year, int month, int day, double& sunriseMin, double& sunsetMin) {
        int64_t dayOfYear = daysFromCivil(year, month, day) - daysFromCivil(year, 1, 1) + 1;
        double gamma = 2.0 * kPi / 365.0 * (double)(dayOfYear - 1);                     // al mediodia
        double eqtime = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma) - 0.014615 * cos(2 * gamma) - 0.040849 * sin(2 * gamma));
        double decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma) - 0.006758 * cos(2 * gamma) + 0.000907 * sin(2 * gamma)
                      - 0.002697 * cos(3 * gamma) + 0.00148 * sin(3 * gamma);
        double lat = latDeg * kDegToRad;
        double cosHa = (cos(90.833 * kDegToRad) / (cos(lat) * cos(decl))) - tan(lat) * tan(decl);
        if (cosHa > 1.0) return -1;                                                     // el sol no sale
        if (cosHa < -1.0) return 1;                                                     // el sol no se pone
        double ha = acos(cosHa) * kRadToDeg;
        sunriseMin = 720.0 - 4.0 * (lonDeg + ha) - eqtime;
        sunsetMin  = 720.0 - 4.0 * (lonDeg - ha) - eqtime;
        return 0;
    }

}

// ---------- Radio LoRa ----------
double PAGES_Core::loraTimeOnAirMs(int sf, long bandwidthHz, int cr4, int payloadBytes, int preambleSymbols) {
    if (sf < 6 || sf > 12 || bandwidthHz <= 0 || payloadBytes < 0) return 0.0;
    if (cr4 < 5) cr4 = 5;
    if (cr4 > 8) cr4 = 8;
    double symbolMs = (double)(1UL << sf) * 1000.0 / (double)bandwidthHz;
    int lowDataRate = (sf >= 11 && bandwidthHz <= 125000L) ? 1 : 0;
    double numerator = 8.0 * payloadBytes - 4.0 * sf + 28.0 + 16.0;                     // CRC activo, cabecera explicita
    double denominator = 4.0 * (sf - 2 * lowDataRate);
    double blocks = ceil(numerator / denominator) * cr4;
    if (blocks < 0) blocks = 0;
    double payloadSymbols = 8.0 + blocks;
    return (preambleSymbols + 4.25 + payloadSymbols) * symbolMs;
}

double PAGES_Core::loraBitrate(int sf, long bandwidthHz, int cr4) {
    if (sf < 6 || sf > 12 || bandwidthHz <= 0 || cr4 < 5 || cr4 > 8) return 0.0;
    return sf * (double)bandwidthHz / (double)(1UL << sf) * 4.0 / cr4;
}

void PAGES_Core::AirtimeTracker::clear() {
    for (int i = 0; i < 60; i++) { txBucket_[i] = Bucket{0, 0, false}; rxBucket_[i] = Bucket{0, 0, false}; }
    txTotal_ = 0;
    rxTotal_ = 0;
}

void PAGES_Core::AirtimeTracker::add_(Bucket* buckets, uint32_t nowMs, uint32_t airMs) {
    uint32_t minute = nowMs / 60000UL;
    Bucket& b = buckets[minute % 60];
    if (!b.used || b.minute != minute) { b.minute = minute; b.ms = 0; b.used = true; }
    b.ms += airMs;
}

uint32_t PAGES_Core::AirtimeTracker::sum_(const Bucket* buckets, uint32_t nowMs) const {
    uint32_t minute = nowMs / 60000UL;
    uint32_t total = 0;
    for (int i = 0; i < 60; i++) {
        if (buckets[i].used && (uint32_t)(minute - buckets[i].minute) < 60U) total += buckets[i].ms;
    }
    return total;
}

void PAGES_Core::AirtimeTracker::addTx(uint32_t nowMs, uint32_t airMs) { add_(txBucket_, nowMs, airMs); txTotal_ += airMs; }
void PAGES_Core::AirtimeTracker::addRx(uint32_t nowMs, uint32_t airMs) { add_(rxBucket_, nowMs, airMs); rxTotal_ += airMs; }

float PAGES_Core::AirtimeTracker::txDutyPercent(uint32_t nowMs) const {
    return 100.0f * txLastHourMs(nowMs) / 3600000.0f;
}

void PAGES_Core::sMeterText(int rssiDbm, char* out, size_t size) {
    if (rssiDbm >= -93) {
        int over = rssiDbm + 93;
        if (over == 0) snprintf(out, size, "S9");
        else snprintf(out, size, "S9+%d", over);
        return;
    }
    int s = 9 + (int)floor((rssiDbm + 93) / 6.0);       // -99 -> S8, -141 -> S1
    if (s < 0) s = 0;
    snprintf(out, size, "S%d", s);
}

// ---------- Pronostico y alarmas ----------
float PAGES_Core::cloudBaseM(float tempC, float dewPointC) {
    float spread = tempC - dewPointC;
    return spread < 0.0f ? 0.0f : spread * 125.0f;
}

const char* PAGES_Core::convectionClass(float tempC, float dewPointC) {
    float spread = tempC - dewPointC;
    if (spread <= 3.0f) return "Saturated";
    if (spread < 10.0f) return "Cumulus";
    if (spread < 12.0f) return "Moderate";
    return "Dry, stable";
}

float PAGES_Core::frostMinC(float temp18C, float dew18C, float groundK) {
    return temp18C - 0.55f * (temp18C - dew18C) - groundK;
}

void PAGES_Core::forecastText(float p, bool hasTrend, Trend trend, float tempC, float hum, float dew, char* out, size_t size, float delta) {
    const char* text = "No data";
    bool haveHum = hum >= 0.0f;
    float spread = haveHum ? tempC - dew : 99.0f;
    bool fog = haveHum && hum >= 95.0f && spread <= 1.5f && trend != TREND_FALLING_FAST;
    bool humid = haveHum && hum >= 85.0f;
    bool dry = haveHum && hum < 65.0f;
    bool matrix = false;                                        // matriz de decision (presion, tendencia 3 h, T-Td)
    if (hasTrend && haveHum && delta == delta) {
        if (delta < -1.5f && p < 1010.0f && spread <= 3.0f)       { text = "Severe storm";    matrix = true; }
        else if (delta < -1.5f && p > 1020.0f && spread > 8.0f)   { text = "Sudden change";   matrix = true; }
        else if (fabsf(delta) <= 0.5f && p > 1016.0f && spread > 10.0f) { text = "Fair, anticyc."; matrix = true; }
        else if (delta > 1.5f && p < 1008.0f && spread <= 4.0f)   { text = "Passing showers"; matrix = true; }
    }
    if (matrix) {
        // ya decidido por la matriz
    } else if (!hasTrend || trend == TREND_UNKNOWN) {
        if (fog) text = "Fog/dew likely";
        else if (p >= 1020.0f) text = "Fair (trend wait)";
        else if (p < 1005.0f) text = "Unsettled (wait)";
        else text = "Trend wait";
    } else {
        switch (trend) {
            case TREND_FALLING_FAST: text = p < 1010.0f ? "Storm/gale risk" : "Rain/wind soon"; break;
            case TREND_FALLING:      text = (p < 1010.0f || humid) ? "Rain likely" : "Clouding over"; break;
            case TREND_FALLING_SLOW: text = humid ? "Showers possible" : "Turning cloudy"; break;
            case TREND_STEADY:
                if (fog) text = "Fog/dew likely";
                else if (p >= 1020.0f && !humid) text = "Fair, stable";
                else if (p < 1005.0f) text = "Unsettled";
                else if (humid && spread < 3.0f) text = "Overcast, damp";
                else text = dry ? "Fair" : "Little change";
                break;
            case TREND_RISING_SLOW:  text = fog ? "Fog/dew likely" : "Improving"; break;
            case TREND_RISING:       text = p < 1010.0f ? "Clearing" : "Fair, improving"; break;
            case TREND_RISING_FAST:  text = "Clearing, windy"; break;
            default: break;
        }
    }
    snprintf(out, size, "%s", text);
}

static bool alarmWord(const char* text, const char* word) {
    size_t n = strlen(word);
    for (const char* p = text; *p; p++) {
        if (p != text && isalnum((unsigned char)p[-1])) continue;
        size_t i = 0;
        while (i < n && p[i] && toupper((unsigned char)p[i]) == word[i]) i++;
        if (i == n && !isalnum((unsigned char)p[n])) return true;
    }
    return false;
}

PAGES_Core::AlarmKind PAGES_Core::alarmFromText(const char* text) {
    if (!text || !text[0]) return ALARM_NONE;
    if (alarmWord(text, "MOB") || alarmWord(text, "MANOVERBOARD") || alarmWord(text, "MAN OVERBOARD")) return ALARM_MOB;
    if (alarmWord(text, "SOS") || alarmWord(text, "EMERGENCY") || alarmWord(text, "MAYDAY")) return ALARM_EMERGENCY;
    return ALARM_NONE;
}

const char* PAGES_Core::alarmTitle(AlarmKind kind) {
    if (kind == ALARM_AIS_CPA) return "COLLISION RISK";
    if (kind == ALARM_AIS_SOS) return "AIS DISTRESS";
    return kind == ALARM_MOB ? "MAN OVERBOARD" : "EMERGENCY";
}

bool PAGES_Core::SosGesture::onPress(uint32_t pressMs, uint32_t gapMs) {
    if (gapMs > GAP_MAX_MS) clear();
    char symbol;
    if (pressMs < DOT_MAX_MS) symbol = '.';
    else if (pressMs <= DASH_MAX_MS) symbol = '-';
    else { clear(); return false; }
    if (count_ >= 10) {                                     // ventana deslizante de 10 simbolos
        for (int i = 1; i < count_; i++) seq_[i - 1] = seq_[i];
        count_--;
    }
    seq_[count_++] = symbol;
    seq_[count_] = '\0';
    bool five = count_ >= 5 && strcmp(seq_ + count_ - 5, ".....") == 0;
    bool sos = count_ >= 9 && strcmp(seq_ + count_ - 9, "...---...") == 0;
    if (five || sos) { clear(); return true; }
    return false;
}