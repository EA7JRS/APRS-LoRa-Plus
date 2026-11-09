/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * M17 por audio (LilyGO T-TWR). Ver include/m17_modem.h
 */

// Solo en la T-TWR (y en las pruebas del PC): asi el buscador de librerias de PlatformIO no compila libm17 en las demas placas
#if !defined(ARDUINO) || defined(HAS_SA868)

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "m17_modem.h"
#include "m17.h"

namespace M17M {

    // ---- Emision ----------------------------------------------------------------------------------------------------

    size_t packetSymbols(const char* src, const char* dst, uint8_t can, const uint8_t* data, size_t len, float* syms, size_t maxSyms) {
        if (!src || !dst || !data || len == 0 || len > MAX_PACKET || !syms) return 0;
        // trama completa: preambulo (192) + LSF (8 + 184) + n x (8 + 184) + EOT (192)
        const size_t frames = (len + 2 + 24) / 25;
        if (maxSyms < 192 * (frames + 3)) return 0;
        lsf_t lsf;
        if (set_LSF(&lsf, src, dst, (uint16_t)(M17_TYPE_PACKET | M17_TYPE_CAN(can)), nullptr)) return 0;

        uint8_t full[MAX_PACKET + 2];
        memcpy(full, data, len);
        const uint16_t crc = CRC_M17(full, (uint16_t)len);
        full[len] = (uint8_t)(crc >> 8);
        full[len + 1] = (uint8_t)(crc & 0xFF);
        size_t left = len + 2;

        uint32_t cnt = 0;
        uint8_t enc[SYM_PER_PLD * 2], rf[SYM_PER_PLD * 2];
        gen_preamble(syms, &cnt, PREAM_LSF);
        gen_syncword(syms, &cnt, SYNC_LSF);
        conv_encode_LSF(enc, &lsf);
        reorder_bits(rf, enc);
        randomize_bits(rf);
        gen_data(syms, &cnt, rf);
        for (uint8_t fn = 0; left > 0; fn++) {
            uint8_t chunk[26] = {0};
            gen_syncword(syms, &cnt, SYNC_PKT);
            if (left > 25) {                                // trama llena (como m17-packet-encode: con 25 justos queda para la ultima)
                memcpy(chunk, full + (size_t)fn * 25, 25);
                chunk[25] = (uint8_t)(fn << 2);
                left -= 25;
            } else {
                memcpy(chunk, full + (size_t)fn * 25, left);
                chunk[25] = (uint8_t)((1 << 7) | (left << 2));    // ultima: bit de fin y numero de bytes de esta trama
                left = 0;
            }
            conv_encode_packet_frame(enc, chunk);
            reorder_bits(rf, enc);
            randomize_bits(rf);
            gen_data(syms, &cnt, rf);
        }
        gen_eot(syms, &cnt);
        return cnt;
    }

    void BasebandTx::start(const float* s, size_t count, float amplitude) {
        syms = s; n = count; pos = 0;
        total = (count + 8) * SPS;                          // y la cola del filtro
        amp = amplitude < 0 ? 0 : (amplitude > 1 ? 1 : amplitude);
    }

    size_t BasebandTx::fill(int16_t* out, size_t maxSamples) {
        // salida(p) = suma de simbolo(k) * rrc(p - k * SPS); los coeficientes de libm17 tienen ganancia sqrt(SPS)
        static const float gain = sqrtf((float)SPS);
        size_t w = 0;
        while (w < maxSamples && pos < total) {
            float acc = 0;
            const long p = (long)pos;
            const long kHi = p / SPS, kLo = (p - 80 + SPS - 1) / SPS;
            for (long k = (kLo < 0 ? 0 : kLo); k <= kHi; k++) {
                if ((size_t)k >= n) break;
                const long t = p - k * SPS;
                if (t < 0 || t > 80) continue;
                acc += syms[k] * rrc_taps_10[t];
            }
            float v = acc * gain / 3.0f * amp;              // simbolo +-3 -> +-amp
            if (v > 1) v = 1;
            if (v < -1) v = -1;
            out[w++] = (int16_t)lrintf(v * 32767.0f);
            pos++;
        }
        return w;
    }

    // ---- Recepcion --------------------------------------------------------------------------------------------------

    bool Rx::begin() {
        uint32_t c = 0;
        gen_syncword(sync[0], &c, SYNC_LSF); c = 0;
        gen_syncword(sync[1], &c, SYNC_PKT); c = 0;
        gen_syncword(sync[2], &c, SYNC_STR); c = 0;
        gen_syncword(sync[3], &c, EOT_MRKR);
        reset();
        return true;
    }

