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
        static const uint16_t T[16] = { 0x0000, 0x1081, 0x2102, 0x3183, 0x4204, 0x5285, 0x6306, 0x7387, 0x8408, 0x9489, 0xA50A, 0xB58B, 0xC60C, 0xD68D, 0xE70E, 0xF78F };
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            crc = (uint16_t)((crc >> 4) ^ T[crc & 15]);
            crc = (uint16_t)((crc >> 4) ^ T[crc & 15]);
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
            bool plain;                             // sin aleatorizador (AFSK 1200)

            void raw(uint8_t x) {                   // scrambled bit on air
                if ((nbits >> 3) >= max) { overflow = true; return; }
                if ((nbits & 7) == 0) out[nbits >> 3] = 0;
                if (x) out[nbits >> 3] |= (uint8_t)(0x80 >> (nbits & 7));
                nbits++;
            }
            void line(uint8_t bit) {                // HDLC bit -> NRZI -> scrambler
                if (!bit) level ^= 1;
                if (plain) { raw(level); return; }
                uint8_t x = (uint8_t)(level ^ ((lfsr >> 16) & 1) ^ ((lfsr >> 11) & 1));
                lfsr = ((lfsr << 1) | x) & 0x1FFFF;
                raw(x);
            }
            void flagByte() {                       // 0x7E, LSB first, no stuffing
                for (int i = 0; i < 8; i++) line((uint8_t)((0x7E >> i) & 1));
                ones = 0;
            }
            void scrambledBit(uint8_t bit) {        // IL2P: G3RUH scrambler only, no NRZI
                if (plain) { raw(bit); return; }
                uint8_t x = (uint8_t)(bit ^ ((lfsr >> 16) & 1) ^ ((lfsr >> 11) & 1));
                lfsr = ((lfsr << 1) | x) & 0x1FFFF;
                raw(x);
            }
            void il2pByte(uint8_t v) {              // MSB first
                for (int i = 7; i >= 0; i--) scrambledBit((uint8_t)((v >> i) & 1));
            }
            void rawByte(uint8_t v) {               // LSB first, no bit stuffing (FX.25 tag and Reed-Solomon bytes)
                for (int i = 0; i < 8; i++) line((uint8_t)((v >> i) & 1));
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

    size_t g3ruhEncode(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOut, size_t leadFlags, bool scramble, size_t trailing) {
        // worst case size: every byte may need up to 20% extra bits; compute exactly with a dry run
        for (;;) {
            BitWriter w = { out, maxOut, 0, 0, 0, 0, false, !scramble };
            for (size_t i = 0; i < leadFlags; i++) w.flagByte();
            for (size_t i = 0; i < len; i++) w.dataByte(frame[i]);
            uint16_t fcs = crc16(frame, len);
            w.dataByte((uint8_t)(fcs & 0xFF));
            w.dataByte((uint8_t)(fcs >> 8));
            for (size_t i = 0; i < (trailing ? trailing : 1); i++) w.flagByte();
            while (w.nbits & 7) w.line((uint8_t)(((0x7E >> (w.nbits & 7)) & 1)));   // pad to a whole byte
            if (!w.overflow) return w.nbits >> 3;
            if (leadFlags == 0) return 0;
            leadFlags = (leadFlags > 4) ? leadFlags - 4 : 0;
        }
    }

    size_t g3ruhEncodeFx25(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOut, size_t leadFlags, int checkBytes, bool scramble, size_t trailing) {
        uint8_t data[FX25::MAX_DATA];
        size_t  used = FX25::stuffFrame(frame, len, data);
        if (used == 0) return 0;
        int tag = FX25::pickTag(checkBytes, used);
        if (tag == 0) return 0;
        const size_t k = (size_t)FX25::tagData(tag), nr = (size_t)FX25::tagCheck(tag);
        uint8_t blk[FX25::BLOCK_SIZE] = {0}, check[FX25::MAX_CHECK];
        memcpy(blk, data, k);                       // the bytes that are not sent stay at zero
        FX25::rsEncode((int)nr, blk, check);
        if (trailing == 0) trailing = 1;
        if (8 + k + nr + trailing > maxOut) return 0;
        while (leadFlags + 8 + k + nr + trailing > maxOut) leadFlags--;
        BitWriter w = { out, maxOut, 0, 0, 0, 0, false, !scramble };
        for (size_t i = 0; i < leadFlags; i++) w.flagByte();
        const uint64_t tv = FX25::tagValue(tag);
        for (int i = 0; i < 8; i++) w.rawByte((uint8_t)(tv >> (8 * i)));
        for (size_t i = 0; i < k; i++)  w.rawByte(blk[i]);
        for (size_t i = 0; i < nr; i++) w.rawByte(check[i]);
        for (size_t i = 0; i < trailing; i++) w.flagByte();
        return w.overflow ? 0 : (w.nbits >> 3);
    }

    size_t g3ruhEncodeIl2p(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOut, size_t leadBytes, bool scramble, size_t trailing) {
        uint8_t body[IL2P::HEADER_TOTAL + 300];
        size_t  bl = IL2P::encode(frame, len, body, sizeof(body));
        if (bl == 0) return 0;
        if (trailing == 0) trailing = 1;
        if (3 + bl + trailing > maxOut) return 0;
        while (leadBytes + 3 + bl + trailing > maxOut) leadBytes--;
        // IL2P no usa el aleatorizador G3RUH ni NRZI (lleva su propio aleatorizador): asi lo emiten Dire Wolf (gen_tone.c) y la
        // NinoTNC. Hasta 5.3.5 se aleatorizaba y solo lo entendian otras placas APRS LoRa+. 'scramble' se ignora aqui
        (void)scramble;
        BitWriter w = { out, maxOut, 0, 0, 0, 0, false, true };
        for (size_t i = 0; i < leadBytes; i++) w.il2pByte(0x55);
        w.il2pByte((uint8_t)(IL2P::SYNC_WORD >> 16));
        w.il2pByte((uint8_t)(IL2P::SYNC_WORD >> 8));
        w.il2pByte((uint8_t)IL2P::SYNC_WORD);
        for (size_t i = 0; i < bl; i++) w.il2pByte(body[i]);
        for (size_t i = 0; i < trailing; i++) w.il2pByte(0x55);
        return w.overflow ? 0 : (w.nbits >> 3);
    }

    // ---- HDLC / G3RUH decoder -----------------------------------------------------------------------------------------

    void Hdlc::reset() { ones = 0; synced = false; nbits = 0; }

    void Hdlc::pushBit(uint8_t bit, FrameCallback cb, void* ctx) {
        if (bit) {
            if (ones < 255) ones++;
            if (ones >= 7) { synced = false; nbits = 0; return; }                          // abort sequence
            if (synced) {
                if (((nbits >> 3) >= MAX_FRAME + 3)) { synced = false; nbits = 0; return; }
                if ((nbits & 7) == 0) buf[nbits >> 3] = 0;
                buf[nbits >> 3] |= (uint8_t)(1 << (nbits & 7));
                nbits++;
            }
            return;
        }
        if (ones == 6) {                                                                    // flag: 0 111111 0
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
            if (((nbits >> 3) >= MAX_FRAME + 3)) { synced = false; nbits = 0; ones = 0; return; }
            if ((nbits & 7) == 0) buf[nbits >> 3] = 0;
            nbits++;                                                                        // zero bit (buffer byte already cleared)
        }
        ones = 0;
    }

    void Decoder::reset() {
        lfsr = 0; prevLevel = 0;
        hdlc.reset(); fxHdlc.reset();
        userCb = nullptr; userCtx = nullptr;
        tagReg = 0; fxState = 0; fxTag = 0; fxBitsLeft = 0; fxBitPos = 0; fxDelivered = false; lastFcs = 0; lastLen = 0;
        ilAcc = 0; ilState = 0; ilInverted = false; ilBc = 0; ilN = 0; ilNeed = 0;
    }

    void Decoder::onHdlc(const uint8_t* frame, size_t len, void* self) {
        Decoder* d = (Decoder*)self;
        d->lastLen = len; d->lastFcs = crc16(frame, len); d->fxDelivered = true;
        if (d->userCb) d->userCb(frame, len, d->userCtx);
    }

    void Decoder::onFx(const uint8_t* frame, size_t len, void* self) {
        Decoder* d = (Decoder*)self;
        if (d->fxDelivered && len == d->lastLen && crc16(frame, len) == d->lastFcs) return;      // the HDLC path already delivered it
        d->fx25Fixed++;
        if (d->userCb) d->userCb(frame, len, d->userCtx);
    }

    void Decoder::fxFinish() {
        const int k  = FX25::tagData(fxTag), nr = FX25::tagCheck(fxTag);
        // received layout: k data bytes, nr check bytes  ->  RS block: data, zeros, check
        memmove(fxBuf + (FX25::BLOCK_SIZE - nr), fxBuf + k, (size_t)nr);
        memset(fxBuf + k, 0, FX25::BLOCK_SIZE - (size_t)nr - (size_t)k);
        if (FX25::rsDecode(nr, fxBuf, (size_t)k) >= 0) {
            fx25Blocks++;
            fxHdlc.reset();
            for (int i = 0; i < k; i++)
                for (int b = 0; b < 8; b++) fxHdlc.pushBit((uint8_t)((fxBuf[i] >> b) & 1), onFx, this);
        }
        fxState = 0; tagReg = 0;
    }

    void Decoder::fxBit(uint8_t b) {
        if (fxState == 0) {
            tagReg = (tagReg >> 1) | ((uint64_t)b << 63);
            int t = FX25::matchTag(tagReg);
            if (t) {
                fxTag = (uint8_t)t; fxState = 1; fxBitPos = 0;
                fxBitsLeft = (uint16_t)((FX25::tagData(t) + FX25::tagCheck(t)) * 8);
                memset(fxBuf, 0, sizeof(fxBuf));
                fxDelivered = false;
            }
        } else {
            if (b) fxBuf[fxBitPos >> 3] |= (uint8_t)(1 << (fxBitPos & 7));
            fxBitPos++;
            if (--fxBitsLeft == 0) fxFinish();
        }
    }

    void Decoder::ilDeliver() {
        uint8_t frame[IL2P::RX_MAX_PAYLOAD + 40];
        size_t n = IL2P::decode(ilHdr, ilPay, frame, sizeof(frame));
        if (n >= 16) { il2pFrames++; if (userCb) userCb(frame, n, userCtx); }
        ilState = 0;
    }

    void Decoder::ilBit(uint8_t b) {
        ilAcc = ((ilAcc << 1) | b) & 0x00FFFFFF;
        if (ilState == 0) {
            if (__builtin_popcount(ilAcc ^ IL2P::SYNC_WORD) <= 1)                               ilInverted = false;
            else if (__builtin_popcount((~ilAcc & 0x00FFFFFF) ^ IL2P::SYNC_WORD) <= 1)          ilInverted = true;
            else return;
            ilState = 1; ilBc = 0; ilN = 0;
            return;
        }
        if (++ilBc < 8) return;
        ilBc = 0;
        uint8_t by = (uint8_t)(ilInverted ? ~ilAcc : ilAcc);
        if (ilState == 1) {
            ilRxHdr[ilN++] = by;
            if (ilN < IL2P::HEADER_TOTAL) return;
            size_t plen;
            if (IL2P::clarifyHeader(ilRxHdr, ilHdr) < 0) { ilState = 0; return; }
            int el = IL2P::encodedPayloadSize(ilHdr, &plen);
            if (el < 0) { ilState = 0; return; }
            if (el == 0) { ilDeliver(); return; }
            ilNeed = (uint16_t)el; ilN = 0; ilState = 2;
        } else {
            ilPay[ilN++] = by;
            if (ilN >= ilNeed) ilDeliver();
        }
    }

    void Decoder::feed(const uint8_t* data, size_t len, FrameCallback cb, void* ctx) {
        userCb = cb; userCtx = ctx;
        for (size_t k = 0; k < len; k++) {
            for (int i = 7; i >= 0; i--) {
                uint8_t in = (data[k] >> i) & 1;
                uint8_t d  = scrambled ? (uint8_t)(in ^ ((lfsr >> 16) & 1) ^ ((lfsr >> 11) & 1)) : in;    // descrambler (no en AFSK 1200)
                lfsr = ((lfsr << 1) | in) & 0x1FFFF;
                uint8_t bit = (d == prevLevel) ? 1 : 0;                                     // NRZI: no change = 1
                prevLevel = d;
                ilBit(in);                                                                  // IL2P: el bit tal cual (sin aleatorizador ni NRZI)
                fxBit(bit);
                hdlc.pushBit(bit, onHdlc, this);
            }
        }
    }

}
