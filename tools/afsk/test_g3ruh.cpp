// Prueba en PC del modem de banda base G3RUH 9600 (include/g3ruh_modem.h). Ver tools/afsk/README.md
// Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
//
//   test_g3ruh                      pruebas propias
//   test_g3ruh decode file.wav      cuenta las tramas de un WAV (p. ej. gen_packets -B 9600 de Dire Wolf)
//   test_g3ruh encode file.wav fs   100 tramas con nuestro modulador (para atest -B 9600)
#include "g3ruh_modem.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)

struct Rx { std::vector<std::string> got; };
static void onFrame(const uint8_t* f, size_t len, void* ctx) {
    char t[512];
    ((Rx*)ctx)->got.push_back(AX25G3::frameToTnc2(f, len, t, sizeof(t)) ? t : "<no UI>");
}

static std::vector<float> modulate(const std::string& tnc2, uint32_t fs, float amp, double clockErr = 0, size_t lead = 40) {
    uint8_t frame[AX25G3::MAX_FRAME], air[600];
    size_t fl = AX25G3::tnc2ToFrame(tnc2.c_str(), frame, sizeof(frame));
    CHECK(fl > 0);
    size_t nb = AX25G3::g3ruhEncode(frame, fl, air, sizeof(air), lead);
    CHECK(nb > 0);
    G3RUH::BaseTx tx;
    tx.start(air, nb, (uint32_t)lrint(fs * (1.0 + clockErr)), amp);
    std::vector<int16_t> buf(200000);
    size_t n = tx.fill(buf.data(), buf.size());
    CHECK(tx.done());
    std::vector<float> out(fs / 20, 0.0f);
    for (size_t i = 0; i < n; i++) out.push_back(buf[i] / 32768.0f);
    out.resize(out.size() + fs / 20, 0.0f);
    return out;
}

static size_t decode(const std::vector<float>& x, uint32_t fs, Rx& rx) {
    G3RUH::BaseRx d; CHECK(d.begin(fs));
    for (float v : x) d.feed(v, onFrame, &rx);
    return rx.got.size();
}

// radio: paso alto de primer orden (acoplo) y paso bajo de primer orden (filtro de la placa / FI)
static void radioPath(std::vector<float>& x, uint32_t fs, float hpHz, float lpHz) {
    const float a = expf(-2.0f * 3.14159265f * hpHz / fs), b = expf(-2.0f * 3.14159265f * lpHz / fs);
    float px = 0, hp = 0, lp = 0;
    for (auto& v : x) { hp = a * (hp + v - px); px = v; lp = (1 - b) * hp + b * lp; v = lp; }
}

static bool readWav(const char* path, std::vector<float>& out, uint32_t& fs) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    uint8_t h[12]; if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4)) { fclose(f); return false; }
    uint16_t ch = 1; fs = 0;
    for (;;) {
        uint8_t c[8]; if (fread(c, 1, 8, f) != 8) { fclose(f); return false; }
        uint32_t sz = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) { std::vector<uint8_t> b(sz); if (fread(b.data(), 1, sz, f) != sz) { fclose(f); return false; } ch = b[2] | b[3] << 8; fs = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24; }
        else if (!memcmp(c, "data", 4)) { std::vector<uint8_t> b(sz); size_t got = fread(b.data(), 1, sz, f); fclose(f);
            for (size_t i = 0; i + 2 * ch <= got; i += 2 * ch) out.push_back((int16_t)(b[i] | b[i + 1] << 8) / 32768.0f);
            return fs != 0; }
        else fseek(f, sz, SEEK_CUR);
    }
}

