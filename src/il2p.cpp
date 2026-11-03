/* Copyright (C) 2026 Manuel - EA7JRS
 * IL2P: compact-header AX.25 with Reed-Solomon FEC. See il2p.h
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 *
 * Header fields, scrambler and payload block layout follow the IL2P specification and were verified against Dire Wolf.
 */
#include <string.h>
#include "il2p.h"
#include "fx25.h"

namespace IL2P {

    namespace {

        // ---- scrambler (x^9 + x^4 + 1, restarted for every block) ----------------------------------------------------------
        constexpr int INIT_TX_LFSR = 0x00F;
        constexpr int INIT_RX_LFSR = 0x1F0;

        inline int scrambleBit(int in, int* state) {
            int out = ((*state >> 4) ^ *state) & 1;
            *state = (((((in ^ *state) & 1) << 9) | (*state ^ ((*state & 1) << 4)))) >> 1;
            return out;
        }
        inline int descrambleBit(int in, int* state) {
            int out = (in ^ *state) & 1;
            *state = ((*state >> 1) | ((in & 1) << 8)) ^ ((in & 1) << 3);
            return out;
        }

        // ---- header fields: bit "bitNum" of consecutive header bytes, least significant bit in byte lsbIndex --------------
        void setField(uint8_t* hdr, int bitNum, int lsbIndex, int width, int value) {
            while (width > 0 && value != 0) {
                if (value & 1) hdr[lsbIndex] |= (uint8_t)(1 << bitNum);
                value >>= 1; lsbIndex--; width--;
            }
        }
        int getField(const uint8_t* hdr, int bitNum, int lsbIndex, int width) {
            int result = 0;
            lsbIndex -= width - 1;
            while (width > 0) {
                result <<= 1;
                if (hdr[lsbIndex] & (1 << bitNum)) result |= 1;
                lsbIndex++; width--;
            }
            return result;
        }
        inline void setUi(uint8_t* h, int v)           { setField(h, 6, 0, 1, v); }
        inline void setPid(uint8_t* h, int v)          { setField(h, 6, 4, 4, v); }
        inline void setControl(uint8_t* h, int v)      { setField(h, 6, 11, 7, v); }
        inline void setFecLevel(uint8_t* h, int v)     { setField(h, 7, 0, 1, v); }
        inline void setHdrType(uint8_t* h, int v)      { setField(h, 7, 1, 1, v); }
        inline void setPayloadCount(uint8_t* h, int v) { setField(h, 7, 11, 10, v); }
        inline int  getUi(const uint8_t* h)            { return getField(h, 6, 0, 1); }
        inline int  getPid(const uint8_t* h)           { return getField(h, 6, 4, 4); }
        inline int  getControl(const uint8_t* h)       { return getField(h, 6, 11, 7); }
        inline int  getFecLevel(const uint8_t* h)      { return getField(h, 7, 0, 1); }
        inline int  getHdrType(const uint8_t* h)       { return getField(h, 7, 1, 1); }
        inline int  getPayloadCount(const uint8_t* h)  { return getField(h, 7, 11, 10); }

        // AX.25 PID <-> the 4 bit PID of the header
        int encodePid(int pid) {
            if ((pid & 0x30) == 0x20 || (pid & 0x30) == 0x10) return 0x2;
            switch (pid) {
                case 0x01: return 0x3; case 0x06: return 0x4; case 0x07: return 0x5; case 0x08: return 0x6;
                case 0xCC: return 0xB; case 0xCD: return 0xC; case 0xCE: return 0xD; case 0xCF: return 0xE;
                case 0xF0: return 0xF;
            }
            return -1;
        }
        int decodePid(int pid) {
            static const uint8_t axpid[16] = { 0xF0, 0xF0, 0x20, 0x01, 0x06, 0x07, 0x08, 0xF0, 0xF0, 0xF0, 0xF0, 0xCC, 0xCD, 0xCE, 0xCF, 0xF0 };
            return axpid[pid & 15];
        }

        // ---- payload layout ---------------------------------------------------------------------------------------------
        struct Layout { int count, blocks, smallSize, largeSize, largeCount, smallCount, parity; };

        // Returns the encoded size, 0 for no payload, -1 for an invalid size
        int layout(Layout& p, int payload, int maxFec) {
            memset(&p, 0, sizeof(p));
            if (payload < 0 || payload > (int)MAX_PAYLOAD) return -1;
            if (payload == 0) return 0;
            p.count = payload;
            if (maxFec) {
                p.blocks = (payload + 238) / 239;
                p.parity = 16;
            } else {
                p.blocks = (payload + 246) / 247;
            }
            p.smallSize  = payload / p.blocks;
            p.largeSize  = p.smallSize + 1;
            p.largeCount = payload - p.blocks * p.smallSize;
            p.smallCount = p.blocks - p.largeCount;
            if (!maxFec) {
                if (p.smallSize <= 61)       p.parity = 2;
                else if (p.smallSize <= 123) p.parity = 4;
                else if (p.smallSize <= 185) p.parity = 6;
                else if (p.smallSize <= 247) p.parity = 8;
                else return -1;
            }
            return p.smallCount * (p.smallSize + p.parity) + p.largeCount * (p.largeSize + p.parity);
        }

