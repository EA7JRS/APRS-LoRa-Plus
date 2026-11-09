// Prueba en PC del modem AFSK 1200 (include/afsk1200.h). Ver tools/afsk/run.sh
// Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
//
//   test_afsk                      pruebas propias (ida y vuelta, ruido, inclinacion, error de reloj)
//   test_afsk decode  file.wav     decodifica un WAV mono de 16 bits (p. ej. de gen_packets de Dire Wolf) y cuenta las tramas
//   test_afsk encode  file.wav fs  escribe 100 tramas APRS con nuestro modulador (para decodificarlas con atest de Dire Wolf)
#include "afsk1200.h"
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
    if (AX25G3::frameToTnc2(f, len, t, sizeof(t))) ((Rx*)ctx)->got.push_back(t);
    else ((Rx*)ctx)->got.push_back("<no UI>");
}

// TNC2 -> muestras de audio (float, -1..1) a fs, con silencio delante y detras
static std::vector<float> modulate(const std::string& tnc2, uint32_t fs, float amp, size_t lead = 30, size_t tail = 3, double clockErr = 0, int fec = 0) {
    uint8_t frame[AX25G3::MAX_FRAME], bits[1024];
    size_t fl = AX25G3::tnc2ToFrame(tnc2.c_str(), frame, sizeof(frame));
    CHECK(fl > 0);
    size_t nb = AFSK::encodeBits(frame, fl, bits, sizeof(bits), lead, tail, fec);
    CHECK(nb > 0);
    // error de reloj: se genera a una frecuencia de muestreo distinta y se trata como si fuera fs
    uint32_t genFs = (uint32_t)lrint(fs * (1.0 + clockErr));
    AFSK::ToneGen g; g.start(bits, nb, genFs, amp);
    std::vector<int16_t> buf(AFSK::ToneGen::samplesFor(nb, genFs) + 16);
    size_t n = g.fill(buf.data(), buf.size());
    CHECK(g.done());
    std::vector<float> out(fs / 5, 0.0f);       // 200 ms de silencio
    for (size_t i = 0; i < n; i++) out.push_back(buf[i] / 32768.0f);
    out.resize(out.size() + fs / 5, 0.0f);
    return out;
}

// Desenfasis / preenfasis de primer orden (6 dB por octava) para simular la inclinacion de una radio FM
static void tilt(std::vector<float>& x, uint32_t fs, bool deemph) {
    const float a = expf(-2.0f * 3.14159265f * 300.0f / fs);    // polo/cero a unos 300 Hz
    float prev = 0, y = 0;
    for (auto& v : x) {
        if (deemph) { y = (1 - a) * v + a * y; v = y * 3.0f; }
        else        { float o = v - a * prev; prev = v; v = o * 0.5f; }
    }
}

static size_t decode(const std::vector<float>& x, uint32_t fs, Rx& rx) {
    AFSK::Demod d; CHECK(d.begin(fs));
    for (float v : x) d.feed(v, onFrame, &rx);
    return rx.got.size();
}