static void writeWav(const char* path, const std::vector<float>& x, uint32_t fs) {
    FILE* f = fopen(path, "wb");
    uint32_t data = (uint32_t)x.size() * 2, riff = 36 + data, br = fs * 2;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(riff); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(1); w32(fs); w32(br); w16(2); w16(16);
    fwrite("data", 1, 4, f); w32(data);
    for (float v : x) { int s = (int)lrintf(v * 32767.0f); if (s > 32767) s = 32767; if (s < -32768) s = -32768; w16((uint16_t)(int16_t)s); }
    fclose(f);
}

static const char* MSGS[] = {
    "EA7JRS-7>APZLRP,WIDE1-1:!3723.45N/00559.12W>T-TWR 9600 G3RUH",
    "EB7ABC>APRS,EA7URS*,WIDE2-1::EA7JRS-7 :Hola a 9600 baudios{7",
    "N0CALL>APZLRP:>status 11111111 \x7e\x7e \xff\xff\xff\xff",
};

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "decode")) {
        std::vector<float> x; uint32_t fs;
        if (!readWav(argv[2], x, fs)) { printf("no se puede leer %s\n", argv[2]); return 2; }
        Rx rx; printf("%zu\n", decode(x, fs, rx));
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "encode")) {
        uint32_t fs = (uint32_t)atoi(argv[3]);
        std::vector<float> all;
        for (int i = 0; i < 100; i++) {
            char t[200]; snprintf(t, sizeof(t), "EA7JRS-7>APZLRP,WIDE1-1:>trama %03d del modulador 9600 APRS LoRa+", i);
            auto x = modulate(t, fs, 0.7f);
            all.insert(all.end(), x.begin(), x.end());
        }
        writeWav(argv[2], all, fs);
        return 0;
    }
    // ida y vuelta limpia
    for (uint32_t fs : { 48000u, 38400u, 44100u, 96000u }) for (const char* m : MSGS) {
        Rx rx; decode(modulate(m, fs, 0.6f), fs, rx);
        CHECK(rx.got.size() == 1);
        if (rx.got.size() == 1) CHECK(rx.got[0] == m);
    }
    // polaridad invertida, niveles, camino de la radio (acoplo de 20 Hz y paso bajo de 7 kHz), error de reloj
    { auto x = modulate(MSGS[0], 48000, 0.6f); for (auto& v : x) v = -v; Rx rx; decode(x, 48000, rx); CHECK(rx.got.size() == 1); }
    for (float a : { 0.02f, 0.9f }) { Rx rx; decode(modulate(MSGS[1], 48000, a), 48000, rx); CHECK(rx.got.size() == 1); }
    { auto x = modulate(MSGS[2], 48000, 0.6f); radioPath(x, 48000, 20.0f, 7000.0f); Rx rx; decode(x, 48000, rx); CHECK(rx.got.size() == 1); }
    for (double e : { -0.01, 0.01 }) { Rx rx; decode(modulate(MSGS[0], 48000, 0.6f, e), 48000, rx); CHECK(rx.got.size() == 1); }
    // ruido
    std::mt19937 rng(77);
    std::normal_distribution<float> g(0.0f, 1.0f);
    for (float snr : { 20.0f, 14.0f, 10.0f, 8.0f }) {
        int ok = 0, total = 40;
        for (int k = 0; k < total; k++) {
            auto x = modulate(MSGS[k % 3], 48000, 0.5f);
            radioPath(x, 48000, 20.0f, 7000.0f);
            const float noise = 0.5f * 0.6f / powf(10.0f, snr / 20.0f);
            for (auto& v : x) v += noise * g(rng);
            Rx rx; decode(x, 48000, rx);
            if (rx.got.size() == 1 && rx.got[0] == MSGS[k % 3]) ok++;
        }
        printf("SNR %4.1f dB: %d/%d\n", snr, ok, total);
        if (snr >= 14) CHECK(ok == total);
    }
    { std::vector<float> x(48000 * 20); for (auto& v : x) v = 0.3f * g(rng); Rx rx; decode(x, 48000, rx); CHECK(rx.got.empty()); }
    printf(fails ? "%d FALLOS\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