        int encodePayload(const uint8_t* payload, int size, int maxFec, uint8_t* enc) {
            Layout p;
            int e = layout(p, size, maxFec);
            if (e <= 0) return e;
            const uint8_t* pin = payload;
            uint8_t*       pout = enc;
            uint8_t scram[256], parity[16];
            for (int b = 0; b < p.blocks; b++) {
                int sz = b < p.largeCount ? p.largeSize : p.smallSize;     // large blocks first
                scrambleBlock(pin, scram, (size_t)sz);
                memcpy(pout, scram, (size_t)sz);
                FX25::rsEncodeG(0, p.parity, scram, (size_t)sz, parity);
                memcpy(pout + sz, parity, (size_t)p.parity);
                pin += sz; pout += sz + p.parity;
            }
            return e;
        }

        // Returns the number of payload bytes recovered (size) or -1
        int decodePayload(const uint8_t* rec, int size, int maxFec, uint8_t* out) {
            Layout p;
            int e = layout(p, size, maxFec);
            if (e <= 0) return -1;
            const uint8_t* pin = rec;
            uint8_t*       pout = out;
            uint8_t blk[256];
            for (int b = 0; b < p.blocks; b++) {
                int sz = b < p.largeCount ? p.largeSize : p.smallSize;
                memcpy(blk, pin, (size_t)(sz + p.parity));
                if (FX25::rsDecodeG(0, p.parity, blk, (size_t)(sz + p.parity), nullptr) < 0) return -1;
                descrambleBlock(blk, pout, (size_t)sz);
                pin += sz + p.parity; pout += sz;
            }
            return size;
        }

        // ---- addresses --------------------------------------------------------------------------------------------------
        inline bool sixbitOk(int c) { return c >= ' ' && c <= '_'; }

    }

    void scrambleBlock(const uint8_t* in, uint8_t* out, size_t len) {
        int state = INIT_TX_LFSR;
        memset(out, 0, len);
        bool   skipping = true;                 // the first 5 output bits are discarded: the scrambler has a delay of 5 bits
        size_t ob = 0;
        int    om = 0x80;
        for (size_t ib = 0; ib < len; ib++) {
            for (int im = 0x80; im != 0; im >>= 1) {
                int s = scrambleBit((in[ib] & im) != 0, &state);
                if (ib == 0 && im == 0x04) skipping = false;
                if (!skipping) {
                    if (s) out[ob] |= (uint8_t)om;
                    om >>= 1;
                    if (om == 0) { om = 0x80; ob++; }
                }
            }
        }
        for (int n = 0; n < 5; n++) {           // flush the last 5 bits with zeros
            int s = scrambleBit(0, &state);
            if (s) out[ob] |= (uint8_t)om;
            om >>= 1;
            if (om == 0) { om = 0x80; ob++; }
        }
    }

    void descrambleBlock(const uint8_t* in, uint8_t* out, size_t len) {
        int state = INIT_RX_LFSR;
        memset(out, 0, len);
        for (size_t b = 0; b < len; b++)
            for (int m = 0x80; m != 0; m >>= 1)
                if (descrambleBit((in[b] & m) != 0, &state)) out[b] |= (uint8_t)m;
    }