static bool readWav(const char* path, std::vector<float>& out, uint32_t& fs) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    uint8_t h[12]; if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return false; }
    uint16_t ch = 1, bitsPer = 16; fs = 0;
    for (;;) {
        uint8_t c[8]; if (fread(c, 1, 8, f) != 8) { fclose(f); return false; }
        uint32_t sz = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) {
            std::vector<uint8_t> b(sz); if (fread(b.data(), 1, sz, f) != sz) { fclose(f); return false; }
            ch = b[2] | b[3] << 8; fs = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24; bitsPer = b[14] | b[15] << 8;
        } else if (!memcmp(c, "data", 4)) {
            std::vector<uint8_t> b(sz); size_t got = fread(b.data(), 1, sz, f); fclose(f);
            if (bitsPer != 16) return false;
            for (size_t i = 0; i + 2 * ch <= got; i += 2 * ch) out.push_back((int16_t)(b[i] | b[i + 1] << 8) / 32768.0f);
            return fs != 0;
        } else fseek(f, sz, SEEK_CUR);
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
    "EA7JRS-7>APZLRP,WIDE1-1,WIDE2-1:!3723.45N/00559.12W>APRS LoRa+ T-TWR",
    "EA7JRS-9>APZLRP:=3723.45N/00559.12W[/A=000123 1200 baud AFSK",
    "EB7ABC>APRS,EA7URS*,WIDE2-1::EA7JRS-7 :Hola desde VHF{12",
    "N0CALL>APZLRP:>status ~~~~~~ \x7e\x7e\x7f bits 11111111 \xff\xff\xff",
};

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "decode")) {
        std::vector<float> x; uint32_t fs;
        if (!readWav(argv[2], x, fs)) { printf("no se puede leer %s\n", argv[2]); return 2; }
        Rx rx; size_t n = decode(x, fs, rx);
        printf("%zu\n", n);
        if (argc >= 4) for (auto& s : rx.got) printf("  %s\n", s.c_str());
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "encode")) {
        uint32_t fs = (uint32_t)atoi(argv[3]);
        const int fec = argc >= 5 ? atoi(argv[4]) : 0;      // 0 AX.25, 1..3 FX.25, 4 IL2P
        std::vector<float> all;
        for (int i = 0; i < 100; i++) {
            char t[200]; snprintf(t, sizeof(t), "EA7JRS-7>APZLRP,WIDE1-1:>trama %03d del modulador APRS LoRa+", i);
            auto x = modulate(t, fs, 0.5f, 20, 2, 0, fec);
            all.insert(all.end(), x.begin(), x.end());
        }
        writeWav(argv[2], all, fs);
        return 0;
    }

    // ---- bits: una bandera en NRZI empezando en marca: 0 1 1 1 1 1 1 0 -> el 0 cambia (espacio), seis 1 lo mantienen, el 0 vuelve a marca
    {
        uint8_t frame[AX25G3::MAX_FRAME], bits[256];
        size_t fl = AX25G3::tnc2ToFrame(MSGS[0], frame, sizeof(frame));
        size_t nb = AFSK::encodeBits(frame, fl, bits, sizeof(bits), 1, 1);
        CHECK(nb > 16);
        for (int i = 1; i < 7; i++) CHECK(AFSK::bitAt(bits, i) == AFSK::bitAt(bits, 0));   // 0 1 1 1 1 1 1 0: el 0 cambia, seis 1 no
        CHECK(AFSK::bitAt(bits, 7) != AFSK::bitAt(bits, 6));
        CHECK(AFSK::encodeBits(frame, fl, bits, 4, 1, 1) == 0);    // no cabe
    }

    const uint32_t rates[] = { 13200, 9600, 22050, 26400, 44100, 48000 };
    // ---- ida y vuelta limpia a varias frecuencias de muestreo
    for (uint32_t fs : rates) {
        for (const char* m : MSGS) {
            Rx rx; decode(modulate(m, fs, 0.5f), fs, rx);
            CHECK(rx.got.size() == 1);
            if (rx.got.size() == 1) CHECK(rx.got[0] == m);
        }
    }
    // ---- FX.25 (16 / 32 / 64) e IL2P: ida y vuelta, y con ruido el FEC rescata tramas que el AX.25 pierde
    for (int fec = 1; fec <= 4; fec++) for (const char* m : MSGS) {
        Rx rx; decode(modulate(m, 13200, 0.5f, 30, 3, 0, fec), 13200, rx);
        CHECK(rx.got.size() == 1);
        if (rx.got.size() == 1) CHECK(rx.got[0] == m);
    }
    {
        std::mt19937 r2(99); std::normal_distribution<float> gg(0.0f, 1.0f);
        int ok[5] = {0};
        for (int fec : { 0, 2, 4 }) for (int k = 0; k < 40; k++) {
            auto x = modulate(MSGS[k % 3], 13200, 0.3f, 30, 3, 0, fec);
            const float noise = (0.3f / sqrtf(2.0f)) / powf(10.0f, 3.0f / 20.0f);
            for (auto& v : x) v += noise * gg(r2);
            Rx rx; decode(x, 13200, rx);
            if (rx.got.size() >= 1 && rx.got[0] == MSGS[k % 3]) ok[fec]++;
        }
        printf("SNR 3 dB: AX.25 %d/40, FX.25(32) %d/40, IL2P %d/40\n", ok[0], ok[2], ok[4]);
        CHECK(ok[2] >= ok[0] && ok[4] >= ok[0]);
    }
    // ---- niveles muy distintos (CAG)
    for (float amp : { 0.01f, 0.1f, 0.9f }) { Rx rx; decode(modulate(MSGS[1], 13200, amp), 13200, rx); CHECK(rx.got.size() == 1); }
    // ---- inclinacion de +-6 dB por octava (radio con o sin desenfasis)
    for (bool de : { true, false }) { auto x = modulate(MSGS[0], 13200, 0.4f); tilt(x, 13200, de); Rx rx; decode(x, 13200, rx); CHECK(rx.got.size() == 1); }
    // ---- error de reloj del emisor de +-2 %
    for (double e : { -0.02, 0.02 }) { Rx rx; decode(modulate(MSGS[2], 13200, 0.5f, 30, 3, e), 13200, rx); CHECK(rx.got.size() == 1); }
    // ---- ruido blanco: tasa de acierto por SNR (en la banda de audio entera)
    std::mt19937 rng(1234);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    for (float snrDb : { 20.0f, 12.0f, 9.0f, 6.0f }) {
        int ok = 0, total = 40;
        for (int k = 0; k < total; k++) {
            auto x = modulate(MSGS[k % 3], 13200, 0.3f);
            const float sig = 0.3f / sqrtf(2.0f), noise = sig / powf(10.0f, snrDb / 20.0f);
            for (auto& v : x) v += noise * gauss(rng);
            Rx rx; decode(x, 13200, rx);
            if (rx.got.size() == 1 && rx.got[0] == MSGS[k % 3]) ok++;
            CHECK(rx.got.size() <= 1);
        }
        printf("SNR %4.1f dB: %d/%d\n", snrDb, ok, total);
        if (snrDb >= 12) CHECK(ok == total);
    }
    // ---- solo ruido: ninguna trama falsa
    {
        std::vector<float> x(13200 * 30);
        for (auto& v : x) v = 0.2f * gauss(rng);
        Rx rx; decode(x, 13200, rx);
        CHECK(rx.got.empty());
    }

    printf(fails ? "%d FALLOS\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