    void Rx::reset() {
        memset(xin, 0, sizeof(xin)); xpos = 0;
        memset(y, 0, sizeof(y)); ypos = 0;
        dc = 0; rmsAvg = 0;
        candType = -1; candWait = 0; plType = -1; plCount = 0; busy = 0; sinceFrame = -1; lockDist = 1e9f;
        lastFn = -1; haveLsf = false; lichMask = 0;
        memset(&cur, 0, sizeof(cur));
    }

    float Rx::tail(int k) const {
        int i = ypos - 1 - k;
        while (i < 0) i += HIST;
        return y[i];
    }

    bool Rx::fillLsf(const void* raw, bool stream, bool fromLich) {
        const lsf_t* l = (const lsf_t*)raw;
        if (LSF_CRC(l) != (uint16_t)((l->crc[0] << 8) | l->crc[1])) { crcErrors++; return false; }
        Lsf& o = cur;
        memset(&o, 0, sizeof(o));
        decode_callsign_bytes(o.src, l->src);
        decode_callsign_bytes(o.dst, l->dst);
        o.type = (uint16_t)((l->type[0] << 8) | l->type[1]);
        memcpy(o.meta, l->meta, 14);
        o.stream = stream;
        o.fromLich = fromLich;
        // META con posicion GNSS: solo en corriente (en paquete el TYPE no define el contenido del META), sin cifrado,
        // subtipo 01 (bits 6..5 del TYPE)
        if (stream && ((o.type >> 3) & 3) == 0 && ((o.type >> 5) & 3) == 1) {
            uint8_t source, st, validity; float lat, lon, alt, speed, radius; uint16_t bearing;
            if (get_LSF_meta_position(&source, &st, &lat, &lon, &validity, &alt, &bearing, &speed, &radius, l) == 0 &&
                (validity & M17_GNSS_LAT_LON_VALID)) {
                o.hasPosition = true; o.lat = lat; o.lon = lon; o.stationType = st;
                o.altitude = (validity & M17_GNSS_ALT_VALID) ? alt : 0;
                if (validity & M17_GNSS_SPD_BEARING_VALID) { o.speed = speed; o.bearing = bearing; }
                o.gnssValid = validity;
            }
        }
        lsfs++;
        return true;
    }

    void Rx::frameDone() {
        if (plType == 0) {                                  // LSF
            lsf_t l;
            decode_LSF(&l, pld);
            if (fillLsf(&l, (((uint16_t)(l.type[0] << 8) | l.type[1]) & 1) != 0, false)) {
                haveLsf = !cur.stream;
                lastFn = -1;
                if (lsfCb) lsfCb(cur, cbCtx);
            }
        } else if (plType == 1) {                           // trama de paquete
            uint8_t data[26], last = 0, fn = 0;
            decode_pkt_frame(data, &last, &fn, pld);
            if (!last && fn <= 31 && (int)fn == lastFn + 1) {
                memcpy(pkt + (size_t)fn * 25, data, 25);
                lastFn = fn;
            } else if (last) {
                const int bytes = fn < 25 ? fn : 25;        // en la ultima, fn = bytes de esta trama
                memcpy(pkt + (size_t)(lastFn + 1) * 25, data, (size_t)bytes);
                const size_t total = (size_t)(lastFn + 1) * 25 + (size_t)bytes;
                if (total >= 3 && CRC_M17(pkt, (uint16_t)total) == 0) {
                    packets++;
                    if (!haveLsf) { memset(&cur, 0, sizeof(cur)); strcpy(cur.src, "?"); strcpy(cur.dst, "?"); }
                    if (pktCb) pktCb(cur, pkt, total - 2, cbCtx);
                } else crcErrors++;
                lastFn = -1;
                haveLsf = false;
            } else {
                lastFn = -1;                                // trama perdida: el paquete no se puede armar
            }
        } else if (plType == 2) {                           // corriente: el LICH trae el LSF en 6 trozos de 5 bytes
            uint8_t data[16], lich[6]; uint16_t fn = 0; uint8_t lc = 0;
            const uint32_t e = decode_str_frame(data, lich, &fn, &lc, pld);
            (void)e;
            if (lc < 6) {
                memcpy(lichBuf + lc * 5, lich, 5);
                lichMask |= (uint8_t)(1 << lc);
                if (lichMask == 0x3F) {
                    lichMask = 0;
                    if (fillLsf(lichBuf, true, true) && lsfCb) lsfCb(cur, cbCtx);
                }
            }
            if (fn & 0x8000) lichMask = 0;                  // ultima trama de la emision
        }
        plType = -1;
        sinceFrame = 0;                                     // la siguiente sincronia, 8 simbolos despues
        lockDist = 1e9f;
    }

