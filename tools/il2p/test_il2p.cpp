/* Copyright (C) 2026 Manuel - EA7JRS
 * PC tests for IL2P (src/il2p.cpp). vectors.txt holds TNC2 frames and the bytes Dire Wolf's il2p_encode_frame() produces for them
 * (header + parity + payload, without preamble and sync word); ours must be identical, and must decode back.
 *   g++ -O2 -Iinclude tools/il2p/test_il2p.cpp src/il2p.cpp src/fx25.cpp src/ax25_g3ruh.cpp -o /tmp/test_il2p && /tmp/test_il2p tools/il2p/vectors.txt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "il2p.h"
#include "ax25_g3ruh.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::string hex(const uint8_t* d, size_t n) { std::string s; char b[3]; for (size_t i = 0; i < n; i++) { snprintf(b, 3, "%02x", d[i]); s += b; } return s; }

// header + payload -> frame, through the same steps a receiver does
static size_t decodeAll(const uint8_t* enc, size_t n, uint8_t* frame, size_t maxFrame, int* hdrFixed = nullptr) {
    uint8_t hdr[IL2P::HEADER_SIZE];
    int e = IL2P::clarifyHeader(enc, hdr);
    if (hdrFixed) *hdrFixed = e;
    if (e < 0) return 0;
    size_t plen;
    int el = IL2P::encodedPayloadSize(hdr, &plen);
    if (el < 0 || IL2P::HEADER_TOTAL + (size_t)el > n) return 0;
    return IL2P::decode(hdr, enc + IL2P::HEADER_TOTAL, frame, maxFrame);
}

// IL2P bytes -> air bytes the way Dire Wolf sends IL2P at 9600 baud (gen_tone.c: no G3RUH scrambler and no NRZI), after a preamble of 0x55
static std::vector<uint8_t> toAir(const uint8_t* body, size_t n, int lead) {
    std::vector<uint8_t> bits;
    for (int i = 0; i < lead; i++) for (int b = 7; b >= 0; b--) bits.push_back((0x55 >> b) & 1);
    const uint8_t sync[3] = { 0xF1, 0x5E, 0x48 };
    for (int i = 0; i < 3; i++) for (int b = 7; b >= 0; b--) bits.push_back((sync[i] >> b) & 1);
    for (size_t i = 0; i < n; i++) for (int b = 7; b >= 0; b--) bits.push_back((body[i] >> b) & 1);
    for (int i = 0; i < 16; i++) bits.push_back(i & 1);
    std::vector<uint8_t> air;
    uint8_t cur = 0; int nb = 0;
    for (uint8_t bit : bits) {
        cur = (uint8_t)((cur << 1) | bit);
        if (++nb == 8) { air.push_back(cur); cur = 0; nb = 0; }
    }
    while (nb) { cur <<= 1; if (++nb == 8) { air.push_back(cur); nb = 0; } }
    return air;
}
static std::vector<std::string> gotFrames;
static void onFrame(const uint8_t* f, size_t n, void*) {
    char t[800];
    if (AX25G3::frameToTnc2(f, n, t, sizeof t)) gotFrames.push_back(t);
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tools/il2p/vectors.txt";
    FILE* f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); return 2; }
    char line[4096];
    int n = 0;
    srand(3);
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        char* bar = strrchr(line, '|');
        if (!bar) continue;
        *bar = 0;
        std::string ref = bar + 1;
        const char* tnc2 = line;

        uint8_t frame[AX25G3::MAX_FRAME], enc[1100], back[AX25G3::MAX_FRAME + 20];
        size_t fl = AX25G3::tnc2ToFrame(tnc2, frame, sizeof frame);
        CHECK(fl > 0, "tnc2ToFrame: %s", tnc2);
        size_t el = IL2P::encode(frame, fl, enc, sizeof enc);
        CHECK(el > 0, "encode: %s", tnc2);
        CHECK(hex(enc, el) == ref, "differs from Dire Wolf: %s\n   ours %s\n   ref  %s", tnc2, hex(enc, el).c_str(), ref.c_str());

        // round trip (compare as TNC2: the command/response bits are not carried the same way)
        size_t bl = decodeAll(enc, el, back, sizeof back);
        char t2[700];
        CHECK(bl > 0 && AX25G3::frameToTnc2(back, bl, t2, sizeof t2) && strcmp(t2, tnc2) == 0, "round trip: %s -> %s", tnc2, bl ? t2 : "(failed)");

        // errors: one byte in the header (t = 1) and as many bytes in each payload block as its parity allows
        uint8_t bad[1100];
        memcpy(bad, enc, el);
        bad[rand() % IL2P::HEADER_TOTAL] ^= (uint8_t)(1 + rand() % 255);
        bl = decodeAll(bad, el, back, sizeof back);
        CHECK(bl > 0 && AX25G3::frameToTnc2(back, bl, t2, sizeof t2) && strcmp(t2, tnc2) == 0, "1 header byte error: %s", tnc2);
        if (el > IL2P::HEADER_TOTAL + 4) {
            memcpy(bad, enc, el);
            bad[IL2P::HEADER_TOTAL + rand() % (el - IL2P::HEADER_TOTAL)] ^= (uint8_t)(1 + rand() % 255);
            bl = decodeAll(bad, el, back, sizeof back);
            CHECK(bl > 0 && AX25G3::frameToTnc2(back, bl, t2, sizeof t2) && strcmp(t2, tnc2) == 0, "1 payload byte error: %s", tnc2);
        }
        // over the air: Dire Wolf's bytes -> our streaming decoder (blocks of 255 bytes, like the radio)
        {
            std::vector<uint8_t> ref8;
            for (size_t i = 0; i + 1 < ref.size(); i += 2) ref8.push_back((uint8_t)strtol(ref.substr(i, 2).c_str(), nullptr, 16));
            std::vector<uint8_t> air = toAir(ref8.data(), ref8.size(), 12);
            AX25G3::Decoder dec;
            gotFrames.clear();
            for (size_t i = 0; i < air.size(); i += 255) dec.feed(air.data() + i, std::min<size_t>(255, air.size() - i), onFrame, nullptr);
            CHECK(gotFrames.size() == 1 && gotFrames[0] == tnc2, "over the air: %s -> %zu frame(s)", tnc2, gotFrames.size());
            // our own transmitter
            uint8_t out[256];
            size_t on = AX25G3::g3ruhEncodeIl2p(frame, fl, out, sizeof out, 60);
            if (on == 0) printf("  (does not fit in 255 bytes: %zu byte frame)\n", fl);
            else {
                AX25G3::Decoder dec2;
                gotFrames.clear();
                dec2.feed(out, on, onFrame, nullptr);
                CHECK(gotFrames.size() == 1 && gotFrames[0] == tnc2, "own IL2P transmitter: %s -> %zu frame(s)", tnc2, gotFrames.size());
                // one channel bit error anywhere after the sync word
                int okErr = 0;
                for (int t = 0; t < 50; t++) {
                    uint8_t bad2[256];
                    memcpy(bad2, out, on);
                    size_t lo = (60 + 3) * 8 + 8, hi = (on - 2) * 8;
                    for (int e = 0; e < 1; e++) { size_t b = lo + (size_t)rand() % (hi - lo); bad2[b >> 3] ^= (uint8_t)(0x80 >> (b & 7)); }
                    AX25G3::Decoder d3;
                    gotFrames.clear();
                    d3.feed(bad2, on, onFrame, nullptr);
                    if (gotFrames.size() == 1 && gotFrames[0] == tnc2) okErr++;
                }
                printf("  %-3zu byte frame, %3zu air bytes: 1 channel bit error in 50 tries: %d recovered\n", fl, on, okErr);
                // Without the G3RUH scrambler one channel bit error is one byte error: Reed-Solomon fixes it in the header and in any payload
                CHECK(okErr >= 48, "IL2P with 1 channel bit error recovered only %d/50", okErr);
            }
        }
        n++;
    }
    fclose(f);
    printf("%d frames compared with Dire Wolf\n", n);
    CHECK(n >= 10, "too few vectors");
    printf(fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
