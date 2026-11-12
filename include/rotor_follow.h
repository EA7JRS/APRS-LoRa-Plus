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

#pragma once
// Objetivos del rotor que salen de lo que el tracker recibe (C++ puro, probado en PC: tools/rotor_follow/test_rotor_follow.cpp):
//   - Baliza CW: indicativo (tras "DE") y locator Maidenhead del texto decodificado ("VVV DE ED7YAA IM76HR").
//   - Ultima estacion APRS oida (LoRa o FSK): filtro de indicativos de la WebUI.
// El acimut y la elevacion se calculan despues desde la posicion del GPS del tracker (SAT_Utils::observer).
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

namespace RotorFollow {

    // Fuentes del rotor (Config.rotor.source). El 3 es el programa externo (GPredict / rotctld), que nunca se guarda.
    // 6 y 7: el sol y la luna (ruido solar, rebote lunar), con las formulas del plotter (lib/aprs_formulas)
    enum Source : int { SRC_SAT = 0, SRC_APRS_TARGET = 1, SRC_FIXED = 2, SRC_EXTERNAL = 3, SRC_LAST_HEARD = 4, SRC_CW_BEACON = 5,
                        SRC_SUN = 6, SRC_MOON = 7 };

    inline bool validSource(int s) { return s == SRC_SAT || s == SRC_APRS_TARGET || s == SRC_FIXED || s == SRC_LAST_HEARD || s == SRC_CW_BEACON ||
                                            s == SRC_SUN || s == SRC_MOON; }

    // Siguiente fuente del menu (la de satelite solo en el modo SAT)
    inline int nextSource(int s, bool satMode) {
        static const int order[] = { SRC_SAT, SRC_APRS_TARGET, SRC_FIXED, SRC_LAST_HEARD, SRC_CW_BEACON, SRC_SUN, SRC_MOON };
        const int N = sizeof(order) / sizeof(order[0]);
        int i = 0;
        for (int k = 0; k < N; k++) if (order[k] == s) i = k;
        for (int k = 1; k <= N; k++) {
            const int n = order[(i + k) % N];
            if (n == SRC_SAT && !satMode) continue;
            return n;
        }
        return SRC_APRS_TARGET;
    }

    inline const char* sourceName(int s) {
        switch (s) {
            case SRC_SAT:         return "satellite";
            case SRC_APRS_TARGET: return "APRS target";
            case SRC_FIXED:       return "fixed target";
            case SRC_EXTERNAL:    return "GPredict";
            case SRC_LAST_HEARD:  return "last heard";
            case SRC_CW_BEACON:   return "CW beacon";
            case SRC_SUN:         return "sun";
            case SRC_MOON:        return "moon";
        }
        return "?";
    }

    // Estacion en movimiento: posicion estimada ahora a partir de su ultima posicion, su velocidad (km/h) y su rumbo (grados),
    // dtS segundos despues (como mucho maxS; por debajo de minSpeedKmh se considera parada). false = sin estimacion (queda la ultima)
    inline bool predict(double lat, double lon, double speedKmh, double courseDeg, double dtS, double maxS, double minSpeedKmh,
                        double& pLat, double& pLon) {
        pLat = lat; pLon = lon;
        if (!(speedKmh >= minSpeedKmh) || speedKmh > 1500.0 || !(dtS > 0) || courseDeg < 0 || courseDeg > 360) return false;
        if (dtS > maxS) dtS = maxS;
        const double R = 6371.0, D2R = 3.14159265358979323846 / 180.0;
        const double d = speedKmh * dtS / 3600.0 / R;                    // angulo recorrido sobre la esfera
        const double brg = courseDeg * D2R, la1 = lat * D2R, lo1 = lon * D2R;
        const double la2 = asin(sin(la1) * cos(d) + cos(la1) * sin(d) * cos(brg));
        double lo2 = lo1 + atan2(sin(brg) * sin(d) * cos(la1), cos(d) - sin(la1) * sin(la2));
        pLat = la2 / D2R;
        pLon = fmod(lo2 / D2R + 540.0, 360.0) - 180.0;
        return true;
    }

    // Locator Maidenhead de 4, 6 u 8 caracteres -> centro del cuadro. false si no es un locator
    inline bool locatorToLatLon(const char* loc, double& lat, double& lon) {
        const size_t n = strlen(loc);
        if (n != 4 && n != 6 && n != 8) return false;
        const char a = (char)toupper((unsigned char)loc[0]), b = (char)toupper((unsigned char)loc[1]);
        if (a < 'A' || a > 'R' || b < 'A' || b > 'R') return false;
        if (!isdigit((unsigned char)loc[2]) || !isdigit((unsigned char)loc[3])) return false;
        lon = (a - 'A') * 20.0 - 180.0 + (loc[2] - '0') * 2.0;
        lat = (b - 'A') * 10.0 - 90.0 + (loc[3] - '0') * 1.0;
        double w = 2.0, h = 1.0;                                    // tamaño del cuadro actual
        if (n >= 6) {
            const char c = (char)toupper((unsigned char)loc[4]), d = (char)toupper((unsigned char)loc[5]);
            if (c < 'A' || c > 'X' || d < 'A' || d > 'X') return false;
            w /= 24.0; h /= 24.0;
            lon += (c - 'A') * w;
            lat += (d - 'A') * h;
        }
        if (n == 8) {
            if (!isdigit((unsigned char)loc[6]) || !isdigit((unsigned char)loc[7])) return false;
            w /= 10.0; h /= 10.0;
            lon += (loc[6] - '0') * w;
            lat += (loc[7] - '0') * h;
        }
        lon += w / 2.0;
        lat += h / 2.0;
        return true;
    }