    void Rx::feed(float x) {
        // continua fuera, RRC adaptado y control de nivel (por la palabra de sincronia)
        dc += (x - dc) * 0.0002f;
        x -= dc;
        xpos = (xpos == 0) ? TAPS - 1 : xpos - 1;
        xin[xpos] = x; xin[xpos + TAPS] = x;
        float f = 0;
        for (int i = 0; i < TAPS; i++) f += rrc_taps_10[i] * xin[xpos + i];
        y[ypos] = f;
        ypos = (ypos + 1) % HIST;
        if (busy > 0) busy--;

        if (plType >= 0) {                                  // recogiendo la carga util: un simbolo cada SPS muestras
            if (++plPhase == SPS) {
                plPhase = 0;
                pld[plCount++] = f * plGain;
                if (plCount == 184) frameDone();
            }
            return;
        }

        // palabra de sincronia: los 8 ultimos simbolos (cada SPS muestras) frente a las 4 conocidas, con el nivel normalizado
        float s[8], mag = 0;
        for (int k = 0; k < 8; k++) { s[k] = tail((7 - k) * SPS); mag += fabsf(s[k]); }
        if (mag < 1e-6f) return;
        float best = 1e9f, bestGain = 1; int bestType = -1;
        for (int t = 0; t < 4; t++) {
            float ref = 0;
            for (int k = 0; k < 8; k++) ref += fabsf(sync[t][k]);
            const float g = ref / mag;
            float d = 0;
            for (int k = 0; k < 8; k++) { const float e = s[k] * g - sync[t][k]; d += e * e; }
            if (d < best) { best = d; bestType = t; bestGain = g; }
        }
        // Enganchado (acaba de terminar una trama): la siguiente palabra de sincronia llega 8 simbolos despues; se busca el minimo
        // en +-3 muestras con un umbral amplio. Libre: umbral estricto y minimo en media duracion de simbolo (distancias al cuadrado)
        const float THRESH_FREE = 3.0f, THRESH_LOCK = 8.0f;
        if (sinceFrame >= 0) {
            sinceFrame++;
            if (sinceFrame < 8 * SPS - 3) return;
            if (sinceFrame <= 8 * SPS + 3 && best < lockDist) { lockDist = best; lockType = bestType; lockGain = bestGain; lockAt = sinceFrame; }
            if (sinceFrame < 8 * SPS + 3) return;
            const bool ok = lockDist < THRESH_LOCK;
            const int lag = sinceFrame - lockAt;
            const int t = lockType; const float g = lockGain, d = lockDist;
            sinceFrame = -1; lockDist = 1e9f;
            if (ok) startPayload(t, g, d, lag);
            return;
        }
        if (candType < 0) {
            if (best < THRESH_FREE) { candType = bestType; candDist = best; candGain = bestGain; candWait = SPS / 2; }
            return;
        }
        if (best < candDist) { candType = bestType; candDist = best; candGain = bestGain; candWait = SPS / 2; return; }
        if (--candWait > 0) return;
        const int t = candType;
        candType = -1;
        startPayload(t, candGain, candDist, SPS / 2);       // el minimo fue hace SPS/2 muestras
    }

    // lag = muestras pasadas desde el ultimo simbolo de la palabra de sincronia: el primer simbolo de la carga va SPS despues
    void Rx::startPayload(int t, float gain, float dist, int lag) {
        busy = 2 * 192 * SPS;
        (void)dist;
        if (t == 3) { lastFn = -1; haveLsf = false; lichMask = 0; return; }     // EOT: fin de la emision
        plType = t; plCount = 0; plGain = gain;
        plPhase = lag;
    }

    // ---- APRS sobre M17 ---------------------------------------------------------------------------------------------

    size_t tnc2ToPacket(const char* tnc2, uint8_t* out, size_t outMax) {
        if (!tnc2 || !out) return 0;
        const size_t n = strlen(tnc2);
        if (n == 0 || n + 1 > outMax || n + 1 > MAX_PACKET) return 0;
        out[0] = PROTO_APRS;
        memcpy(out + 1, tnc2, n);
        return n + 1;
    }

    size_t smsPacket(const char* text, uint8_t* out, size_t outMax) {
        if (!text || !out) return 0;
        const size_t n = strlen(text);
        if (n == 0 || n + 2 > outMax || n + 2 > MAX_PACKET) return 0;
        out[0] = PROTO_SMS;
        memcpy(out + 1, text, n);
        out[n + 1] = 0;
        return n + 2;
    }

    bool aprsCall(const char* m17, char* out, size_t outMax) {
        if (!m17 || !out || outMax < 2 || m17[0] == '@' || m17[0] == '#' || m17[0] == 0) return false;
        size_t k = 0;
        for (const char* p = m17; *p && k + 1 < outMax && k < 9; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            if (c == '/' || c == '.') c = '-';
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
            out[k++] = c;
        }
        while (k > 0 && out[k - 1] == '-') k--;
        out[k] = 0;
        return k >= 3;
    }

