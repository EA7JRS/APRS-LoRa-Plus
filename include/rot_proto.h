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
// Protocolo de rotor compatible con rotctld de Hamlib ("NET rotctl", modelo 2), sin hardware: se prueba en PC.
// El tracker hace de servidor para GPredict (u otro programa) y reenvia el acimut / elevacion a su rotor (con los desfases,
// el montaje movil y la compensacion de inclinacion). El firmware implementa RotProto::Rot; RotProto::Session analiza las lineas.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>

namespace RotProto {

    enum { E_OK = 0, E_INVAL = 1, E_NIMPL = 4, E_REJECTED = 9 };

    class Rot {
    public:
        virtual ~Rot() {}
        virtual bool        setPos(float azDeg, float elDeg) = 0;   // false: rechazada
        virtual bool        getPos(float& azDeg, float& elDeg) = 0; // posicion real si se conoce; si no, la ultima pedida
        virtual bool        park() = 0;
        virtual const char* info() = 0;
    };

    class Session {
    public:
        // Procesa bytes del cliente y añade las respuestas a out. Devuelve true si hay que cerrar la conexion (comando "q")
        bool feed(const char* data, size_t n, Rot& rot, std::string& out) {
            bool close = false;
            for (size_t i = 0; i < n; i++) {
                const char c = data[i];
                if (c == '\n') {
                    if (handle(line_, rot, out)) close = true;
                    line_.clear();
                } else if (c != '\r') {
                    if (line_.size() < 120) line_ += c;
                    else line_.clear();
                }
            }
            return close;
        }
        void reset() { line_.clear(); }

    private:
        std::string line_;

        static void rprt(std::string& out, int code) {
            char b[24];
            snprintf(b, sizeof(b), "RPRT %d\n", code == 0 ? 0 : -code);
            out += b;
        }

        static size_t split(const std::string& s, std::string* tok, size_t max) {
            size_t n = 0, i = 0;
            while (i < s.size() && n < max) {
                while (i < s.size() && s[i] == ' ') i++;
                if (i >= s.size()) break;
                size_t j = i;
                while (j < s.size() && s[j] != ' ') j++;
                tok[n++] = s.substr(i, j - i);
                i = j;
            }
            return n;
        }

        static bool number(const std::string& s, double& v) {
            if (s.empty()) return false;
            char* end = nullptr;
            v = strtod(s.c_str(), &end);
            return end && *end == 0 && isfinite(v);
        }

        bool handle(const std::string& raw, Rot& rot, std::string& out) {
            if (raw.empty()) return false;
            std::string t[4];
            std::string cmd;
            size_t k;
            if (raw[0] == '\\') {
                k = split(raw.substr(1), t, 4);
                if (k == 0) return false;
                cmd = t[0];
                for (size_t i = 1; i < k; i++) t[i - 1] = t[i];
                k--;
            } else {
                if (raw[0] == '+' || raw[0] == ';' || raw[0] == '|') { rprt(out, E_NIMPL); return false; }   // protocolo extendido: no soportado
                k = split(raw.substr(1), t, 4);
                switch (raw[0]) {
                    case 'P': cmd = "set_pos"; break;
                    case 'p': cmd = "get_pos"; break;
                    case 'S': cmd = "stop"; break;
                    case 'K': cmd = "park"; break;
                    case 'R': cmd = "reset"; break;
                    case '_': cmd = "get_info"; break;
                    case 'q': case 'Q': cmd = "quit"; break;
                    default: rprt(out, E_NIMPL); return false;
                }
            }
            char b[96];
            if (cmd == "set_pos") {
                double az, el;
                if (k < 2 || !number(t[0], az) || !number(t[1], el)) { rprt(out, E_INVAL); return false; }
                if (az < -360.0 || az > 720.0 || el < -90.0 || el > 180.0) { rprt(out, E_INVAL); return false; }
                az = fmod(az, 360.0); if (az < 0) az += 360.0;
                if (el > 90.0) el = 90.0;                         // por encima del cenit: el rotor de 180 grados daria la vuelta; se queda en el cenit
                rprt(out, rot.setPos((float)az, (float)el) ? E_OK : E_REJECTED);
                return false;
            }
            if (cmd == "get_pos") {
                float az = 0, el = 0;
                rot.getPos(az, el);
                snprintf(b, sizeof(b), "%.6f\n%.6f\n", (double)az, (double)el);
                out += b;
                return false;
            }
            if (cmd == "stop" || cmd == "reset") { rprt(out, E_OK); return false; }
            if (cmd == "park") { rprt(out, rot.park() ? E_OK : E_REJECTED); return false; }
            if (cmd == "get_info") { snprintf(b, sizeof(b), "%s\n", rot.info()); out += b; return false; }
            if (cmd == "dump_state") {                           // protocolo 1 de Hamlib 4.x (tests/rotctl_parse.c): version, modelo y "clave=valor" hasta "done" (netrotctl lo espera)
                out += "1\n2\nmin_az=0.000000\nmax_az=360.000000\nmin_el=0.000000\nmax_el=90.000000\nsouth_zero=0\nrot_type=AzEl\ndone\n";
                return false;
            }
            if (cmd == "quit") { rprt(out, E_OK); return true; }
            rprt(out, E_NIMPL);
            return false;
        }
    };
}
