/* Copyright (C) 2026 Manuel - EA7JRS
 * PC tests for FX.25 (src/fx25.cpp).
 *   g++ -O2 -Iinclude tools/fx25/test_fx25.cpp src/fx25.cpp -o /tmp/test_fx25 && /tmp/test_fx25 [oracle.txt]
 * oracle.txt (optional): output of a small program linked with Dire Wolf's fx25_init.c/fx25_encode.c: per tag, 4 lines
 *   "tag nroots dataBytes tagHex" / data hex / check hex. Every check word must be identical to ours.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fx25.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int hexv(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

int main(int argc, char** argv) {
    // 1. tags: distinct, far apart
    int minDist = 64;
    for (int a = FX25::TAG_MIN; a <= FX25::TAG_MAX; a++)
        for (int b = a + 1; b <= FX25::TAG_MAX; b++) {
            int d = __builtin_popcountll(FX25::tagValue(a) ^ FX25::tagValue(b));
            if (d < minDist) minDist = d;
        }
    printf("minimum distance between tags: %d bits\n", minDist);
    CHECK(minDist > 2 * FX25::TAG_MAX_ERRORS, "tags too close");
    for (int t = FX25::TAG_MIN; t <= FX25::TAG_MAX; t++) {
        CHECK(FX25::matchTag(FX25::tagValue(t)) == t, "tag %d exact", t);
        uint64_t v = FX25::tagValue(t) ^ 0x0000100200800400ULL;      // 3 bit errors
        CHECK(FX25::matchTag(v) == t, "tag %d with errors", t);
    }
    CHECK(FX25::matchTag(0x0123456789ABCDEFULL) == 0, "random value must not match");
    CHECK(FX25::pickTag(16, 20) == 4 && FX25::pickTag(16, 33) == 3 && FX25::pickTag(16, 65) == 2 && FX25::pickTag(16, 129) == 1 && FX25::pickTag(16, 240) == 0, "pickTag 16");
    CHECK(FX25::pickTag(32, 33) == 7 && FX25::pickTag(32, 128) == 6 && FX25::pickTag(32, 224) == 0, "pickTag 32");
    CHECK(FX25::pickTag(64, 64) == 11 && FX25::pickTag(64, 128) == 10 && FX25::pickTag(64, 191) == 9, "pickTag 64");

    // 2. check words against Dire Wolf
    if (argc > 1) {
        FILE* f = fopen(argv[1], "r");
        if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
        char l1[256], l2[1024], l3[256];
        int n = 0;
        while (fgets(l1, sizeof l1, f) && fgets(l2, sizeof l2, f) && fgets(l3, sizeof l3, f)) {
            int tag, nroots, k; unsigned long long tv;
            sscanf(l1, "%d %d %d %llx", &tag, &nroots, &k, &tv);
            uint8_t data[255] = {0}, check[64], ref[64];
            for (int i = 0; i < k; i++) data[i] = (uint8_t)(hexv(l2[2 * i]) * 16 + hexv(l2[2 * i + 1]));
            for (int i = 0; i < nroots; i++) ref[i] = (uint8_t)(hexv(l3[2 * i]) * 16 + hexv(l3[2 * i + 1]));
            FX25::rsEncode(nroots, data, check);
            CHECK(FX25::tagValue(tag) == tv, "tag value %d", tag);
            CHECK(FX25::tagData(tag) == k && FX25::tagCheck(tag) == nroots, "tag %d size", tag);
            CHECK(memcmp(check, ref, (size_t)nroots) == 0, "tag %d: check bytes differ from Dire Wolf", tag);
            n++;
        }
        fclose(f);
        printf("check words compared with Dire Wolf: %d tags\n", n);
        CHECK(n == 11, "expected 11 tags");
    }

    // 3. correct up to nroots/2 errors; detect (almost always) more
    srand(7);
    int okAll = 0, tried = 0, wrongFix = 0, detected = 0;
    for (int nroots = 16; nroots <= 64; nroots *= 2) {
        for (int trial = 0; trial < 400; trial++) {
            int k = 255 - nroots, kr = 20 + rand() % (k - 20);
            uint8_t blk[255] = {0}, ref[255];
            for (int i = 0; i < kr; i++) blk[i] = (uint8_t)rand();
            FX25::rsEncode(nroots, blk, blk + k);
            memcpy(ref, blk, 255);
            int ne = rand() % (nroots / 2 + 1);                 // 0 .. t errors, only in sent bytes
            for (int e = 0; e < ne; e++) {
                int p;
                do { p = rand() % 255; } while (p >= kr && p < k);
                blk[p] ^= (uint8_t)(1 + rand() % 255);
            }
            int diff = 0; for (int i = 0; i < 255; i++) diff += blk[i] != ref[i];
            int r = FX25::rsDecode(nroots, blk, (size_t)kr);
            tried++;
            if (r == diff && memcmp(blk, ref, 255) == 0) okAll++;
            else CHECK(0, "nroots %d, %d errors: decode returned %d", nroots, diff, r);
        }
        // too many errors: must almost never return a wrong "fix"
        for (int trial = 0; trial < 400; trial++) {
            int k = 255 - nroots, kr = k;
            uint8_t blk[255], ref[255];
            for (int i = 0; i < k; i++) blk[i] = (uint8_t)rand();
            FX25::rsEncode(nroots, blk, blk + k);
            memcpy(ref, blk, 255);
            for (int e = 0; e < nroots / 2 + 1 + rand() % 8; e++) blk[rand() % 255] ^= (uint8_t)(1 + rand() % 255);
            int r = FX25::rsDecode(nroots, blk, (size_t)kr);
            if (r < 0) detected++;
            else if (memcmp(blk, ref, 255) != 0) wrongFix++;
        }
    }
    printf("correction: %d/%d ok; too many errors: %d detected, %d wrong fixes\n", okAll, tried, detected, wrongFix);

    // 4. stuffing
    uint8_t fr[40], out[FX25::MAX_DATA];
    for (int i = 0; i < 40; i++) fr[i] = 0xFF;                  // lots of stuffing
    size_t used = FX25::stuffFrame(fr, 40, out);
    CHECK(used > 42 && used < 60, "stuffFrame used %zu", used);
    CHECK(out[0] == 0x7E, "opening flag");
    uint8_t big[300] = {0};
    CHECK(FX25::stuffFrame(big, 240, out) == 0, "too big must fail");

    printf(fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
