// Prueba en PC del modem M17 (include/m17_modem.h). Ver tools/afsk/README.md
// Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
//
//   test_m17                         pruebas propias
//   test_m17 decode file.s16         decodifica banda base int16 a 48 kHz (m17-packet-encode -r) y escribe lo recibido
//   test_m17 symbols file.f32 text   escribe los simbolos (float) de un SMS para m17-packet-decode
#include "m17_modem.h"
#include "m17.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)

struct Got { std::vector<std::string> pkts; std::vector<M17M::Lsf> lsfs; };
static void onLsf(const M17M::Lsf& l, void* c) { ((Got*)c)->lsfs.push_back(l); }
static void onPkt(const M17M::Lsf& l, const uint8_t* d, size_t n, void* c) {
    char t[900];
    std::string body((const char*)d + 1, (const char*)d + n);
    while (!body.empty() && body.back() == 0) body.pop_back();
    snprintf(t, sizeof(t), "%s>%s:%02X:%s", l.src, l.dst, d[0], body.c_str());
    ((Got*)c)->pkts.push_back(t);
}

static std::vector<float> baseband(const float* syms, size_t n, float amp, double clockErr = 0) {
    // error de reloj: se remuestrea la banda base de 48 kHz con interpolacion lineal
    M17M::BasebandTx tx; tx.start(syms, n, amp);
    std::vector<int16_t> b(400000); size_t m = tx.fill(b.data(), b.size());
    std::vector<float> x(4800, 0.0f);
    for (double t = 0; t + 1 < m; t += 1.0 + clockErr) { size_t i = (size_t)t; float fr = (float)(t - i); x.push_back((b[i] * (1 - fr) + b[i + 1] * fr) / 32768.0f); }
    x.resize(x.size() + 4800, 0.0f);
    return x;
}

static Got decode(const std::vector<float>& x) {
    Got g; M17M::Rx rx; rx.begin(); rx.setCallbacks(onLsf, onPkt, &g);
    for (float v : x) rx.feed(v);
    return g;
}

