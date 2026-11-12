// Prueba en PC del audio del modo FM (include/fm_dsp.h): detector de ruido, silenciador automatico y reduccion de ruido
// Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tools/fm/test_fm_dsp.cpp src/fm_dsp.cpp -o /tmp/test_fm_dsp && /tmp/test_fm_dsp
#include "fm_dsp.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace FmDsp;
static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const float FS = 13200.0f;
static std::mt19937 rng(7);

// "voz": f0 variable 110..180 Hz, armonicos con dos formantes (500 / 1500 Hz), silabas a 4 Hz con pausas
static std::vector<float> voice(float seconds, float amp) {
    std::vector<float> v((size_t)(seconds * FS));
    double ph = 0;
    for (size_t i = 0; i < v.size(); i++) {
        const double t = i / FS;
        const double f0 = 140 + 35 * sin(2 * M_PI * 0.7 * t);
        ph += 2 * M_PI * f0 / FS;
        double s = 0;
        for (int h = 1; h * f0 < 3200; h++) {
            const double f = h * f0;
            const double w = exp(-pow((f - 500) / 250, 2)) + 0.5 * exp(-pow((f - 1500) / 350, 2)) + 0.05;
            s += w * sin(h * ph) / sqrt((double)h);
        }
        const double env = std::max(0.0, sin(2 * M_PI * 2.0 * t));            // silabas con pausas
        v[i] = (float)(amp * env * s * 0.35);
    }
    return v;
}

// soplido de FM: el ruido del discriminador crece con la frecuencia (ruido blanco derivado), el desenfasis de 750 us (212 Hz)
// lo deja casi plano y el paso bajo del modulo lo corta en 3 kHz
static std::vector<float> hiss(float seconds, float amp) {
    std::normal_distribution<float> g(0, 1);
    std::vector<float> v((size_t)(seconds * FS));
    float last = 0, a = 0, b = 0, c = 0;
    const float kd = 1.0f - expf(-2.0f * (float)M_PI * 212.0f / FS);
    const float kl = 1.0f - expf(-2.0f * (float)M_PI * 3000.0f / FS);
    for (auto& x : v) { const float w = g(rng); const float d = w - last; last = w; a += (d * 8.0f - a) * kd; b += (a - b) * kl; c += (b - c) * kl; x = amp * c; }
    return v;
}

static double energy(const std::vector<float>& v, size_t from = 0) { double e = 0; for (size_t i = from; i < v.size(); i++) e += (double)v[i] * v[i]; return e; }

// ganancia en regimen permanente de VoiceFilter a una frecuencia (amplitud de salida / de entrada)
static float voiceGain(float hz) {
    VoiceFilter f; f.begin(FS);
    float peak = 0;
    for (int i = 0; i < (int)FS; i++) {
        const float y = f.process(0.5f * sinf(2.0f * (float)M_PI * hz * i / FS));
        if (i > FS / 2) peak = std::max(peak, fabsf(y));
    }
    return peak / 0.5f;
}

