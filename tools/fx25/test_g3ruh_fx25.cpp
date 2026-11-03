/* Copyright (C) 2026 Manuel - EA7JRS
 * PC loopback test of the 9600 baud G3RUH path with FX.25: encoder -> (damaged) air bits -> decoder.
 *   g++ -O2 -Iinclude tools/fx25/test_g3ruh_fx25.cpp src/ax25_g3ruh.cpp src/fx25.cpp -o /tmp/test_g3 && /tmp/test_g3
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "ax25_g3ruh.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Rx { std::vector<std::string> frames; };
static void onFrame(const uint8_t* f, size_t n, void* ctx) {
    char tnc2[600];
    if (AX25G3::frameToTnc2(f, n, tnc2, sizeof(tnc2))) ((Rx*)ctx)->frames.push_back(tnc2);
}

// feeds the air bytes in blocks of 255 bytes (as the radio does) after some random idle bytes
static Rx receive(const std::vector<uint8_t>& air, AX25G3::Decoder& dec) {
    Rx rx;
    for (size_t i = 0; i < air.size(); i += 255) dec.feed(air.data() + i, std::min<size_t>(255, air.size() - i), onFrame, &rx);
    return rx;
}

static std::vector<uint8_t> withNoiseAround(const uint8_t* air, size_t n, unsigned seed) {
    srand(seed);
    std::vector<uint8_t> v;
    for (int i = 0; i < 37; i++) v.push_back((uint8_t)rand());
    v.insert(v.end(), air, air + n);
    for (int i = 0; i < 40; i++) v.push_back((uint8_t)rand());
    return v;
}

int main() {
    const char* msgs[] = {
        "EA7JRS-9>APLRT1,WIDE1-1:!3737.12N/00455.12W>APRS LoRa+ test",
        "EA7JRS-9>APLRT1:>short status",
        "EA7JRS-9>APLRT1,WIDE1-1,WIDE2-1:@092345z3737.12N/00455.12W>123/045/A=000456 comment that is a bit longer to need the next block size ~~~~ 0123456789",
    };
    int checks[] = { 16, 32, 64 };
    for (const char* m : msgs) {
        uint8_t frame[AX25G3::MAX_FRAME], air[256];
        size_t fl = AX25G3::tnc2ToFrame(m, frame, sizeof(frame));
        CHECK(fl > 0, "tnc2ToFrame");

        // plain AX.25
        size_t n = AX25G3::g3ruhEncode(frame, fl, air, sizeof(air), 60);
        AX25G3::Decoder d0;
        Rx r0 = receive(withNoiseAround(air, n, 1), d0);
        CHECK(r0.frames.size() == 1 && r0.frames[0] == m, "plain AX.25 round trip");

        for (int cb : checks) {
            size_t nf = AX25G3::g3ruhEncodeFx25(frame, fl, air, sizeof(air), 60, cb);
            if (nf == 0) { printf("  %-3zu bytes frame, %d check bytes: does not fit (falls back to AX.25)\n", fl, cb); continue; }
            std::vector<uint8_t> clean = withNoiseAround(air, nf, 2);

            // 1. clean channel: a normal AX.25 decoder (no FX.25 knowledge = our Hdlc path) and ours both give exactly one frame
            AX25G3::Decoder d1;
            Rx r1 = receive(clean, d1);
            CHECK(r1.frames.size() == 1 && r1.frames[0] == m, "FX.25 clean (%d): %zu frames", cb, r1.frames.size());
            CHECK(d1.fx25Blocks == 1 && d1.fx25Fixed == 0, "FX.25 clean: block counted, not 'fixed'");

            // 2. damaged channel: random air bit errors inside the block
            int recovered = 0, byFx = 0, trials = 200;
            for (int t = 0; t < trials; t++) {
                std::vector<uint8_t> bad = clean;
                srand(100 + t);
                int nerr = cb / 16;                     // 1 / 2 / 4 channel errors: the scrambler turns each into up to 3 byte errors, within t = 8 / 16 / 32
                size_t from = (37 + 60 + 8) * 8, to = (37 + nf - 2) * 8;
                for (int e = 0; e < nerr; e++) { size_t b = from + (size_t)rand() % (to - from); bad[b >> 3] ^= (uint8_t)(0x80 >> (b & 7)); }
                AX25G3::Decoder d2;
                Rx r2 = receive(bad, d2);
                if (r2.frames.size() == 1 && r2.frames[0] == m) recovered++;
                CHECK(r2.frames.size() <= 1, "duplicate frames (%zu)", r2.frames.size());
                byFx += d2.fx25Fixed ? 1 : 0;
            }
            printf("  %-3zu bytes frame, %2d check bytes: %zu air bytes; %d/%d damaged blocks recovered (%d only thanks to FX.25)\n", fl, cb, nf, recovered, trials, byFx);
            CHECK(recovered == trials, "FX.25 %d check bytes did not recover every block", cb);
        }
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
