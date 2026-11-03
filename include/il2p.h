/* Copyright (C) 2026 Manuel - EA7JRS
 * IL2P (Improved Layer 2 Protocol): AX.25 with compact headers and Reed-Solomon forward error correction.
 * Pure C++ (no Arduino dependencies) so it can be unit tested on a PC.
 *
 * Follows the IL2P specification (Nino Carrillo KK4HEJ, tarpn.net) and is compatible with Dire Wolf 1.7 (WB2OSZ, GPL-2.0-or-later),
 * from whose implementation the scrambler and the block layout were checked and adapted.
 * Only UI frames are handled (what APRS uses): type 1 header for frames without digipeaters, type 0 (the whole AX.25 frame is
 * the payload) otherwise.
 * Part of AIS/LoRa APRS Tracker - Manuel Canca EA7JRS (based on LoRa APRS Tracker by Ricardo Guzman CA2RXU, GPL-3.0)
 */
#ifndef IL2P_H_
#define IL2P_H_

#include <stddef.h>
#include <stdint.h>

namespace IL2P {

    constexpr uint32_t SYNC_WORD     = 0xF15E48;    // sent after the preamble (0x55 bytes), MSB first
    constexpr size_t   HEADER_SIZE   = 13;
    constexpr size_t   HEADER_PARITY = 2;
    constexpr size_t   HEADER_TOTAL  = HEADER_SIZE + HEADER_PARITY;
    constexpr size_t   MAX_PAYLOAD   = 1023;        // by the specification
    constexpr size_t   RX_MAX_PAYLOAD = 330;        // largest payload the receiver accepts (memory)
    constexpr size_t   RX_MAX_ENCODED = 400;        // its encoded size is always below this

    // AX.25 UI frame (without FCS) -> IL2P header + parity + encoded payload (sync word and preamble not included).
    // Returns the number of bytes, or 0 if the frame is not a UI frame or does not fit in maxOut
    size_t encode(const uint8_t* frame, size_t len, uint8_t* out, size_t maxOut);

    // Received header (HEADER_TOTAL bytes after the sync word): Reed-Solomon + descrambling. Returns the number of corrected bytes or -1
    int    clarifyHeader(const uint8_t* rec, uint8_t* hdr);
    // Payload size announced by a clarified header, and the number of bytes (with parity) that follow it on the air.
    // Returns the encoded size (0 = no payload), or -1 if the header is not valid
    int    encodedPayloadSize(const uint8_t* hdr, size_t* payloadLen);
    // Header + encoded payload -> AX.25 UI frame (without FCS). Returns its length, or 0 if it could not be recovered / is not a UI frame
    size_t decode(const uint8_t* hdr, const uint8_t* encodedPayload, uint8_t* frame, size_t maxFrame);

    // building blocks (exposed for the PC tests)
    void   scrambleBlock(const uint8_t* in, uint8_t* out, size_t len);
    void   descrambleBlock(const uint8_t* in, uint8_t* out, size_t len);

}

#endif
