// Prueba en PC del demodulador POCSAG de banda base de la T-TWR (include/pocsag_modem.h) con el decodificador de include/pocsag.h
// Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tools/afsk/test_pocsag_audio.cpp -o /tmp/tpa && /tmp/tpa
#include "pocsag.h"
#include "pocsag_modem.h"
#include <cmath>
#include <random>
#include <string>
#include <vector>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)

// flujo POCSAG: preambulo, un lote con la direccion y el mensaje alfanumerico, relleno
static std::vector<uint8_t> stream(uint32_t ric, const std::string& text) {
    std::vector<uint8_t> bits;
    auto put = [&](uint32_t w, int n) { for (int i = n - 1; i >= 0; i--) bits.push_back((w >> i) & 1); };
    for (int i = 0; i < 576; i++) bits.push_back(i & 1 ? 0 : 1);
    std::vector<uint8_t> payload;
    for (char c : text) for (int j = 0; j < 7; j++) payload.push_back((c >> j) & 1);
    payload.push_back(0); for (int j = 0; j < 6; j++) payload.push_back(0);          // fin (ETX parcial)
    std::vector<uint32_t> words(16, Pocsag::IDLE_WORD);
    int pos = (ric & 7) * 2;
    words[pos++] = Pocsag::encodeWord(((ric >> 3) << 2 | 3) & 0xFFFFF);
    std::vector<uint32_t> more;
    for (size_t b = 0; b < payload.size(); b += 20) {
        uint32_t d = 0;
        for (int k = 0; k < 20; k++) d = (d << 1) | (b + k < payload.size() ? payload[b + k] : 0);
        if (pos < 16) words[pos++] = Pocsag::encodeWord(0x100000UL | d); else more.push_back(Pocsag::encodeWord(0x100000UL | d));
    }
    put(Pocsag::SYNC_WORD, 32); for (auto w : words) put(w, 32);
    if (!more.empty()) {
        std::vector<uint32_t> w2(16, Pocsag::IDLE_WORD);
        for (size_t i = 0; i < more.size() && i < 16; i++) w2[i] = more[i];
        put(Pocsag::SYNC_WORD, 32); for (auto w : w2) put(w, 32);
    }
    return bits;
}

// banda base como la sacaria el SA868: NRZ (1 = -desviacion, como POCSAG), paso bajo de 3 kHz, acoplo de alterna, ruido
static std::vector<float> audio(const std::vector<uint8_t>& bits, int baud, float fs, float hpfHz, float snrDb, bool invert, double clock, unsigned seed) {
    std::mt19937 rng(seed); std::normal_distribution<float> g(0, 1);
    std::vector<float> out;
    for (int i = 0; i < (int)(0.2f * fs); i++) out.push_back(0.3f * g(rng));        // ruido antes
    const double spb = fs / baud * (1.0 + clock);
    float lp1 = 0, lp2 = 0, hpPrevIn = 0, hpOut = 0;
    const float kl = 1.0f - expf(-2.0f * (float)M_PI * 3000.0f / fs);
    const float rc = hpfHz > 0 ? 1.0f / (2.0f * (float)M_PI * hpfHz) : 1e9f, dt = 1.0f / fs, ah = rc / (rc + dt);
    const float noise = 0.4f / powf(10.0f, snrDb / 20.0f);
    const size_t total = (size_t)(bits.size() * spb);
    for (size_t n = 0; n < total; n++) {
        const size_t b = (size_t)(n / spb);
        float s = (bits[b] ^ (invert ? 1 : 0)) ? -0.4f : 0.4f;
        lp1 += (s - lp1) * kl; lp2 += (lp1 - lp2) * kl;
        hpOut = ah * (hpOut + lp2 - hpPrevIn); hpPrevIn = lp2;
        out.push_back(hpOut + noise * g(rng));
    }
    for (int i = 0; i < (int)(0.2f * fs); i++) out.push_back(0.3f * g(rng));
    return out;
}

struct Got { std::vector<std::pair<uint32_t, std::string>> msgs; };
static void onMsg(const Pocsag::Message& m, void* c) {
    char buf[256]; Pocsag::decodeAlpha(m, buf, sizeof(buf));
    std::string t(buf);
    ((Got*)c)->msgs.push_back({ m.ric, t });
}
struct Sink { Pocsag::Decoder dec; Got got; };
static void onByte(uint8_t b, void* c) { Sink* s = (Sink*)c; s->dec.feed(&b, 1, onMsg, &s->got); }

static bool decodes(int baud, float hpfHz, float compHz, float snr, bool invert, double clock, unsigned seed, const std::string& text = "DAPNET en la T-TWR 73") {
    const float fs = 48000;
    auto x = audio(stream(1234567, text), baud, fs, hpfHz, snr, invert, clock, seed);
    PocsagModem::BaseRx rx; rx.begin(fs, baud, compHz);
    Sink s; s.dec.reset();
    for (float v : x) rx.feed(v, onByte, &s);
    s.dec.flush(onMsg, &s.got);
    for (auto& m : s.got.msgs) if (m.first == 1234567 && m.second.find(text) == 0) return true;
    return false;
}

int main() {
    for (int baud : { 512, 1200, 2400 }) {
        CHECK(decodes(baud, 320, 320, 30, false, 0, 1));
        CHECK(decodes(baud, 320, 320, 30, true, 0, 2));                 // polaridad invertida
        CHECK(decodes(baud, 0, 0, 30, false, 0, 3));                    // sin acoplo de alterna
    }
    CHECK(decodes(1200, 320, 320, 30, false, 0.005, 4) && decodes(1200, 320, 320, 30, false, -0.005, 5));   // +-0,5 % de reloj
    CHECK(decodes(1200, 250, 320, 30, false, 0, 6) && decodes(1200, 400, 320, 30, false, 0, 7));          // corte distinto del supuesto
    CHECK(decodes(1200, 320, 320, 30, false, 0, 8, std::string(40, 'x')));                               // dos lotes
    int ok12 = 0, okNo = 0;
    for (unsigned s = 0; s < 20; s++) { if (decodes(1200, 320, 320, 12, false, 0, 100 + s)) ok12++; if (decodes(1200, 320, 0, 30, false, 0, 200 + s)) okNo++; }
    printf("1200 baudios, SNR 12 dB: %d/20; acoplo de 320 Hz sin compensar: %d/20\n", ok12, okNo);
    CHECK(ok12 >= 17);
    printf(fails ? "%d FALLOS de %d\n" : "OK (%d comprobaciones)\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
