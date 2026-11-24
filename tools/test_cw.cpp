// Prueba en PC del codificador/decodificador CW (include/cw_morse.h):
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tools/test_cw.cpp src/cw_morse.cpp -o /tmp/test_cw && /tmp/test_cw
// Copyright (c) 2026 Manuel - EA7JRS
#include "cw_morse.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdint>
#include <cstdlib>

using namespace CWMorse;
static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

// Reproduce un texto en CW a wpm, con peso de raya (dashUnits, 3.0 = perfecto) y jitter (±ms), y lo pasa por el decodificador.
// Devuelve el texto decodificado. letterGapU/wordGapU permiten simular Farnsworth (espacios mas largos).
static std::string playAndDecode(const char* text, uint8_t wpm, float dashUnits, int jitter,
                                 float letterGapU = 3.0f, float wordGapU = 7.0f, bool adaptive = true) {
    Decoder dec;
    dec.begin(wpm, adaptive);
    const uint32_t unit = dotMs(wpm);
    std::string out;
    uint32_t t = 0;
    auto run = [&](uint32_t until) {                 // avanza el reloj 1 ms y recoge caracteres
        while (t < until) { t++; char c = dec.tick(t); if (c) out += c; }
    };
    auto jit = [&](uint32_t base) -> uint32_t {
        if (jitter <= 0) return base;
        int d = (int)(rand() % (2 * jitter + 1)) - jitter;
        long v = (long)base + d; return v < 1 ? 1 : (uint32_t)v;
    };
    bool firstWord = true;
    for (const char* p = text; *p; ++p) {
        if (*p == ' ') { run(t + (uint32_t)(wordGapU * unit)); firstWord = true; continue; }
        if (!firstWord) run(t + (uint32_t)(letterGapU * unit));
        firstWord = false;
        const char* pat = pattern(*p);
        if (!pat) continue;
        for (const char* e = pat; *e; ++e) {
            uint32_t dur = (*e == '-') ? jit((uint32_t)(dashUnits * unit)) : jit(unit);
            dec.keyDown(t);
            run(t + dur);
            dec.keyUp(t);
            if (e[1]) run(t + jit(unit));            // hueco entre elementos (1 unidad)
        }
    }
    run(t + 8 * unit);                               // silencio final para cerrar letra y palabra
    while (!out.empty() && out.back() == ' ') out.pop_back();   // el ultimo hueco emite un espacio: se descarta
    return out;
}

int main() {
    // --- codificador / tablas ---
    CHECK(strcmp(pattern('A'), ".-") == 0, "pattern A");
    CHECK(strcmp(pattern('5'), ".....") == 0, "pattern 5");
    CHECK(charForPattern(".-") == 'A', "charForPattern A");
    CHECK(charForPattern("-...") == 'B', "charForPattern B");
    CHECK(pattern(' ') == nullptr, "space has no pattern");
    CHECK(dotMs(20) == 60, "dotMs 20wpm = 60ms");

    // --- decodificador, timing perfecto a varias velocidades ---
    for (uint8_t w : {12, 20, 30}) {
        std::string r = playAndDecode("CQ DE EA7JRS", w, 3.0f, 0);
        char buf[64]; snprintf(buf, sizeof(buf), "decode perfect %u wpm (got '%s')", w, r.c_str());
        CHECK(r == "CQ DE EA7JRS", buf);
    }

    // --- fist pesado: rayas largas (3.6 unidades) y jitter; la division adaptativa debe seguir acertando ---
    {
        std::string r = playAndDecode("PARIS CODEX", 18, 3.6f, 4);
        char buf[96]; snprintf(buf, sizeof(buf), "decode heavy dashes + jitter (got '%s')", r.c_str());
        CHECK(r == "PARIS CODEX", buf);
    }
    // --- fist ligero: rayas cortas (2.5 unidades) ---
    {
        std::string r = playAndDecode("TEST DE K", 15, 2.5f, 3);
        char buf[96]; snprintf(buf, sizeof(buf), "decode light dashes (got '%s')", r.c_str());
        CHECK(r == "TEST DE K", buf);
    }
    // --- Farnsworth: caracteres rapidos, espacios largos ---
    {
        std::string r = playAndDecode("HELLO", 25, 3.0f, 0, 5.0f, 11.0f);
        char buf[96]; snprintf(buf, sizeof(buf), "decode Farnsworth spacing (got '%s')", r.c_str());
        CHECK(r == "HELLO", buf);
    }
    // --- el decodificador sigue una velocidad distinta de la inicial (empieza a 10, señal a 22 wpm) ---
    {
        Decoder dec; dec.begin(16, true);               // arranca a 16 wpm; la señal va a 24 (mismatch realista de 1,5x)
        const uint32_t unit = dotMs(24);
        uint32_t t = 0; std::string out;
        auto run = [&](uint32_t until){ while (t < until){ t++; char c = dec.tick(t); if (c) out += c; } };
        const char* text = "CQ CQ TEST PARIS";          // palabras con puntos y rayas para converger pronto
        bool fw = true;
        for (const char* p = text; *p; ++p) {
            if (*p == ' ') { run(t + 7 * unit); fw = true; continue; }
            if (!fw) run(t + 3 * unit);
            fw = false;
            for (const char* e = pattern(*p); *e; ++e) {
                uint32_t dur = (*e == '-') ? 3 * unit : unit;
                dec.keyDown(t); run(t + dur); dec.keyUp(t);
                if (e[1]) run(t + unit);
            }
        }
        run(t + 8 * unit);
        while (!out.empty() && out.back() == ' ') out.pop_back();
        bool near = dec.wpm() >= 20 && dec.wpm() <= 28;
        char buf[128]; snprintf(buf, sizeof(buf), "adaptive speed tracking 16->24 wpm (got %u wpm, '%s')", dec.wpm(), out.c_str());
        CHECK(near, buf);
        bool endsParis = out.size() >= 5 && out.compare(out.size() - 5, 5, "PARIS") == 0;
        char buf2[128]; snprintf(buf2, sizeof(buf2), "adaptive run ends in PARIS after converging (got '%s')", out.c_str());
        CHECK(endsParis, buf2);
    }
    // --- modo no adaptativo: comportamiento clasico (umbral fijo 2 unidades) con timing perfecto ---
    {
        std::string r = playAndDecode("ABC", 20, 3.0f, 0, 3.0f, 7.0f, false);
        char buf[64]; snprintf(buf, sizeof(buf), "non-adaptive perfect (got '%s')", r.c_str());
        CHECK(r == "ABC", buf);
    }

    if (fails == 0) printf("All CW tests passed\n");
    else printf("%d CW test(s) FAILED\n", fails);
    return fails ? 1 : 0;
}