    size_t encode(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOut) {
        // find the end of the address field
        size_t addrEnd = 0;
        for (size_t i = 0; i + 7 <= len && i < 70; i += 7) if (frame[i + 6] & 1) { addrEnd = i + 7; break; }
        if (addrEnd < 14 || addrEnd + 2 > len) return 0;
        if (frame[addrEnd] != 0x03) return 0;                       // UI frames only
        const int  nAddr = (int)(addrEnd / 7);
        const int  pid4  = encodePid(frame[addrEnd + 1]);

        uint8_t hdr[HEADER_SIZE], copy[RX_MAX_PAYLOAD + 16];
        memset(hdr, 0, sizeof(hdr));
        const uint8_t* payload;
        size_t         plen;
        bool type1 = false;
        if (nAddr == 2 && pid4 >= 0) {
            type1 = true;
            for (int k = 0; k < 6 && type1; k++) {                  // callsign characters must exist in DEC SIXBIT
                if (!sixbitOk(frame[k] >> 1) || !sixbitOk(frame[7 + k] >> 1)) type1 = false;
            }
        }
        if (type1) {
            for (int k = 0; k < 6; k++) {
                hdr[k]     = (uint8_t)((frame[k] >> 1) - ' ');
                hdr[6 + k] = (uint8_t)((frame[7 + k] >> 1) - ' ');
            }
            // trailing spaces are not sent: they are the six bit code 0 anyway
            hdr[12] = (uint8_t)((((frame[6] >> 1) & 0x0F) << 4) | ((frame[13] >> 1) & 0x0F));
            setUi(hdr, 1);
            setPid(hdr, pid4);
            setControl(hdr, (0 << 6) | (1 << 2) | (5 << 3));        // UI, no poll, command
            payload = frame + addrEnd + 2;
            plen    = len - addrEnd - 2;
        } else {                                                    // type 0: transparent, the whole AX.25 frame is the payload
            if (len > sizeof(copy)) return 0;
            memcpy(copy, frame, len);
            copy[6]  |= 0x80;                                       // APRS: both C bits set (as Dire Wolf sends them)
            copy[13] |= 0x80;
            payload = copy;
            plen    = len;
        }
        if (plen > MAX_PAYLOAD || (plen == 0 && !type1)) return 0;
        setFecLevel(hdr, 0);                                        // automatic number of parity symbols
        setHdrType(hdr, type1 ? 1 : 0);
        setPayloadCount(hdr, (int)plen);

        Layout lay;
        int enc = layout(lay, (int)plen, 0);
        if (enc < 0) return 0;
        const size_t total = HEADER_TOTAL + (size_t)enc;
        if (total > maxOut) return 0;
        scrambleBlock(hdr, out, HEADER_SIZE);
        FX25::rsEncodeG(0, (int)HEADER_PARITY, out, HEADER_SIZE, out + HEADER_SIZE);
        if (enc > 0 && encodePayload(payload, (int)plen, 0, out + HEADER_TOTAL) != enc) return 0;
        return total;
    }

    int clarifyHeader(const uint8_t* rec, uint8_t* hdr) {
        uint8_t blk[HEADER_TOTAL];
        memcpy(blk, rec, HEADER_TOTAL);
        int e = FX25::rsDecodeG(0, (int)HEADER_PARITY, blk, HEADER_TOTAL, nullptr);
        if (e < 0) return -1;
        descrambleBlock(blk, hdr, HEADER_SIZE);
        return e;
    }

    int encodedPayloadSize(const uint8_t* hdr, size_t* payloadLen) {
        int count = getPayloadCount(hdr);
        if (payloadLen) *payloadLen = (size_t)count;
        if (count > (int)RX_MAX_PAYLOAD) return -1;
        Layout lay;
        int e = layout(lay, count, getFecLevel(hdr));
        if (e > (int)RX_MAX_ENCODED) return -1;
        if (e == 0 && getHdrType(hdr) == 0) return -1;              // type 0 always has a payload
        return e;
    }

    size_t decode(const uint8_t* hdr, const uint8_t* encodedPayload, uint8_t* frame, size_t maxFrame) {
        int type = getHdrType(hdr), fec = getFecLevel(hdr), count = getPayloadCount(hdr);
        uint8_t payload[RX_MAX_PAYLOAD + 1];
        if (count > (int)RX_MAX_PAYLOAD) return 0;
        if (count > 0 && decodePayload(encodedPayload, count, fec, payload) != count) return 0;
        if (type == 0) {
            if ((size_t)count > maxFrame || count < 16) return 0;
            memcpy(frame, payload, (size_t)count);
            return (size_t)count;
        }
        // type 1: addresses in the header, UI frames only
        if (getPid(hdr) == 0 || getPid(hdr) == 1 || !getUi(hdr)) return 0;
        if ((size_t)count + 16 > maxFrame) return 0;
        const int control = getControl(hdr);
        const bool cmd = (control & 0x04) != 0;
        for (int side = 0; side < 2; side++) {                      // 0 destination, 1 source
            uint8_t* a = frame + 7 * side;
            const uint8_t* c = hdr + 6 * side;
            int len = 6;
            while (len > 0 && (c[len - 1] & 0x3F) == 0) len--;       // trailing spaces
            for (int k = 0; k < 6; k++) {
                int ch = (c[k] & 0x3F) + ' ';
                if (k >= len) ch = ' ';
                if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ')) return 0;
                a[k] = (uint8_t)(ch << 1);
            }
            int ssid = side == 0 ? (hdr[12] >> 4) & 0x0F : hdr[12] & 0x0F;
            bool cbit = side == 0 ? cmd : !cmd;
            a[6] = (uint8_t)(0x60 | (ssid << 1) | (cbit ? 0x80 : 0) | (side == 1 ? 1 : 0));
        }
        frame[14] = 0x03;
        frame[15] = (uint8_t)decodePid(getPid(hdr));
        if (count > 0) memcpy(frame + 16, payload, (size_t)count);
        return 16 + (size_t)count;
    }

}
