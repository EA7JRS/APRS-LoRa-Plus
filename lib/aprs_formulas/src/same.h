/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// SAME (Specific Area Message Encoding): las cabeceras de aviso de la NOAA Weather Radio (EE. UU.; Canada tiene
// otra parecida) y del EAS. AFSK de 520,83 baudios (marca 2083,3 Hz, espacio 1562,5 Hz), 16 bytes 0xAB de preambulo,
// bytes con el bit menos significativo primero, la cabecera ASCII tres veces
// ("ZCZC-WXR-TOR-048201-048157+0030-3171845-KHGX/NWS-") y, al final, "NNNN" tres veces.
// Las emisoras van de 162,400 a 162,550 MHz en FM (WX1 a WX7).
#ifndef PLOTTER_SAME_H_
#define PLOTTER_SAME_H_

#include <cstdint>
#include <string>
#include <vector>

#include "alerts.h"

namespace plotter {

struct SameMsg {
    std::string raw;                // cabecera entera
    std::string org, event;         // "WXR", "TOR"
    std::vector<std::string> fips;  // "048201" (P subdivision, SS estado, CCC condado)
    int durationMin = 0;
    int julianDay = 0, hour = 0, minute = 0;    // emision (UTC)
    std::string sender;             // "KHGX/NWS"
};

bool sameParse(const std::string& header, SameMsg& m);
size_t sameHeaderEnd(const std::string& s);                    // longitud de una cabecera completa al principio de s, 0 si no
const char* sameEventName(const std::string& code);            // "TOR" -> "Tornado Warning"; nullptr si no la conoce
uint8_t sameEventLevel(const std::string& code);               // nivel comun 0..3
const char* sameOrgName(const std::string& org);               // "WXR" -> "National Weather Service"
// Aviso comun: onset desde el dia juliano (del ano de "now"), expires con la duracion
Alert sameToAlert(const SameMsg& m, int64_t nowUnix);

// Canales de la NOAA Weather Radio: WX1 162,550 ... WX7 162,525 MHz. 0 si la frecuencia no es uno de ellos (+-3 kHz).
extern const double kNwrMhz[7];
int nwrChannel(double mhz);

// Demodulador: muestras de audio (cualquier escala) a "fs" (8 a 48 kHz). poll() da cada cabecera confirmada
// (dos copias iguales, mayoria de tres, o una sola valida si no llegan mas en 6 s) y "NNNN" al final del aviso.
class SameDecoder {
public:
    void begin(float fs);
    void reset();
    void feed(const float* x, size_t n);
    void feed(const int16_t* x, size_t n);
    void push(float x) { sample(x); }                           // una muestra (sin mirar los plazos; poll() los mira)
    bool poll(std::string& header);
    uint32_t bursts() const { return burstCount_; }            // rafagas leidas (para el estado)
private:
    void sample(float x);
    void onBit(int b);
    void endBurst();
    void addBurst(const std::string& s);
    void emit(const std::string& s);
    void timeouts();

    float fs_ = 0, step_ = 0, phase_ = 0, prevD_ = 0;
    int n_ = 0, idx_ = 0;
    float mr_ = 1, mi_ = 0, sr_ = 1, si_ = 0;                   // osciladores de marca y espacio
    float mwr_ = 1, mwi_ = 0, swr_ = 1, swi_ = 0;               // giro por muestra
    std::vector<float> ring_;                                   // 4 por muestra
    float aMI_ = 0, aMQ_ = 0, aSI_ = 0, aSQ_ = 0;
    uint8_t shift_ = 0;
    bool synced_ = false;
    int bitCount_ = 0;
    std::string burst_;
    uint64_t t_ = 0;                                            // muestras desde begin
    struct Burst { std::string text; uint64_t t; };
    std::vector<Burst> recent_;
    bool emittedGroup_ = false;
    std::string lastEmit_;
    uint64_t lastEmitT_ = 0;
    std::vector<std::string> out_;
    uint32_t burstCount_ = 0;
    uint32_t renorm_ = 0;
};

// Genera una senal SAME (para pruebas y para el boton "Test" de la app): preambulo, texto, tres veces con 1 s entre
// medias. Amplitud 0,5.
void sameSynth(const std::string& text, float fs, std::vector<float>& out, int repeats = 3);

}  // namespace plotter
#endif
