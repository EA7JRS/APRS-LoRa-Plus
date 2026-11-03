/* Copyright (C) 2026 Manuel - EA7JRS
 * AX.25 UI frames + 9600 baud G3RUH (scrambled NRZI FSK) bit stream.
 * Pure C++ (no Arduino dependencies) so it can be unit tested on a PC.
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef AX25_G3RUH_H_
#define AX25_G3RUH_H_

#include <stddef.h>
#include <stdint.h>
#include "fx25.h"
#include "il2p.h"

namespace AX25G3 {

    constexpr size_t MAX_FRAME = 330;                   // max AX.25 frame bytes (without FCS) accepted by the decoder

    uint16_t crc16(const uint8_t* data, size_t len);    // CRC-16/X.25 (the AX.25 FCS)

    // "SRC>DST,DIGI1*,DIGI2:info"  ->  AX.25 UI frame without FCS. Returns 0 on error
    size_t tnc2ToFrame(const char* tnc2, uint8_t* out, size_t maxOut);
    // AX.25 UI frame without FCS -> "SRC>DST,DIGI*:info". Returns 0 if it is not a UI frame
    size_t frameToTnc2(const uint8_t* frame, size_t len, char* out, size_t maxOut);

    // Builds the on-air bytes: leading flags + HDLC frame (bit stuffing + FCS) + closing flags, NRZI + G3RUH scrambler, MSB first.
    // leadFlags is reduced if the result would not fit in maxOut. Returns 0 if the frame itself does not fit
    size_t g3ruhEncode(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOut, size_t leadFlags);

    // Same as g3ruhEncode but the frame is sent as an FX.25 block: correlation tag + Reed-Solomon (checkBytes = 16, 32 or 64).
    // AX.25 receivers that do not know FX.25 still decode the frame inside the block. Returns 0 if the frame does not fit
    // (the caller then falls back to plain AX.25)
    size_t g3ruhEncodeFx25(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOut, size_t leadFlags, int checkBytes);

    // Same again for IL2P (preamble, sync word, header and Reed-Solomon payload; scrambled by the G3RUH scrambler, no NRZI).
    // Only IL2P stations decode it. Returns 0 if the frame does not fit (the caller then falls back to plain AX.25)
    size_t g3ruhEncodeIl2p(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOut, size_t leadBytes);

    typedef void (*FrameCallback)(const uint8_t* frame, size_t len, void* ctx);   // frame without FCS (FCS already verified)

    // HDLC receiver for an already NRZI-decoded bit stream (flags, bit un-stuffing, FCS)
    class Hdlc {
    public:
        Hdlc() { reset(); }
        void reset();
        void pushBit(uint8_t b, FrameCallback cb, void* ctx);
    private:
        uint8_t  ones;
        bool     synced;
        size_t   nbits;
        uint8_t  buf[MAX_FRAME + 4];
    };

    // Bit-level streaming decoder (descrambler + NRZI + HDLC). Needs no byte alignment: any bit offset works.
    // It also recognises FX.25 blocks (correlation tag) and corrects them with Reed-Solomon; the frame of a block that
    // the plain HDLC path already delivered is not delivered twice
    class Decoder {
    public:
        Decoder() : fx25Blocks(0), fx25Fixed(0), il2pFrames(0) { reset(); }
        void reset();
        void feed(const uint8_t* data, size_t len, FrameCallback cb, void* ctx);
        uint32_t fx25Blocks;                // FX.25 blocks received (statistics)
        uint32_t fx25Fixed;                 // frames only recovered thanks to FX.25 (HDLC alone had failed)
        uint32_t il2pFrames;                // IL2P frames received
    private:
        static void onHdlc(const uint8_t* frame, size_t len, void* self);
        static void onFx(const uint8_t* frame, size_t len, void* self);
        void fxBit(uint8_t b);
        void fxFinish();
        void ilBit(uint8_t b);
        void ilDeliver();
        uint32_t lfsr;
        uint8_t  prevLevel;
        Hdlc     hdlc, fxHdlc;
        FrameCallback userCb;
        void*         userCtx;
        // FX.25
        uint64_t tagReg;
        uint8_t  fxState;                   // 0 searching the tag, 1 receiving the block
        uint8_t  fxTag;
        uint16_t fxBitsLeft, fxBitPos;
        uint8_t  fxBuf[FX25::BLOCK_SIZE];
        bool     fxDelivered;               // the plain HDLC path delivered a frame since the tag was found
        uint16_t lastFcs;
        size_t   lastLen;
        // IL2P
        uint32_t ilAcc;
        uint8_t  ilState;                   // 0 searching the sync word, 1 header, 2 payload
        bool     ilInverted;
        uint8_t  ilBc;
        uint16_t ilN, ilNeed;
        uint8_t  ilRxHdr[IL2P::HEADER_TOTAL];
        uint8_t  ilHdr[IL2P::HEADER_SIZE];
        uint8_t  ilPay[IL2P::RX_MAX_ENCODED];
    };

}

#endif