int main() {
    // banda de voz del altavoz: 1 kHz pasa, 60 Hz (zumbido), 100 Hz (CTCSS) y 5 kHz (soplido) se atenuan; limitador acotado
    {
        CHECK(fabsf(voiceGain(1000) - 1.0f) < 0.1f);
        CHECK(voiceGain(600) > 0.85f && voiceGain(2400) > 0.85f);
        CHECK(voiceGain(60) < 0.02f);
        CHECK(voiceGain(100) < 0.05f);
        CHECK(voiceGain(5500) < 0.08f);
        VoiceFilter f; f.begin(FS);
        float dc = 0;
        for (int i = 0; i < (int)FS; i++) dc = f.process(0.3f);          // continua: desaparece
        CHECK(fabsf(dc) < 1e-3f);
        CHECK(VoiceFilter::limit(0.3f) == 0.3f);
        CHECK(VoiceFilter::limit(5.0f) <= 1.0f && VoiceFilter::limit(5.0f) > 0.95f);
        CHECK(VoiceFilter::limit(-5.0f) >= -1.0f);
        CHECK(VoiceFilter::limit(0.8f) > 0.7f && VoiceFilter::limit(0.8f) < 0.8f);
    }
    // detector: el soplido parece ruido, la voz no, el silencio tampoco
    {
        NoiseDetector d; d.begin(FS);
        int noisy = 0, blocks = 0;
        for (float x : hiss(2, 0.3f)) if (d.feed(x)) { blocks++; if (d.noise()) noisy++; }
        printf("soplido: hf %.2f, %d/%d bloques como ruido\n", d.hfRatio(), noisy, blocks);
        CHECK(noisy > blocks * 8 / 10);
        NoiseDetector dv; dv.begin(FS);
        int vn = 0, vb = 0;
        for (float x : voice(3, 0.5f)) if (dv.feed(x)) { vb++; if (dv.noise()) vn++; }
        printf("voz: %d/%d bloques como ruido (hf %.2f)\n", vn, vb, dv.hfRatio());
        CHECK(vn < vb / 20);
        NoiseDetector dq; dq.begin(FS);
        for (int i = 0; i < 13200; i++) dq.feed(0.001f * (float)((i % 7) - 3) / 3.0f);
        CHECK(dq.quiet() && !dq.noise());
        // canal sin portadora (soplido) y luego voz con el ruido reducido 20 dB por la portadora: voz, tambien en las pausas
        NoiseDetector dm; dm.begin(FS);
        for (float x : hiss(1, 0.3f)) dm.feed(x);
        auto v = voice(3, 0.5f); auto h = hiss(3, 0.3f * 0.1f);
        int mn = 0, mb = 0;
        for (size_t i = 0; i < v.size(); i++) if (dm.feed(v[i] + h[i])) { mb++; if (dm.noise()) mn++; }
        printf("voz + ruido a 20 dB: %d/%d bloques como ruido\n", mn, mb);
        CHECK(mn < mb / 6);
    }

    // silenciador por software: abre con voz, cierra con soplido; con RSSI sigue el suelo de ruido
    {
        NoiseDetector d; d.begin(FS); AutoSquelch s;
        int openNoise = 0, blocks = 0;
        for (float x : hiss(3, 0.3f)) if (d.feed(x)) { blocks++; if (s.softUpdate(d, -120, false)) openNoise++; }
        CHECK(openNoise <= 2);
        int openVoice = 0, vb = 0;
        for (float x : voice(3, 0.5f)) if (d.feed(x)) { vb++; if (s.softUpdate(d, -120, false)) openVoice++; }
        printf("silenciador: abierto %d/%d con soplido, %d/%d con voz\n", openNoise, blocks, openVoice, vb);
        CHECK(openVoice > vb * 9 / 10);                     // tambien en las pausas entre silabas (portadora)
        AutoSquelch r; NoiseDetector dr; dr.begin(FS);
        int opened = 0;
        for (float x : voice(2, 0.5f)) if (dr.feed(x)) { if (r.softUpdate(dr, -121, true)) opened++; }     // voz pero RSSI en el suelo
        CHECK(opened == 0 && fabsf(r.rssiFloor() + 121) < 2.5f);      // voz pero RSSI en el suelo: no abre
        opened = 0;
        for (float x : voice(2, 0.5f)) if (dr.feed(x)) { if (r.softUpdate(dr, -100, true)) opened++; }
        CHECK(opened > 10);
    }

    // silenciador por nivel (Rev2.0)
    {
        AutoSquelch s; int level = 1;
        uint32_t t = 0;
        for (; t < 1500; t += 20) { int n = s.levelUpdate(t, true, true, level); if (n >= 0) level = n; }
        CHECK(level == 2);
        for (; t < 6000; t += 20) { int n = s.levelUpdate(t, true, true, level); if (n >= 0) level = n; }
        CHECK(level == 3);
        for (; t < 70000; t += 20) { int n = s.levelUpdate(t, false, false, level); if (n >= 0) level = n; }
        CHECK(level == 2);                                  // un minuto libre: prueba a bajar
        for (; t < 73000; t += 20) { int n = s.levelUpdate(t, true, true, level); if (n >= 0) level = n; }
        CHECK(level == 3);                                  // se abrio con ruido: vuelve a subir
        for (; t < 200000; t += 20) { int n = s.levelUpdate(t, false, false, level); if (n >= 0) level = n; }
        CHECK(level == 3);                                  // y no vuelve a probar en 5 minutos
        for (; t < 500000; t += 20) { int n = s.levelUpdate(t, false, false, level); if (n >= 0) level = n; }
        CHECK(level < 3);
        for (; t < 520000; t += 20) { int n = s.levelUpdate(t, true, false, level); if (n >= 0) level = n; }   // senal de verdad: no cambia
        const int keep = level;
        for (; t < 530000; t += 20) { int n = s.levelUpdate(t, true, false, level); if (n >= 0) level = n; }
        CHECK(level == keep && level >= 1);
    }

    // FFT: ida y vuelta
    {
        float re[256], im[256], r0[256];
        for (int i = 0; i < 256; i++) { re[i] = r0[i] = sinf(i * 0.3f) + 0.2f * cosf(i * 1.7f); im[i] = 0; }
        fft(re, im, 256, false); fft(re, im, 256, true);
        float err = 0; for (int i = 0; i < 256; i++) err = fmaxf(err, fabsf(re[i] - r0[i]));
        CHECK(err < 1e-4f);
        for (int i = 0; i < 256; i++) { re[i] = cosf(2 * (float)M_PI * 10 * i / 256); im[i] = 0; }
        fft(re, im, 256, false);
        CHECK(fabsf(re[10] - 128) < 0.01f && fabsf(re[11]) < 0.01f);
    }

    // reduccion de ruido: reconstruccion sin ruido, atenuacion del soplido, mejora de la relacion senal / ruido
    {
        NoiseReducer nr; nr.begin(); nr.setStrength(0.0f);
        auto v = voice(2, 0.5f);
        std::vector<float> out(v.size());
        for (size_t i = 0; i < v.size(); i++) out[i] = nr.process(v[i]);
        // con fuerza 0 la ganancia aun quita algo; comprueba el retardo y que la voz pasa casi entera
        double num = 0, den = 0;
        for (size_t i = 4000; i < v.size(); i++) { const double d = out[i] - v[i - NoiseReducer::N]; num += d * d; den += (double)v[i - NoiseReducer::N] * v[i - NoiseReducer::N]; }
        printf("voz sola, fuerza 0: error %.1f dB\n", 10 * log10(num / den));
        CHECK(10 * log10(num / den) < -6);

        for (float strength : { 0.5f, 1.0f }) {
            NoiseReducer a; a.begin(); a.setStrength(strength);
            auto h = hiss(4, 0.15f);
            std::vector<float> o(h.size());
            for (size_t i = 0; i < h.size(); i++) o[i] = a.process(h[i]);
            const double att = 10 * log10(energy(o, 13200) / energy(h, 13200));
            printf("soplido solo, fuerza %.1f: %.1f dB\n", strength, att);
            CHECK(att < -8);

            NoiseReducer b; b.begin(); b.setStrength(strength);
            auto vv = voice(6, 0.5f); auto hh = hiss(6, 0.05f);
            std::vector<float> in(vv.size()), oo(vv.size());
            for (size_t i = 0; i < vv.size(); i++) { in[i] = vv[i] + hh[i]; oo[i] = b.process(in[i]); }
            double sIn = 0, nIn = 0, sOut = 0, nOut = 0;
            for (size_t i = 26400; i < vv.size(); i++) {
                const float ref = vv[i - NoiseReducer::N];
                sIn += (double)vv[i] * vv[i]; nIn += (double)hh[i] * hh[i];
                sOut += (double)ref * ref; nOut += (double)(oo[i] - ref) * (oo[i] - ref);
            }
            const double snrIn = 10 * log10(sIn / nIn), snrOut = 10 * log10(sOut / nOut);
            printf("voz + soplido, fuerza %.1f: SNR %.1f -> %.1f dB\n", strength, snrIn, snrOut);
            CHECK(snrOut > snrIn + 3);
        }
    }
    // CW A1A recibido en FM: portadora a 20 ppm (punto de 60 ms) con "PARIS" repetido; soplido sin portadora, casi silencio con ella
    {
        const float dot = 0.060f;
        const char* morse = ".--. .- .-. .. ...";               // PARIS
        std::vector<int> keyed;                                 // 1 ms por elemento
        auto add = [&](int on, float units) { for (int k = 0; k < (int)(units * dot * 1000); k++) keyed.push_back(on); };
        add(0, 10);
        for (int rep = 0; rep < 3; rep++) {
            for (const char* p = morse; *p; p++) {
                if (*p == ' ') { add(0, 2); continue; }        // con el hueco de elemento: 3 unidades entre letras
                add(1, *p == '-' ? 3 : 1); add(0, 1);
            }
            add(0, 6);
        }
        add(0, 10);
        auto h = hiss((float)keyed.size() / 1000.0f + 0.1f, 0.3f);
        std::normal_distribution<float> g(0, 1);
        QuietingDetector q; q.begin(FS);
        int lastOn = 0, edges = 0, expectEdges = 0, wrongMs = 0;
        for (size_t k = 1; k < keyed.size(); k++) if (keyed[k] != keyed[k - 1]) expectEdges++;
        const int perMs = (int)(FS / 1000);
        for (size_t k = 0; k < keyed.size(); k++) {
            for (int s = 0; s < perMs; s++) {
                const size_t i = k * perMs + s;
                const float x = keyed[k] ? 0.01f * g(rng) : h[i];          // portadora: el ruido baja unos 30 dB
                q.feed(x);
            }
            const int on = q.on() ? 1 : 0;
            if (on != lastOn) { edges++; lastOn = on; }
            if (k > 600 && on != keyed[k > 6 ? k - 6 : 0]) wrongMs++;          // con 6 ms de retardo
        }
        printf("CW por silenciamiento: %d flancos (esperados %d), %d ms mal de %zu\n", edges, expectEdges, wrongMs, keyed.size());
        CHECK(edges == expectEdges);
        CHECK(wrongMs < (int)keyed.size() / 30);                      // ~3 ms de error por flanco
    }
    printf(fails ? "%d FALLOS de %d\n" : "OK (%d comprobaciones)\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
