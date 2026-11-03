/* Copyright (C) 2026 Manuel - EA7JRS
 * FX.25: AX.25 wrapped in a Reed-Solomon forward error correction block (correlation tag + data + check bytes).
 * Pure C++ (no Arduino dependencies) so it can be unit tested on a PC.
 *
 * Format as published by Stuart Longland VK4MSL (FX.25 specification) and as used by Dire Wolf (WB2OSZ, GPL-2.0-or-later):
 *   64-bit correlation tag, sent LSB first  +  k data bytes (flag + bit-stuffed AX.25 frame + FCS + flag, padded with flags)
 *   +  16 / 32 / 64 check bytes of a Reed-Solomon code over GF(2^8) (poly 0x11d, first root alpha^1, primitive element alpha).
 *   The RS word is always RS(255, 255 - nroots): the data bytes that are not sent are zeros placed after the sent data bytes.
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef FX25_H_
#define FX25_H_

#include <stddef.h>
#include <stdint.h>

namespace FX25 {

    constexpr int    TAG_MIN         = 1;           // correlation tags 0x01..0x0B are defined
    constexpr int    TAG_MAX         = 11;
    constexpr size_t BLOCK_SIZE      = 255;         // RS(255, 255 - nroots)
    constexpr size_t MAX_DATA        = 239;         // RS(255,239)
    constexpr size_t MAX_CHECK       = 64;
    constexpr int    TAG_MAX_ERRORS  = 8;           // bits that may be wrong in the 64-bit tag and still be accepted (the closest tags differ in 32 bits)

    uint64_t tagValue(int tag);                     // 64-bit correlation tag (transmitted LSB first)
    int      tagData(int tag);                      // data bytes sent over the air for this tag
    int      tagCheck(int tag);                     // check bytes: 16, 32 or 64

    // Smallest tag with the requested number of check bytes (16, 32, 64) that carries dlen data bytes. 0 if none
    int      pickTag(int checkBytes, size_t dlen);

    // Tag whose 64-bit value is closest to reg (the last 64 received bits, the oldest bit in bit 0). 0 if none within TAG_MAX_ERRORS
    int      matchTag(uint64_t reg);

    // Bit-stuffs frame+FCS between flags into out[0..MAX_DATA) and pads with flag bits. Returns the number of bytes actually used
    // (the rest is padding), or 0 if it does not fit
    size_t   stuffFrame(const uint8_t* frame, size_t len, uint8_t* out);

    // Generic Reed-Solomon (also used by IL2P, which starts at alpha^0): k data bytes -> nroots check bytes. fcr = exponent of the first root (0 or 1)
    void     rsEncodeG(int fcr, int nroots, const uint8_t* data, size_t k, uint8_t* check);
    // Corrects block (n = data + check bytes) in place. Returns the number of corrected bytes or -1. positions (optional) receives their indexes
    int      rsDecodeG(int fcr, int nroots, uint8_t* block, size_t n, int* positions);

    // FX.25 Reed-Solomon. block = BLOCK_SIZE bytes: data (255 - nroots bytes, zero padded), then nroots check bytes
    void     rsEncode(int nroots, const uint8_t* data, uint8_t* check);
    // Corrects block in place. Returns the number of corrected bytes, or -1 if it could not be corrected.
    // Corrections inside [fixedZeroFrom, 255 - nroots) (the bytes that were never sent and must be zero) make it fail
    int      rsDecode(int nroots, uint8_t* block, size_t fixedZeroFrom);

}

#endif
