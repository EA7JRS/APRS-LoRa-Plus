// Simula la via de emision del firmware (codificador, calendario, Shaper con cuantizador de PLL) y escribe un WAV a 12 kHz + los tonos.
// Compilar como test_fst4w.cpp, anadiendo src/ft_beacon.cpp. Uso: t_sim ft8|ft4|f120|f300 salida.wav "CQ EA7JRS IM77" [locator dbm ruido desplazamiento_pll_Hz]
// FT8/FT4 se decodifican con decode_ft8 de ft8_lib (-ft4 para FT4); FST4W-120/300 con tools/ft/demod_fst4w.py.
// Simula la via de emision del firmware (palabras del PLL escritas en los instantes de stepStartUs) y escribe un WAV a 12 kHz
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#include "ft_beacon.h"
#include "fst4w.h"
int main(int argc, char** argv) {
    // t_sim <ft8|ft4|f120|f300> <salida.wav> [texto|indicativo] [locator] [dbm] [ruido_sigma] [fase_pll_Hz]
    const char* m = argv[1];
    FTX::Proto p = !strcmp(m, "ft4") ? FTX::PROTO_FT4 : !strcmp(m, "f120") ? FTX::PROTO_FST4W_120 : !strcmp(m, "f300") ? FTX::PROTO_FST4W_300 : FTX::PROTO_FT8;
    const char* arg3 = argc > 3 ? argv[3] : "CQ EA7JRS IM77";
    double sigma = argc > 6 ? atof(argv[6]) : 0.0;
    double gridShift = argc > 7 ? atof(argv[7]) : 0.0;     // desplazamiento de la rejilla del PLL respecto al audio (Hz): el dial real no cae en un multiplo del paso
    uint8_t tones[FTX::MAX_SYMBOLS];
    if (FTX::isFst4w(p)) { if (!FST4W::encode(arg3, argc > 4 ? argv[4] : "IM77", argc > 5 ? atoi(argv[5]) : 10, tones)) { puts("ENCFAIL"); return 1; } }
    else if (!FTX::encode(p, arg3, tones)) { puts("ENCFAIL"); return 1; }
    FTX::Shaper sh;
    sh.begin(p, tones, FTX::tone0AudioHz(p) + gridShift);
    const double fs = 12000.0;
    const double total = FTX::slotMs(p) / 1000.0;
    const size_t N = (size_t)(total * fs);
    std::vector<double> f(N, 0.0), amp(N, 0.0);
    const uint8_t ns = FTX::symbols(p), nst = sh.steps();
    double t0 = FTX::startDelayMs(p) / 1000.0;
    std::vector<double> fhw;
    // lista de (tiempo, frecuencia)
    std::vector<std::pair<double, double>> ev;
    for (uint8_t i = 0; i < ns; i++) for (uint8_t j = 0; j < nst; j++) {
        uint32_t w = sh.nextWord(i, j);
        ev.push_back({ t0 + FTX::stepStartUs(p, i, j) / 1e6, (double)w * FTX::PLL_STEP_HZ - gridShift });
    }
    double tend = t0 + (FTX::symbolStartUs(p, ns - 1) + FTX::symbolUs(p)) / 1e6;
    double phase = 0; size_t k = 0; srand(1);
    std::vector<short> pcm(N);
    double cur = 0;
    for (size_t n = 0; n < N; n++) {
        double t = n / fs;
        while (k < ev.size() && ev[k].first <= t) { cur = ev[k].second; k++; }
        bool on = t >= t0 && t < tend;
        if (on) phase += 2 * M_PI * cur / fs;
        double x = on ? 0.5 * sin(phase) : 0.0;
        if (sigma > 0) { double u = 0; for (int q = 0; q < 12; q++) u += rand() / (double)RAND_MAX; x += sigma * (u - 6.0); }
        double v = x * 32767.0; if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        pcm[n] = (short)lrint(v);
    }
    FILE* o = fopen(argv[2], "wb");
    unsigned dl = (unsigned)(N * 2), sr = 12000, br = 24000; short fm = 1, ch = 1, ba = 2, bits = 16; unsigned fl = 16, rl = 36 + dl;
    fwrite("RIFF", 1, 4, o); fwrite(&rl, 4, 1, o); fwrite("WAVEfmt ", 1, 8, o); fwrite(&fl, 4, 1, o); fwrite(&fm, 2, 1, o); fwrite(&ch, 2, 1, o);
    fwrite(&sr, 4, 1, o); fwrite(&br, 4, 1, o); fwrite(&ba, 2, 1, o); fwrite(&bits, 2, 1, o); fwrite("data", 1, 4, o); fwrite(&dl, 4, 1, o); fwrite(pcm.data(), 2, N, o);
    fclose(o);
    // tonos transmitidos (para la comparacion del demodulador)
    FILE* tf = fopen((std::string(argv[2]) + ".tones").c_str(), "w");
    for (uint8_t i = 0; i < ns; i++) fprintf(tf, "%d", tones[i]);
    fprintf(tf, "\n");
    fclose(tf);
    printf("%s: %u simbolos, %u escrituras, %.3f s\n", FTX::name(p), ns, (unsigned)ev.size(), tend - t0);
    return 0;
}
