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
// Protocolo de control CAT compatible con rigctld de Hamlib ("NET rigctl", modelo 2), sin hardware: se prueba en PC.
// El firmware implementa CatProto::Rig; CatProto::Session analiza las lineas del cliente y escribe las respuestas.
// Especificacion publica: docs/CAT_HAMLIB.md
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>

namespace CatProto {

    // Codigos de error de Hamlib (se devuelven como "RPRT -n")
    enum { E_OK = 0, E_INVAL = 1, E_NIMPL = 4, E_REJECTED = 9, E_NAVAIL = 11 };

    // Bits de Hamlib: modos (rig.h, RIG_MODE_*) y niveles (RIG_LEVEL_*)
    const uint64_t MODE_PKTFM   = 1ULL << 12;     // "PKTFM":  APRS FSK 9600 (G3RUH), el paquete por FM de siempre
    const uint64_t MODE_PKTFMN  = 1ULL << 34;     // "PKTFMN": APRS LoRa. Hamlib no tiene un modo LoRa; se propone RIG_MODE_LORA (docs/CAT_HAMLIB.md)
    const uint64_t LEVEL_RFPOWER  = 1ULL << 12;
    const uint64_t LEVEL_STRENGTH = 1ULL << 30;
    const int      PTT_NONE = 0, PTT_RIG = 1;

    struct Range { uint32_t lo, hi; };

    // Lo que el firmware ofrece al protocolo
    class Rig {
    public:
        virtual ~Rig() {}
        virtual uint32_t    freqHz() = 0;
        virtual bool        setFreqHz(uint32_t hz) = 0;             // false: frecuencia no permitida
        virtual bool        isFsk() = 0;                            // perfil activo: FSK (true) o LoRa (false)
        virtual bool        setFsk(bool fsk) = 0;                   // false: no hay un perfil de ese tipo
        virtual int         passbandHz() = 0;                       // ancho de banda del perfil
        virtual int         rssiDbm() = 0;                          // RSSI del ultimo paquete
        virtual int         powerDbm() = 0;
        virtual bool        setPowerDbm(int dbm) = 0;               // devuelve false si no se puede
        virtual int         powerMinDbm() = 0;
        virtual int         powerMaxDbm() = 0;
        virtual bool        pttEnabled() = 0;                       // PTT = baliza (opcion de la WebUI, apagada por defecto)
        virtual bool        triggerBeacon() = 0;
        virtual size_t      ranges(Range* out, size_t max) = 0;     // frecuencias que acepta setFreqHz (para dump_state)
        virtual const char* info() = 0;
    };