static std::vector<float> sms(const char* src, const char* dst, const char* text, float amp, double clk = 0) {
    std::vector<uint8_t> d; d.push_back(M17M::PROTO_SMS); for (const char* p = text; *p; p++) d.push_back((uint8_t)*p); d.push_back(0);
    static float syms[M17M::maxPacketSymbols()];
    size_t n = M17M::packetSymbols(src, dst, 0, d.data(), d.size(), syms, M17M::maxPacketSymbols());
    CHECK(n > 0);
    return baseband(syms, n, amp, clk);
}

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "decode")) {
        FILE* f = fopen(argv[2], "rb"); if (!f) return 2;
        std::vector<float> x; int16_t v;
        while (fread(&v, 2, 1, f) == 1) x.push_back(v / 32768.0f);
        fclose(f);
        x.insert(x.begin(), 4800, 0.0f); x.resize(x.size() + 4800, 0.0f);
        Got g = decode(x);
        for (auto& p : g.pkts) printf("%s\n", p.c_str());
        return g.pkts.empty() ? 1 : 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "symbols")) {
        std::vector<uint8_t> d; d.push_back(M17M::PROTO_SMS); for (const char* p = argv[3]; *p; p++) d.push_back((uint8_t)*p); d.push_back(0);
        static float syms[M17M::maxPacketSymbols()];
        size_t n = M17M::packetSymbols("EA7JRS-7", "ALL", 0, d.data(), d.size(), syms, M17M::maxPacketSymbols());
        FILE* f = fopen(argv[2], "wb"); fwrite(syms, sizeof(float), n, f); fclose(f);
        return n ? 0 : 1;
    }

    // SMS corto y largo (varias tramas, y el caso de 25 bytes justos en la ultima)
    const char* texts[] = { "Hola desde la T-TWR", "x", std::string(100, 'A').c_str() };
    std::string longText(300, 'b'); for (size_t i = 0; i < longText.size(); i++) longText[i] = (char)('a' + i % 26);
    std::string exact(21, 'e');                                     // 1 + 21 + 1 + 2 = 25 bytes
    for (const std::string& t : { std::string("Hola desde la T-TWR"), std::string("x"), longText, exact }) {
        Got g = decode(sms("EA7JRS-7", "EB7ABC", t.c_str(), 0.6f));
        CHECK(g.pkts.size() == 1);
        if (g.pkts.size() == 1) CHECK(g.pkts[0] == "EA7JRS-7>EB7ABC:05:" + t);
        CHECK(g.lsfs.size() == 1);
    }
    (void)texts;
    // niveles, continua, error de reloj
    for (float a : { 0.05f, 0.95f }) { Got g = decode(sms("EA7JRS", "ALL", "nivel", a)); CHECK(g.pkts.size() == 1); }
    { auto x = sms("EA7JRS", "ALL", "continua", 0.5f); for (auto& v : x) v += 0.2f; Got g = decode(x); CHECK(g.pkts.size() == 1); }
    for (double e : { -0.001, 0.001 }) { Got g = decode(sms("EA7JRS", "ALL", "reloj +-0,1 %", 0.5f, e)); CHECK(g.pkts.size() == 1); }
    // ruido
    std::mt19937 rng(17); std::normal_distribution<float> gn(0.0f, 1.0f);
    for (float snr : { 20.0f, 14.0f, 10.0f }) {
        int ok = 0;
        for (int k = 0; k < 30; k++) {
            auto x = sms("EA7JRS-9", "ALL", "ruido en M17", 0.5f);
            const float noise = 0.5f * 0.45f / powf(10.0f, snr / 20.0f);
            for (auto& v : x) v += noise * gn(rng);
            Got g = decode(x);
            if (g.pkts.size() == 1 && g.pkts[0] == "EA7JRS-9>ALL:05:ruido en M17") ok++;
        }
        printf("SNR %4.1f dB: %d/30\n", snr, ok);
        if (snr >= 14) CHECK(ok == 30);
    }
    { std::vector<float> x(48000 * 10); for (auto& v : x) v = 0.3f * gn(rng); Got g = decode(x); CHECK(g.pkts.empty() && g.lsfs.empty()); }

    // emision de voz con posicion GNSS en el META (como una radio M17): LSF + 12 tramas de corriente + EOT
    {
        lsf_t lsf;
        set_LSF(&lsf, "EA7JRS-5", "@ALL", (uint16_t)(M17_TYPE_STREAM | M17_TYPE_VOICE | M17_TYPE_META_POSITION), nullptr);
        set_LSF_meta_position(&lsf, 0, M17_GNSS_STATION_MOBILE, 37.3909f, -5.9845f,
                              M17_GNSS_LAT_LON_VALID | M17_GNSS_ALT_VALID | M17_GNSS_SPD_BEARING_VALID, 12.0f, 270, 50.0f, 0);
        std::vector<float> syms(192 * 20);
        uint32_t cnt = 0;
        gen_preamble(syms.data(), &cnt, PREAM_LSF);
        float fr[192];
        gen_frame(fr, nullptr, FRAME_LSF, &lsf, 0, 0); memcpy(&syms[cnt], fr, sizeof(fr)); cnt += 192;
        uint8_t voice[16] = {0};
        for (uint16_t fn = 0; fn < 12; fn++) {
            gen_frame(fr, voice, FRAME_STR, &lsf, (uint8_t)(fn % 6), fn == 11 ? (uint16_t)(fn | 0x8000) : fn);
            memcpy(&syms[cnt], fr, sizeof(fr)); cnt += 192;
        }
        gen_eot(syms.data(), &cnt);
        auto x = baseband(syms.data(), cnt, 0.6f);
        Got g = decode(x);
        CHECK(g.lsfs.size() >= 2);                                  // el LSF y, al menos una vez, el armado con el LICH
        bool lichSeen = false;
        for (auto& l : g.lsfs) {
            CHECK(!strcmp(l.src, "EA7JRS-5") && l.stream && l.hasPosition);
            CHECK(fabsf(l.lat - 37.3909f) < 0.001f && fabsf(l.lon + 5.9845f) < 0.001f && l.bearing == 270);
            if (l.fromLich) lichSeen = true;
        }
        CHECK(lichSeen);
        // sin el LSF del principio (se engancha a mitad): solo el LICH
        std::vector<float> late(x.begin() + (4800 + 2 * 192 * 10 + 50), x.end());
        Got g2 = decode(late);
        CHECK(!g2.lsfs.empty() && g2.lsfs[0].fromLich && g2.lsfs[0].hasPosition);
        printf("voz con GNSS: %zu LSF (%s), tarde: %zu\n", g.lsfs.size(), lichSeen ? "LSF + LICH" : "solo LSF", g2.lsfs.size());
    }
    // APRS sobre M17: conversiones
    {
        char out[900]; uint8_t pk[900];
        M17M::Lsf l; memset(&l, 0, sizeof(l)); strcpy(l.src, "EA7JRS-7"); strcpy(l.dst, "@ALL");
        const char* aprs = "EA7JRS-7>APZLRP,WIDE1-1:!3723.45N/00559.07W>Hola";
        size_t n = M17M::tnc2ToPacket(aprs, pk, sizeof(pk));
        CHECK(n == strlen(aprs) + 1 && pk[0] == M17M::PROTO_APRS);
        CHECK(M17M::packetToTnc2(l, pk, n, out, sizeof(out)) > 0 && !strcmp(out, aprs));
        // 0x02 sin cabecera: la del LSF
        const uint8_t bare[] = { 0x02, '>', 'Q', 'R', 'V' };
        CHECK(M17M::packetToTnc2(l, bare, sizeof(bare), out, sizeof(out)) > 0 && !strcmp(out, "EA7JRS-7>APZM17:>QRV"));
        // SMS a un indicativo y a todos, con caracteres reservados y texto largo
        strcpy(l.dst, "EB7ABC");
        n = M17M::smsPacket("Hola {1}", pk, sizeof(pk));
        CHECK(n == 10 && pk[0] == M17M::PROTO_SMS && pk[9] == 0);
        CHECK(M17M::packetToTnc2(l, pk, n, out, sizeof(out)) > 0 && !strcmp(out, "EA7JRS-7>APZM17,NOGATE::EB7ABC   :Hola (1}"));
        strcpy(l.dst, "@ALL");
        n = M17M::smsPacket(std::string(100, 'z').c_str(), pk, sizeof(pk));
        CHECK(M17M::packetToTnc2(l, pk, n, out, sizeof(out)) > 0 && strlen(out) == strlen("EA7JRS-7>APZM17,NOGATE::ALL      :") + 67);
        // indicativos M17 con '/' y '.', y no validos
        char c[12];
        CHECK(M17M::aprsCall("EA7JRS/P", c, sizeof(c)) && !strcmp(c, "EA7JRS-P"));
        CHECK(!M17M::aprsCall("@ALL", c, sizeof(c)) && !M17M::aprsCall("#BCAST", c, sizeof(c)) && !M17M::aprsCall("AB", c, sizeof(c)));
        const uint8_t raw[] = { 0x00, 1, 2, 3 };
        CHECK(M17M::packetToTnc2(l, raw, sizeof(raw), out, sizeof(out)) == 0);
        // emision de voz: posicion y estado
        l.stream = true; l.type = (uint16_t)(M17_TYPE_STREAM | M17_TYPE_VOICE);
        CHECK(M17M::lsfToTnc2(l, out, sizeof(out)) > 0);
        CHECK(!strcmp(out, "EA7JRS-7>APZM17,NOGATE:>M17 voice to @ALL"));
        l.hasPosition = true; l.lat = 37.3909f; l.lon = -5.9845f; l.stationType = 1; l.speed = 50.0f; l.bearing = 270; l.altitude = 12.0f;
        l.gnssValid = M17_GNSS_LAT_LON_VALID | M17_GNSS_ALT_VALID | M17_GNSS_SPD_BEARING_VALID;
        CHECK(M17M::lsfToTnc2(l, out, sizeof(out)) > 0);
        CHECK(!strcmp(out, "EA7JRS-7>APZM17,NOGATE:!3723.45N/00559.07W>270/027/A=000039 M17 voice to @ALL"));
        l.gnssValid = M17_GNSS_LAT_LON_VALID; l.stationType = 2; l.lat = -33.5f; l.lon = 151.25f;
        CHECK(M17M::lsfToTnc2(l, out, sizeof(out)) > 0 && !strcmp(out, "EA7JRS-7>APZM17,NOGATE:!3330.00S/15115.00E[ M17 voice to @ALL"));
    }
    printf(fails ? "%d FALLOS\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