    // Indicativo de radioaficionado plausible: 3-10 caracteres, letras / cifras / '/', alguna cifra y alguna letra, y no es un locator
    inline bool looksLikeCall(const char* t) {
        const size_t n = strlen(t);
        if (n < 3 || n > 10) return false;
        bool digit = false, alpha = false;
        for (size_t i = 0; i < n; i++) {
            const char c = t[i];
            if (isdigit((unsigned char)c)) digit = true;
            else if (isalpha((unsigned char)c)) alpha = true;
            else if (c != '/') return false;
        }
        double la, lo;
        return digit && alpha && !locatorToLatLon(t, la, lo);
    }

    // Busca en el texto CW decodificado la ultima pareja indicativo + locator. El indicativo es la palabra tras "DE" mas cercana
    // antes del locator (o, si no hay "DE", la palabra con pinta de indicativo mas cercana). true si hay locator.
    inline bool parseCwBeacon(const char* text, char* call, size_t callSize, char* loc, size_t locSize) {
        static const int MAXW = 48;
        char words[MAXW][12];
        int nw = 0;
        const char* p = text;
        while (*p && nw < MAXW) {
            while (*p == ' ' || *p == '\n' || *p == '\r') p++;
            if (!*p) break;
            size_t k = 0;
            bool tooLong = false;
            while (*p && *p != ' ' && *p != '\n' && *p != '\r') {
                if (k < sizeof(words[0]) - 1) words[nw][k++] = (char)toupper((unsigned char)*p);
                else tooLong = true;
                p++;
            }
            words[nw][k] = '\0';
            if (!tooLong) nw++;
        }
        for (int i = nw - 1; i >= 0; i--) {
            double la, lo;
            if (!locatorToLatLon(words[i], la, lo)) continue;
            int cIdx = -1;
            for (int j = i - 1; j >= 1 && j >= i - 6; j--) if (strcmp(words[j - 1], "DE") == 0 && looksLikeCall(words[j])) { cIdx = j; break; }
            if (cIdx < 0) for (int j = i - 1; j >= 0 && j >= i - 4; j--) if (looksLikeCall(words[j])) { cIdx = j; break; }
            if (cIdx < 0 && i + 1 < nw && looksLikeCall(words[i + 1])) cIdx = i + 1;     // "IM76HR ED7YAA"
            if (call && callSize) {
                if (cIdx >= 0) { strncpy(call, words[cIdx], callSize - 1); call[callSize - 1] = '\0'; }
                else call[0] = '\0';
            }
            if (loc && locSize) { strncpy(loc, words[i], locSize - 1); loc[locSize - 1] = '\0'; }
            return true;
        }
        return false;
    }

    // Filtro de indicativos para "ultima estacion oida": lista separada por comas o espacios. "EA7JRS" = cualquier SSID,
    // "EA7JRS-9" = solo ese, "EA7*" = prefijo. Lista vacia = todas.
    inline bool callMatches(const char* call, const char* filter) {
        if (!filter || !*filter) return true;
        char base[12];
        size_t b = 0;
        while (call[b] && call[b] != '-' && b < sizeof(base) - 1) { base[b] = (char)toupper((unsigned char)call[b]); b++; }
        base[b] = '\0';
        const char* p = filter;
        while (*p) {
            while (*p == ',' || *p == ' ' || *p == ';') p++;
            if (!*p) break;
            char tok[16];
            size_t k = 0;
            while (*p && *p != ',' && *p != ' ' && *p != ';') { if (k < sizeof(tok) - 1) tok[k++] = (char)toupper((unsigned char)*p); p++; }
            tok[k] = '\0';
            if (k == 0) continue;
            if (tok[k - 1] == '*') {
                tok[k - 1] = '\0';
                bool ok = true;
                for (size_t i = 0; tok[i]; i++) if (toupper((unsigned char)call[i]) != tok[i]) { ok = false; break; }
                if (ok) return true;
            } else if (strchr(tok, '-')) {
                bool ok = true;
                size_t i = 0;
                for (; tok[i]; i++) if (toupper((unsigned char)call[i]) != tok[i]) { ok = false; break; }
                if (ok && call[i] == '\0') return true;
            } else if (strcmp(base, tok) == 0) {
                return true;
            }
        }
        return false;
    }
}
