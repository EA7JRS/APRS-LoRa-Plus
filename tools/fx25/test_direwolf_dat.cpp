/* Copyright (C) 2026 Manuel - EA7JRS
 * Feeds the FX.25 test files that Dire Wolf's own "fxsend" unit test writes (fx01.dat ... fx0b.dat: 16 flags + tag + data with
 * 8 damaged bytes + check bytes + 16 flags, as HDLC-level bits, LSB first) to our decoder, after NRZI + G3RUH scrambling,
 * and expects the frame TEST>WB2OSZ-1:Foo?Bar? to come out of every one.
 *   g++ -O2 -Iinclude tools/fx25/test_direwolf_dat.cpp src/ax25_g3ruh.cpp src/fx25.cpp -o /tmp/test_dw && /tmp/test_dw fx01.dat ... fx0b.dat
 */
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "ax25_g3ruh.h"

static std::vector<std::string> got;
static void onFrame(const uint8_t* f, size_t n, void*) {
    char t[600];
    if (AX25G3::frameToTnc2(f, n, t, sizeof(t))) got.push_back(t);
}

int main(int argc, char** argv) {
    int bad = 0;
    for (int a = 1; a < argc; a++) {
        FILE* fp = fopen(argv[a], "rb");
        if (!fp) { printf("cannot open %s\n", argv[a]); return 2; }
        std::vector<uint8_t> air;
        uint32_t lfsr = 0; uint8_t level = 0; int nb = 0; uint8_t cur = 0;
        int c;
        while ((c = fgetc(fp)) != EOF) {
            for (int i = 0; i < 8; i++) {
                int bit = (c >> i) & 1;
                if (!bit) level ^= 1;                                             // NRZI
                uint8_t x = (uint8_t)(level ^ ((lfsr >> 16) & 1) ^ ((lfsr >> 11) & 1));   // G3RUH scrambler
                lfsr = ((lfsr << 1) | x) & 0x1FFFF;
                cur = (uint8_t)((cur << 1) | x);
                if (++nb == 8) { air.push_back(cur); nb = 0; cur = 0; }
            }
        }
        fclose(fp);
        got.clear();
        AX25G3::Decoder d;
        d.feed(air.data(), air.size(), onFrame, nullptr);
        bool ok = got.size() == 1 && got[0] == "WB2OSZ-1>TEST:Foo?Bar?";
        printf("%s: %zu frame(s)%s%s, FX.25 blocks %u, recovered by FX.25 %u -> %s\n", argv[a], got.size(), got.empty() ? "" : " ", got.empty() ? "" : got[0].c_str(), d.fx25Blocks, d.fx25Fixed, ok ? "OK" : "FAIL");
        if (!ok) bad++;
    }
    printf(bad ? "FAILED\n" : "ALL OK\n");
    return bad ? 1 : 0;
}