    class Session {
    public:
        // Procesa bytes del cliente y añade las respuestas a out. Devuelve true si hay que cerrar la conexion (comando "q").
        bool feed(const char* data, size_t n, Rig& rig, std::string& out) {
            bool close = false;
            for (size_t i = 0; i < n; i++) {
                const char c = data[i];
                if (c == '\n') {
                    if (handle(line_, rig, out)) close = true;
                    line_.clear();
                } else if (c != '\r') {
                    if (line_.size() < 200) line_ += c;
                    else line_.clear();                      // linea absurda: se descarta
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

        static bool isVfoName(const std::string& t) {
            return t.compare(0, 3, "VFO") == 0 || t == "currVFO" || t == "Main" || t == "Sub" || t == "TX" || t == "RX" ||
                   t == "MEM" || t == "MainA" || t == "MainB" || t == "SubA" || t == "SubB";
        }

        // Separa la linea en palabras; si la primera palabra tras el comando es un VFO, se descarta (modo con VFO de rigctld)
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

        static const char* modeName(Rig& rig) { return rig.isFsk() ? "PKTFM" : "PKTFMN"; }

        static float powerFraction(Rig& rig) {
            const int lo = rig.powerMinDbm(), hi = rig.powerMaxDbm();
            if (hi <= lo) return 1.0f;
            float f = (float)(rig.powerDbm() - lo) / (float)(hi - lo);
            return f < 0 ? 0 : (f > 1 ? 1 : f);
        }

        static void dumpState(Rig& rig, std::string& out) {
            char b[256];
            const uint64_t modes = MODE_PKTFM | MODE_PKTFMN;
            Range r[16];
            const size_t nr = rig.ranges(r, 16);
            const int lo = rig.powerMinDbm(), hi = rig.powerMaxDbm();
            const int loMw = (int)lroundf(powf(10.0f, lo / 10.0f)), hiMw = (int)lroundf(powf(10.0f, hi / 10.0f));
            out += "1\n0\n0\n";                                                  // version del protocolo, modelo (lo asigna Hamlib), region ITU (en desuso)
            for (size_t i = 0; i < nr; i++) {                                    // intervalos de recepcion
                snprintf(b, sizeof(b), "%u.000000 %u.000000 0x%llx -1 -1 0x1 0x1\n", (unsigned)r[i].lo, (unsigned)r[i].hi, (unsigned long long)modes);
                out += b;
            }
            out += "0 0 0 0 0 0 0\n";
            for (size_t i = 0; i < nr; i++) {                                    // intervalos de emision
                snprintf(b, sizeof(b), "%u.000000 %u.000000 0x%llx %d %d 0x1 0x1\n", (unsigned)r[i].lo, (unsigned)r[i].hi, (unsigned long long)modes, loMw, hiMw);
                out += b;
            }
            out += "0 0 0 0 0 0 0\n";
            snprintf(b, sizeof(b), "0x%llx 1\n0 0\n", (unsigned long long)modes);               // pasos de sintonia
            out += b;
            snprintf(b, sizeof(b), "0x%llx %d\n0 0\n", (unsigned long long)modes, rig.passbandHz());   // filtros
            out += b;
            out += "0\n0\n0\n0\n\n\n";                                           // max_rit, max_xit, max_ifshift, announces, preamp, attenuator
            out += "0x0\n0x0\n";                                                  // has_get_func, has_set_func
            snprintf(b, sizeof(b), "0x%llx\n0x%llx\n0x0\n0x0\n", (unsigned long long)(LEVEL_RFPOWER | LEVEL_STRENGTH), (unsigned long long)LEVEL_RFPOWER);
            out += b;                                                             // has_get_level, has_set_level, has_get_parm, has_set_parm
            out += "vfo_ops=0x0\n";
            snprintf(b, sizeof(b), "ptt_type=0x%x\n", rig.pttEnabled() ? PTT_RIG : PTT_NONE);
            out += b;
            out += "targetable_vfo=0x0\nhas_set_vfo=0\nhas_get_vfo=0\nhas_set_freq=1\nhas_get_freq=1\nhas_set_conf=0\nhas_get_conf=0\n";
            out += "has_power2mW=0\nhas_mW2power=0\nhas_get_ant=0\nhas_set_ant=0\ntimeout=0\nrig_model=0\n";
            snprintf(b, sizeof(b), "level_gran=12=0,1,0.01;30=-60,60,1;\nparm_gran=\nrig_model=0\nhamlib_version=%s\ndone\n", rig.info());
            out += b;
        }

        // Devuelve true si hay que cerrar la conexion
        bool handle(const std::string& raw, Rig& rig, std::string& out) {
            if (raw.empty()) return false;
            std::string t[6];
            std::string cmd;
            std::string rest;
            size_t k;
            if (raw[0] == '\\') {                       // nombre largo: \get_freq
                k = split(raw.substr(1), t, 6);
                if (k == 0) return false;
                cmd = t[0];
                for (size_t i = 1; i < k; i++) t[i - 1] = t[i];
                k--;
            } else {                                    // letra corta: f, F 434775000
                if (raw[0] == '+' || raw[0] == ';' || raw[0] == '|') { rprt(out, E_NIMPL); return false; }     // protocolo extendido: no soportado
                k = split(raw.substr(1), t, 6);
                const char c = raw[0];
                switch (c) {
                    case 'f': cmd = "get_freq"; break;
                    case 'F': cmd = "set_freq"; break;
                    case 'm': cmd = "get_mode"; break;
                    case 'M': cmd = "set_mode"; break;
                    case 't': cmd = "get_ptt"; break;
                    case 'T': cmd = "set_ptt"; break;
                    case 'l': cmd = "get_level"; break;
                    case 'L': cmd = "set_level"; break;
                    case 'v': cmd = "get_vfo"; break;
                    case 'V': cmd = "set_vfo"; break;
                    case 's': cmd = "get_split_vfo"; break;
                    case '_': cmd = "get_info"; break;
                    case 'q': case 'Q': cmd = "quit"; break;
                    default: rprt(out, E_NIMPL); return false;
                }
            }
            // si hay VFO delante (modo con VFO), se salta
            if (k > 0 && isVfoName(t[0]) && cmd != "set_vfo" && cmd != "get_vfo_info") {
                for (size_t i = 1; i < k; i++) t[i - 1] = t[i];
                k--;
            }
            char b[96];
            if (cmd == "chk_vfo") { out += "0\n"; return false; }
            if (cmd == "dump_state") { dumpState(rig, out); return false; }
            if (cmd == "get_freq") { snprintf(b, sizeof(b), "%u\n", (unsigned)rig.freqHz()); out += b; return false; }
            if (cmd == "set_freq") {
                if (k < 1) { rprt(out, E_INVAL); return false; }
                const double f = atof(t[0].c_str());
                if (!(f > 0 && f < 4e9)) { rprt(out, E_INVAL); return false; }
                rprt(out, rig.setFreqHz((uint32_t)llround(f)) ? E_OK : E_REJECTED);
                return false;
            }
            if (cmd == "get_mode") { snprintf(b, sizeof(b), "%s\n%d\n", modeName(rig), rig.passbandHz()); out += b; return false; }
            if (cmd == "set_mode") {
                if (k < 1) { rprt(out, E_INVAL); return false; }
                if (t[0] == "PKTFM" || t[0] == "FM-D") rprt(out, rig.setFsk(true) ? E_OK : E_REJECTED);
                else if (t[0] == "PKTFMN") rprt(out, rig.setFsk(false) ? E_OK : E_REJECTED);
                else rprt(out, E_INVAL);
                return false;
            }
            if (cmd == "get_ptt") { out += "0\n"; return false; }
            if (cmd == "set_ptt") {
                if (k < 1) { rprt(out, E_INVAL); return false; }
                if (atoi(t[0].c_str()) == 0) { rprt(out, E_OK); return false; }
                if (!rig.pttEnabled()) { rprt(out, E_NAVAIL); return false; }
                rprt(out, rig.triggerBeacon() ? E_OK : E_REJECTED);
                return false;
            }
            if (cmd == "get_level") {
                if (k < 1) { rprt(out, E_INVAL); return false; }
                if (t[0] == "STRENGTH") { snprintf(b, sizeof(b), "%d\n", rig.rssiDbm() + 93); out += b; }       // dB sobre S9 (S9 = -93 dBm por encima de 30 MHz)
                else if (t[0] == "RFPOWER") { snprintf(b, sizeof(b), "%f\n", (double)powerFraction(rig)); out += b; }
                else rprt(out, E_NAVAIL);
                return false;
            }
            if (cmd == "set_level") {
                if (k < 2) { rprt(out, E_INVAL); return false; }
                if (t[0] != "RFPOWER") { rprt(out, E_NAVAIL); return false; }
                const double v = atof(t[1].c_str());
                const int lo = rig.powerMinDbm(), hi = rig.powerMaxDbm();
                const int dbm = lo + (int)lround((v < 0 ? 0 : (v > 1 ? 1 : v)) * (hi - lo));
                rprt(out, rig.setPowerDbm(dbm) ? E_OK : E_REJECTED);
                return false;
            }
            if (cmd == "get_vfo") { out += "VFOA\n"; return false; }
            if (cmd == "set_vfo") { rprt(out, E_OK); return false; }
            if (cmd == "get_split_vfo") { out += "0\nNone\n"; return false; }
            if (cmd == "get_split_freq" || cmd == "get_rit" || cmd == "get_xit") { out += "0\n"; return false; }
            if (cmd == "get_lock_mode") { out += "0\nRPRT 0\n"; return false; }       // Hamlib lo pregunta antes de cambiar el modo y espera estas dos lineas
            if (cmd == "set_lock_mode") { rprt(out, E_OK); return false; }
            if (cmd == "get_powerstat") { out += "1\n"; return false; }
            if (cmd == "get_info") { snprintf(b, sizeof(b), "%s\n", rig.info()); out += b; return false; }
            if (cmd == "get_vfo_info") {                 // frecuencia, modo, ancho, split y modo satelite
                snprintf(b, sizeof(b), "%u\n%s\n%d\n0\n0\n", (unsigned)rig.freqHz(), modeName(rig), rig.passbandHz());
                out += b;
                return false;
            }
            if (cmd == "quit") { rprt(out, E_OK); return true; }          // rigctld contesta antes de cerrar; el cliente espera esa linea
            rprt(out, E_NIMPL);
            return false;
        }
    };
}
