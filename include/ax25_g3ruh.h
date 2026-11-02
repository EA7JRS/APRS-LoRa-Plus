/* Copyright (C) 2026 Manuel - EA7JRS
 * AX.25 UI frames + 9600 baud G3RUH (scrambled NRZI FSK) bit stream.
 * Pure C++ (no Arduino dependencies) so it can be unit tested on a PC.
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef AX25_G3RUH_H_
#define AX25_G3RUH_H_

#include <stddef.h>
#include <stdint.h>

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

    typedef void (*FrameCallback)(const uint8_t* frame, size_t len, void* ctx);   // frame without FCS (FCS already verified)

    // Bit-level streaming decoder (descrambler + NRZI + HDLC). Needs no byte alignment: any bit offset works
    class Decoder {
    public:
        Decoder() { reset(); }
        void reset();
        void feed(const uint8_t* data, size_t len, FrameCallback cb, void* ctx);
    private:
        uint32_t lfsr;
        uint8_t  prevLevel;
        uint8_t  ones;
        bool     synced;
        size_t   nbits;
        uint8_t  buf[MAX_FRAME + 4];
    };

}

#endif
