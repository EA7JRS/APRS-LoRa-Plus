/* Copyright (C) 2026 Manuel - EA7JRS
 * AX.25 UI frames + 9600 baud G3RUH bit stream. See ax25_g3ruh.h
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "ax25_g3ruh.h"

namespace AX25G3 {

    uint16_t crc16(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int b = 0; b < 8; b++) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
        }
        return (uint16_t)~crc;
    }

    // ---- TNC2 <-> AX.25 ----------------------------------------------------------------------------------------------

    static bool putAddress(uint8_t* out, const char* s, size_t n, bool last) {
        // s[0..n) = "CALL" or "CALL-SSID" optionally followed by '*' (digipeater "has been repeated")
        bool hbit = false;
        if (n > 0 && s[n - 1] == '*') { hbit = true; n--; }
        size_t dash = n;
        for (size_t i = 0; i < n; i++) if (s[i] == '-') { dash = i; break; }
        if (dash == 0 || dash > 6) return false;
        int ssid = 0;
        if (dash < n) {
            if (n - dash - 1 == 0 || n - dash - 1 > 2) return false;
            for (size_t i = dash + 1; i < n; i++) {
                if (!isdigit((unsigned char)s[i])) return false;
                ssid = ssid * 10 + (s[i] - '0');
            }
            if (ssid > 15) return false;
        }
        for (size_t i = 0; i < 6; i++) {
            char c = (i < dash) ? (char)toupper((unsigned char)s[i]) : ' ';
            out[i] = (uint8_t)(c << 1);
        }
        out[6] = (uint8_t)(0x60 | (ssid << 1) | (hbit ? 0x80 : 0) | (last ? 1 : 0));
        return true;
    }

    size_t tnc2ToFrame(const char* tnc2, uint8_t* out, size_t maxOut) {
        const char* colon = strchr(tnc2, ':');
        const char* gt    = strchr(tnc2, '>');
        if (!colon || !gt || gt > colon) return 0;
        const char* srcPtr = tnc2;
        size_t      srcLen = (size_t)(gt - tnc2);
        const char* p = gt + 1;
        int count = 0;                              // destination + digipeaters
        const char* items[9]; size_t ilen[9];
        while (p <= colon && count < 9) {
            const char* comma = p;
            while (comma < colon && *comma != ',') comma++;
            items[count] = p; ilen[count] = (size_t)(comma - p); count++;
            p = comma + 1;
            if (comma >= colon) break;
        }
        if (count < 1) return 0;
        size_t digis = (size_t)count - 1;
        size_t need  = 7 * (2 + digis) + 2 + strlen(colon + 1);
        if (need > maxOut) return 0;
        size_t pos = 0;
        if (!putAddress(out + pos, items[0], ilen[0], false)) return 0;             // destination
        pos += 7;
        if (!putAddress(out + pos, srcPtr, srcLen, digis == 0)) return 0;           // source
        pos += 7;
        for (size_t d = 0; d < digis; d++) {
            if (!putAddress(out + pos, items[d + 1], ilen[d + 1], d == digis - 1)) return 0;
            pos += 7;
        }
        out[pos++] = 0x03;                          // UI
        out[pos++] = 0xF0;                          // no layer 3
        size_t il = strlen(colon + 1);
        memcpy(out + pos, colon + 1, il);
        return pos + il;
    }

    static size_t getAddress(const uint8_t* a, char* out) {
        size_t n = 0;
        for (int i = 0; i < 6; i++) {
            char c = (char)(a[i] >> 1);
            if (c != ' ') out[n++] = c;
        }
        int ssid = (a[6] >> 1) & 0x0F;
        if (ssid) n += (size_t)sprintf(out + n, "-%d", ssid);
        out[n] = 0;
        return n;
    }

    size_t frameToTnc2(const uint8_t* f, size_t len, char* out, size_t maxOut) {
        if (len < 16) return 0;
        size_t addrEnd = 0;
        for (size_t i = 0; i + 6 < len && i < 70; i += 7) {
            if (f[i + 6] & 1) { addrEnd = i + 7; break; }
        }
        if (addrEnd < 14 || addrEnd + 2 > len) return 0;
        if (f[addrEnd] != 0x03 || f[addrEnd + 1] != 0xF0) return 0;
        size_t infoLen = len - addrEnd - 2;
        char tmp[16];
        size_t pos = 0;
        if (maxOut < 32 + infoLen + 9 * 11) return 0;
        pos += getAddress(f + 7, out + pos);           // source
        out[pos++] = '>';
        pos += getAddress(f, out + pos);               // destination
        // digipeaters: '*' on the last one that has the H bit set
        int nd = (int)((addrEnd - 14) / 7), lastH = -1;
        for (int d = 0; d < nd; d++) if (f[14 + 7 * d + 6] & 0x80) lastH = d;
        for (int d = 0; d < nd; d++) {
            out[pos++] = ',';
            size_t n = getAddress(f + 14 + 7 * d, tmp);
            memcpy(out + pos, tmp, n); pos += n;
            if (d == lastH) out[pos++] = '*';
        }
        out[pos++] = ':';
        memcpy(out + pos, f + addrEnd + 2, infoLen); pos += infoLen;
        out[pos] = 0;
        return pos;
    }

    // ---- G3RUH encoder ------------------------------------------------------------------------------------------------

    namespace {
        struct BitWriter {
            uint8_t* out; size_t max; size_t nbits;
            uint32_t lfsr; uint8_t level; uint8_t ones;
            bool overflow;

            void raw(uint8_t x) {                   // scrambled bit on air
                if ((nbits >> 3) >= max) { overflow = true; return; }
                if ((nbits & 7) == 0) out[nbits >> 3] = 0;
                if (x) out[nbits >> 3] |= (uint8_t)(0x80 >> (nbits & 7));
                nbits++;
            }
            void line(uint8_t bit) {                // HDLC bit -> NRZI -> scrambler
                if (!bit) level ^= 1;
                uint8_t x = (uint8_t)(level ^ ((lfsr >> 16) & 1) ^ ((lfsr >> 11) & 1));
                lfsr = ((lfsr << 1) | x) & 0x1FFFF;
                raw(x);
            }
            void flagByte() {                       // 0x7E, LSB first, no stuffing
                for (int i = 0; i < 8; i++) line((uint8_t)((0x7E >> i) & 1));
                ones = 0;
            }
            void dataByte(uint8_t v) {              // LSB first with bit stuffing
                for (int i = 0; i < 8; i++) {
                    uint8_t b = (v >> i) & 1;
                    line(b);
                    if (b) { if (++ones == 5) { line(0); ones = 0; } } else ones = 0;
                }
            }
        };
    }

    size_t g3ruhEncode(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOut, size_t leadFlags) {
        // worst case size: every byte may need up to 20% extra bits; compute exactly with a dry run
        for (;;) {
            BitWriter w = { out, maxOut, 0, 0, 0, 0, false };
            for (size_t i = 0; i < leadFlags; i++) w.flagByte();
            for (size_t i = 0; i < len; i++) w.dataByte(frame[i]);
            uint16_t fcs = crc16(frame, len);
            w.dataByte((uint8_t)(fcs & 0xFF));
            w.dataByte((uint8_t)(fcs >> 8));
            w.flagByte();
            w.flagByte();
            while (w.nbits & 7) w.line((uint8_t)(((0x7E >> (w.nbits & 7)) & 1)));   // pad to a whole byte
            if (!w.overflow) return w.nbits >> 3;
            if (leadFlags == 0) return 0;
            leadFlags = (leadFlags > 4) ? leadFlags - 4 : 0;
        }
    }

    // ---- G3RUH decoder ------------------------------------------------------------------------------------------------

    void Decoder::reset() {
        lfsr = 0; prevLevel = 0; ones = 0; synced = false; nbits = 0;
    }

    void Decoder::feed(const uint8_t* data, size_t len, FrameCallback cb, void* ctx) {
        for (size_t k = 0; k < len; k++) {
            for (int i = 7; i >= 0; i--) {
                uint8_t in = (data[k] >> i) & 1;
                uint8_t d  = (uint8_t)(in ^ ((lfsr >> 16) & 1) ^ ((lfsr >> 11) & 1));    // descrambler
                lfsr = ((lfsr << 1) | in) & 0x1FFFF;
                uint8_t bit = (d == prevLevel) ? 1 : 0;                                     // NRZI: no change = 1
                prevLevel = d;

                if (bit) {
                    if (ones < 255) ones++;
                    if (ones >= 7) { synced = false; nbits = 0; continue; }                 // abort sequence
                    if (synced) {
                        if (((nbits >> 3) >= MAX_FRAME + 3)) { synced = false; nbits = 0; continue; }
                        if ((nbits & 7) == 0) buf[nbits >> 3] = 0;
                        buf[nbits >> 3] |= (uint8_t)(1 << (nbits & 7));
                        nbits++;
                    }
                } else {
                    if (ones == 6) {                                                        // flag: 0 111111 0
                        if (synced && nbits >= 7 && ((nbits - 7) & 7) == 0) {
                            size_t n = (nbits - 7) >> 3;
                            if (n >= 18) {
                                size_t fl = n - 2;
                                uint16_t fcs = (uint16_t)(buf[fl] | (buf[fl + 1] << 8));
                                if (crc16(buf, fl) == fcs && cb) cb(buf, fl, ctx);
                            }
                        }
                        synced = true; nbits = 0;
                    } else if (ones == 5) {
                        // stuffed zero: drop it
                    } else if (synced) {
                        if (((nbits >> 3) >= MAX_FRAME + 3)) { synced = false; nbits = 0; ones = 0; continue; }
                        if ((nbits & 7) == 0) buf[nbits >> 3] = 0;
                        nbits++;                                                            // zero bit (buffer byte already cleared)
                    }
                    ones = 0;
                }
            }
        }
    }

}