    static size_t put(char*, size_t outMax, int n) { return (n > 0 && (size_t)n < outMax) ? (size_t)n : 0; }

    size_t packetToTnc2(const Lsf& lsf, const uint8_t* data, size_t len, char* out, size_t outMax) {
        if (!data || len < 2 || !out || outMax < 16) return 0;
        char src[12];
        if (!aprsCall(lsf.src, src, sizeof(src))) return 0;
        // texto: hasta el primer 0 (el SMS lo lleva al final), sin caracteres de control
        char text[MAX_PACKET + 1]; size_t t = 0;
        for (size_t i = 1; i < len && data[i] != 0 && t < MAX_PACKET; i++) {
            const char c = (char)data[i];
            if (c == '\r' || c == '\n') break;
            text[t++] = ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) ? ' ' : c;
        }
        text[t] = 0;
        if (t == 0) return 0;
        if (data[0] == PROTO_APRS) {
            const char* gt = strchr(text, '>'); const char* colon = strchr(text, ':');
            if (gt && colon && gt < colon && gt > text) return put(out, outMax, snprintf(out, outMax, "%s", text));
            return put(out, outMax, snprintf(out, outMax, "%s>%s:%s", src, TOCALL, text));
        }
        if (data[0] == PROTO_SMS) {
            char dst[12];
            if (!aprsCall(lsf.dst, dst, sizeof(dst))) snprintf(dst, sizeof(dst), "ALL");     // @ALL y difusiones: a todos
            if (t > 67) text[t = 67] = 0;
            for (size_t i = 0; i < t; i++) if (text[i] == '{' || text[i] == '|' || text[i] == '~') text[i] = '(';   // reservados en APRS
            return put(out, outMax, snprintf(out, outMax, "%s>%s,NOGATE::%-9s:%s", src, TOCALL, dst, text));
        }
        return 0;
    }

    size_t lsfToTnc2(const Lsf& lsf, char* out, size_t outMax) {
        if (!out || outMax < 40) return 0;
        char src[12], dst[12];
        if (!aprsCall(lsf.src, src, sizeof(src))) return 0;
        if (!aprsCall(lsf.dst, dst, sizeof(dst))) snprintf(dst, sizeof(dst), "%s", lsf.dst[0] ? lsf.dst : "?");
        const char* kind = ((lsf.type >> 1) & 3) == 2 ? "voice" : (((lsf.type >> 1) & 3) == 1 ? "data" : "voice+data");
        if (!lsf.hasPosition || lsf.lat < -90 || lsf.lat > 90 || lsf.lon < -180 || lsf.lon > 180)
            return put(out, outMax, snprintf(out, outMax, "%s>%s,NOGATE:>M17 %s to %s", src, TOCALL, kind, dst));
        const float alat = fabsf(lsf.lat), alon = fabsf(lsf.lon);
        int latD = (int)alat, lonD = (int)alon;
        float latM = (alat - latD) * 60.0f, lonM = (alon - lonD) * 60.0f;
        if (latM >= 59.995f) { latM = 0; latD++; }
        if (lonM >= 59.995f) { lonM = 0; lonD++; }
        // simbolo: casa (fija), coche (movil), persona (portatil)
        const char sym = lsf.stationType == 0 ? '-' : (lsf.stationType == 1 ? '>' : '[');
        char ext[24] = "";
        if (lsf.gnssValid & M17_GNSS_SPD_BEARING_VALID) {
            const int cse = lsf.bearing % 360 == 0 ? 360 : lsf.bearing % 360;
            int spd = (int)lroundf(lsf.speed / 1.852f);
            if (spd < 0) spd = 0;
            if (spd > 999) spd = 999;
            snprintf(ext, sizeof(ext), "%03d/%03d", cse, spd);
        }
        char alt[16] = "";
        if (lsf.gnssValid & M17_GNSS_ALT_VALID) {
            long ft = lroundf(lsf.altitude * 3.28084f);
            if (ft < -99999) ft = -99999;
            if (ft > 999999) ft = 999999;
            snprintf(alt, sizeof(alt), "/A=%06ld", ft);
        }
        return put(out, outMax, snprintf(out, outMax, "%s>%s,NOGATE:!%02d%05.2f%c/%03d%05.2f%c%c%s%s M17 %s to %s", src, TOCALL,
                                          latD, latM, lsf.lat < 0 ? 'S' : 'N', lonD, lonM, lsf.lon < 0 ? 'W' : 'E', sym, ext, alt, kind, dst));
    }
}

#endif
