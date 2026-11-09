// Prueba en PC del nucleo del modo FM de la T-TWR (include/fm_core.h)
// Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
//   g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tools/fm/test_fm.cpp src/fm_core.cpp -o /tmp/test_fm && /tmp/test_fm
#include "fm_core.h"
#include <cstdio>
#include <cstring>

using namespace FmCore;
static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)

static bool q(const char* name, const char* c, Qsy& o) { return parseQsy(name, c, o); }

int main() {
    // tonos
    CHECK(CTCSS[0] == 670 && CTCSS[CTCSS_COUNT - 1] == 2541 && DCS[0] == 23 && DCS[DCS_COUNT - 1] == 754);
    for (int i = 1; i < CTCSS_COUNT; i++) CHECK(CTCSS[i] > CTCSS[i - 1]);
    for (int i = 1; i < DCS_COUNT; i++) CHECK(DCS[i] > DCS[i - 1]);
    for (int i = 0; i < DCS_COUNT; i++) { int c = DCS[i]; CHECK(c % 10 < 8 && (c / 10) % 10 < 8 && c / 100 < 8); }   // octal
    CHECK(niceRfCtcss(670) == 1 && niceRfCtcss(885) == 8 && niceRfCtcss(2503) == 38 && niceRfCtcss(693) == 0 && niceRfCtcss(1598) == 0);
    CHECK(ctcssThreshold(670) == 0x0C0D && ctcssThreshold(1035) == 0x0606 && ctcssThreshold(2541) == 0x0505);
    CHECK(nextCtcss(2541, 1) == 670 && nextCtcss(670, -1) == 2541 && nextCtcss(1000, 1) == 1035 && nextCtcss(1001, 1) == 1035);
    CHECK(nextDcs(754, 1) == 23 && nextDcs(23, -1) == 754 && validDcs(23) && !validDcs(24) && !validDcs(29));
    CHECK(!strcmp(toneModeName(TONE_TSQL), "TSQL"));

    // canal, desplazamientos
    Channel c; clearChannel(c, 145600000L);
    CHECK(c.offsetHz == 600000L && txHz(c) == 145600000L);
    c.shift = -1; CHECK(txHz(c) == 145000000L && validChannel(c, 136000000L, 174000000L));
    // emision solo en las bandas de aficionado
    CHECK(txAllowed(144800000L, 1) && txAllowed(145990000L, 1) && !txAllowed(146500000L, 1) && txAllowed(146940000L, 2) && txAllowed(147000000L, 3));
    CHECK(!txAllowed(146500000L, 0) && txAllowed(145500000L, 0) && txAllowed(433500000L, 0) && !txAllowed(446006250L, 0));
    CHECK(!txAllowed(420500000L, 2) && txAllowed(430000000L, 2) && !txAllowed(420500000L, 1) && !txAllowed(441000000L, 1) && txAllowed(449000000L, 2) && !txAllowed(451000000L, 2));
    CHECK(txAllowed(147500000L, 2) && !txAllowed(148500000L, 2) && !txAllowed(441000000L, 3) && !txAllowed(146100000L, 1));
    CHECK(!txAllowed(161975000L, 1) && !txAllowed(156800000L, 2) && !txAllowed(137100000L, 3) && !txAllowed(162550000L, 2));
    CHECK(standardOffset(438650000L, 1) == 7600000L && standardOffset(442000000L, 2) == 5000000L);
    int8_t sh = 0; long off = 0;
    CHECK(autoRepeater(145650000L, 1, sh, off) && sh == -1 && off == 600000L);
    CHECK(autoRepeater(439050000L, 1, sh, off) && sh == -1 && off == 7600000L);
    CHECK(autoRepeater(147120000L, 2, sh, off) && sh == 1);
    CHECK(autoRepeater(444100000L, 2, sh, off) && sh == 1 && off == 5000000L);
    CHECK(!autoRepeater(145500000L, 1, sh, off) && !autoRepeater(144800000L, 2, sh, off));

    // VFO
    CHECK(snap(145512000L, 12500) == 145512500L && snap(145506000L, 12500) == 145500000L);
    CHECK(tune(145500000L, 1, 12500, 144000000L, 146000000L) == 145512500L);
    CHECK(tune(145500000L, -1, 12500, 144000000L, 146000000L) == 145487500L);
    CHECK(tune(145506000L, 1, 12500, 144000000L, 146000000L) == 145512500L);         // fuera de rejilla: al siguiente punto
    CHECK(tune(145506000L, -1, 12500, 144000000L, 146000000L) == 145500000L);
    CHECK(tune(146000000L, 1, 25000, 144000000L, 146000000L) == 144000000L);         // vuelta
    CHECK(tune(144000000L, -1, 25000, 144000000L, 146000000L) == 146000000L);
    CHECK(tune(145506250L, 1, 6250, 144000000L, 146000000L) == 145512500L);

    // QSY: ejemplos de la especificacion
    Qsy o;
    CHECK(q(nullptr, "439.400MHz T071 -760", o) && o.hz == 439400000L && o.toneMode == TONE_ENC && o.tone == 719 && o.hasOffset && o.offsetHz == -7600000L && !o.narrow);
    CHECK(q(nullptr, "438.650MHz TOFF -760", o) && o.toneMode == TONE_OFF && o.hasOffset && o.offsetHz == -7600000L);
    CHECK(q(nullptr, "145.737MHz t077 -060", o) && o.hz == 145737000L && o.tone == 770 && o.narrow && o.offsetHz == -600000L);
    CHECK(q(nullptr, "146.82 MHz T107 AARC Repeater (Tone of 107.2)", o) && o.hz == 146820000L && o.tone == 1072 && !o.hasOffset);
    CHECK(q(nullptr, "146.835MHz C107 R25m AARC", o) && o.toneMode == TONE_TSQL && o.tone == 1072 && o.rangeKm == 40);
    CHECK(q(nullptr, "146.40 MHz T067 +100 Repeater", o) && o.hz == 146400000L && o.tone == 670 && o.offsetHz == 1000000L);
    CHECK(q(nullptr, "442.440MHz T107 -500 Repeater", o) && o.offsetHz == -5000000L);
    CHECK(q(nullptr, "145.50 MHz t077 Simplex", o) && o.hz == 145500000L && o.narrow && !o.hasOffset);
    CHECK(q(nullptr, "145.600MHz D023 -060 R40k", o) && o.toneMode == TONE_DCS && o.dcs == 23 && o.rangeKm == 40);
    CHECK(q(nullptr, "145.600MHz 1750 -060", o) && o.toneMode == TONE_1750);
    CHECK(q(nullptr, "145.600mhz T088 -000", o) && o.hasOffset && o.offsetHz == 0 && o.tone == 885);
    CHECK(q("146.94-R", "T100 -060 R25k club", o) && o.hz == 146940000L && o.tone == 1000 && o.offsetHz == -600000L && o.rangeKm == 25);
    CHECK(!q(nullptr, "Hola desde EA7JRS", o) && !q(nullptr, "145.6 MHz", o) && !q(nullptr, "", o) && !q(nullptr, nullptr, o));
    CHECK(q(nullptr, "145.600MHz T999", o) && o.toneMode == TONE_OFF);                 // tono que no existe: se ignora el campo
    CHECK(q(nullptr, "145.600MHz D029", o) && o.toneMode == TONE_OFF);                 // DCS no valido

    // QSY -> canal
    Channel k;
    q(nullptr, "145.650MHz T088", o); qsyToChannel(o, 1, k);
    CHECK(k.rxHz == 145650000L && k.shift == -1 && k.offsetHz == 600000L && k.toneMode == TONE_ENC && k.tone == 885 && k.wide);
    q(nullptr, "145.650MHz t088 -000", o); qsyToChannel(o, 1, k);
    CHECK(k.shift == 0 && !k.wide);
    q(nullptr, "439.050MHz C123", o); qsyToChannel(o, 1, k);
    CHECK(k.shift == -1 && k.offsetHz == 7600000L && k.toneMode == TONE_TSQL && k.tone == 1230);
    q(nullptr, "145.500MHz", o); qsyToChannel(o, 1, k);
    CHECK(k.shift == 0 && k.toneMode == TONE_OFF);

    // formato propio y vuelta
    char t[32];
    Channel f; clearChannel(f, 145650000L); f.shift = -1; f.toneMode = TONE_ENC; f.tone = 885; f.wide = true;
    CHECK(formatQsy(f, 1, t, sizeof(t)) && !strcmp(t, "145.650MHz T088 -060"));
    f.wide = false; f.toneMode = TONE_TSQL; f.tone = 1072;
    CHECK(formatQsy(f, 1, t, sizeof(t)) && !strcmp(t, "145.650MHz c107 -060"));
    f.shift = 0; CHECK(formatQsy(f, 1, t, sizeof(t)) && !strcmp(t, "145.650MHz c107 -000"));
    f.toneMode = TONE_OFF; f.wide = true; CHECK(formatQsy(f, 1, t, sizeof(t)) && !strcmp(t, "145.650MHz Toff"));
    clearChannel(f, 439050000L); f.shift = -1; f.offsetHz = 7600000L; f.toneMode = TONE_DCS; f.dcs = 754; f.wide = true;
    CHECK(formatQsy(f, 1, t, sizeof(t)) && !strcmp(t, "439.050MHz D754 -760"));
    q(nullptr, t, o); qsyToChannel(o, 1, k);
    CHECK(k.rxHz == f.rxHz && k.shift == f.shift && k.offsetHz == f.offsetHz && k.toneMode == TONE_DCS && k.dcs == 754);
    CHECK(formatFreq(145512500L, t, sizeof(t)) && !strcmp(t, "145.5125"));

    // escaneo: canal libre -> avanza tras la espera; ocupado -> se para; portadora -> sigue 2 s despues de quedar libre
    Scanner s; s.start(0, RESUME_CARRIER, 150);
    CHECK(!s.tick(100, false) && s.tick(160, false));
    s.tuned(200); CHECK(!s.tick(400, true) && s.paused() && !s.tick(3000, true) && !s.tick(3100, false) && !s.tick(5000, false) && s.tick(5101, false));
    s.tuned(6000); CHECK(!s.tick(6200, true));
    Scanner tm; tm.start(0, RESUME_TIME, 150); tm.tuned(0); tm.tick(200, true);
    CHECK(!tm.tick(4000, true) && tm.tick(5300, true));
    Scanner st; st.start(0, RESUME_STOP, 150); st.tick(200, true); CHECK(!st.running());

    // ARTS
    Arts a; a.start(0, 15, true);
    CHECK(a.wantPoll(0, false) && !a.wantPoll(0, true));
    a.polled(1); CHECK(!a.wantPoll(10000, false) && a.wantPoll(15001, false));
    CHECK(!a.update(30000) && a.state == ARTS_UNKNOWN);
    a.heard(30000); CHECK(a.update(30001) && a.state == ARTS_IN);
    CHECK(!a.update(89000) && a.update(90001) && a.state == ARTS_OUT);
    a.heard(100000); CHECK(a.update(100001) && a.state == ARTS_IN);
    CHECK(!a.wantCwId(500000) && a.wantCwId(600000));
    Arts b; b.start(0, 25, false); CHECK(b.update(100001) && b.state == ARTS_OUT && !b.wantCwId(700000));

    // EAI: intervalo (indicativo, pitidos cada 2,5 s, fin) y continuo (tramos de microfono)
    Eai e; e.start(0, EAI_INTERVAL, 1);
    CHECK(e.tick(0) == EA_CWID && e.tick(100) == EA_NONE && e.tick(2500) == EA_BEEP && e.tick(3000) == EA_NONE && e.tick(5000) == EA_BEEP);
    CHECK(e.remainingS(30000) == 30 && e.tick(60000) == EA_END && !e.running());
    Eai ec; ec.start(0, EAI_CONTINUOUS, 15);
    CHECK(ec.tick(0) == EA_CWID && ec.tick(10) == EA_MIC_ON && ec.micOn && ec.tick(40000) == EA_NONE && ec.tick(50010) == EA_MIC_OFF);
    CHECK(ec.tick(55000) == EA_NONE && ec.tick(60010) == EA_MIC_ON);
    CHECK(ec.tick(600000) == EA_MIC_OFF && ec.tick(600001) == EA_CWID && ec.tick(600002) == EA_MIC_ON);
    CHECK(ec.tick(900000) == EA_MIC_OFF && ec.tick(900001) == EA_END);
    Eai lim; lim.start(0, EAI_INTERVAL, 99); CHECK(lim.remainingS(0) == 3000);

    // hombre caido
    ManDown m; m.configure(2); m.activity(0);
    CHECK(m.tick(80000) == 0 && m.tick(90000) == 1 && m.tick(100000) == 0 && m.tick(120000) == 2 && m.tick(130000) == 0);
    m.activity(130000); CHECK(m.tick(200000) == 0 && m.tick(220000) == 1);
    ManDown md0; md0.configure(0); CHECK(md0.tick(1u << 30) == 0 && !md0.enabled());

    printf(fails ? "%d FALLOS de %d\n" : "OK (%d comprobaciones)\n", fails ? fails : checks, checks);
    return fails ? 1 : 0;
}
