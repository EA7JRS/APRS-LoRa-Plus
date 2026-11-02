// Prueba en PC del decodificador POCSAG (include/pocsag.h): g++ -std=c++17 -Iinclude tools/test_pocsag.cpp -o /tmp/tp && /tmp/tp
// Copyright (c) 2026 Manuel - EA7JRS
#include "pocsag.h"
#include <vector>
#include <string>
#include <cstdlib>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

// ---- codificador de pruebas ----
struct Bits { std::vector<uint8_t> v; void put(uint32_t value, int n) { for (int i = n - 1; i >= 0; i--) v.push_back((value >> i) & 1); } };

static uint32_t addressWord(uint32_t ric, int func) { return Pocsag::encodeWord(((ric >> 3) << 2 | (func & 3)) & 0xFFFFF); }   // bit 20 = 0 (direccion): 18 bits de direccion + 2 de funcion
static uint32_t messageWord(uint32_t data20) { return Pocsag::encodeWord(0x100000UL | (data20 & 0xFFFFF)); }

// Construye lotes: cada elemento es (ric, func, bits del mensaje). Devuelve el flujo de bits con preambulo y sincronismos
struct Item { uint32_t ric; int func; std::vector<uint8_t> payload; };

static Bits buildStream(const std::vector<Item>& items, bool preamble = true) {
    std::vector<uint32_t> words;                 // palabras ya colocadas en lotes de 16 (con relleno)
    std::vector<uint32_t> cur(16, Pocsag::IDLE_WORD);
    int pos = 0;
    auto flushBatch = [&]() { for (auto w : cur) words.push_back(w); cur.assign(16, Pocsag::IDLE_WORD); pos = 0; };
    for (const auto& it : items) {
        int frame = it.ric & 7;
        if (pos < frame * 2) pos = frame * 2;
        else if (pos > frame * 2 && pos != 0) { flushBatch(); pos = frame * 2; }
        if (pos >= 16) { flushBatch(); pos = frame * 2; }
        cur[pos++] = addressWord(it.ric, it.func);
        size_t b = 0;
        while (b < it.payload.size()) {
            uint32_t data = 0;
            for (int k = 0; k < 20; k++) data = (data << 1) | (b + k < it.payload.size() ? it.payload[b + k] : 0);
            b += 20;
            if (pos >= 16) flushBatch();
            cur[pos++] = messageWord(data);
        }
        if (pos >= 16) flushBatch();
    }
    flushBatch();
    Bits s;
    if (preamble) for (int i = 0; i < 576; i++) s.v.push_back(i & 1 ? 0 : 1);
    for (size_t i = 0; i < words.size(); i++) {
        if (i % 16 == 0) s.put(Pocsag::SYNC_WORD, 32);
        s.put(words[i], 32);
    }
    return s;
}

static std::vector<uint8_t> alphaBits(const std::string& text) {
    std::vector<uint8_t> v;
    for (char c : text) for (int j = 0; j < 7; j++) v.push_back((c >> j) & 1);
    return v;
}
static std::vector<uint8_t> numericBits(const std::string& text) {
    static const std::string T = "0123456789*U -][";
    std::vector<uint8_t> v;
    for (char c : text) { size_t i = T.find(c); for (int j = 0; j < 4; j++) v.push_back((i >> j) & 1); }
    return v;
}

struct Got { uint32_t ric; int func; std::string alpha, numeric; bool corrupt; };
static std::vector<Got> got;
static uint8_t gotCorrected = 0;
static void onMsg(const Pocsag::Message& m, void*) {
    Got g; g.ric = m.ric; g.func = m.func; g.corrupt = m.corrupt;
    char buf[256]; Pocsag::decodeAlpha(m, buf, sizeof(buf)); g.alpha = buf;
    Pocsag::decodeNumeric(m, buf, sizeof(buf)); g.numeric = buf;
    gotCorrected += m.corrected;
    got.push_back(g);
}

static std::vector<uint8_t> toBytes(const Bits& s, int offsetBits, bool invert, int tailBits = 200, unsigned noiseSeed = 1) {
    std::vector<uint8_t> bits;
    srand(noiseSeed);
    for (int i = 0; i < offsetBits; i++) bits.push_back(rand() & 1);
    for (auto b : s.v) bits.push_back(invert ? !b : b);
    for (int i = 0; i < tailBits; i++) bits.push_back(rand() & 1);       // ruido tras la emision
    std::vector<uint8_t> bytes((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); i++) if (bits[i]) bytes[i / 8] |= 0x80 >> (i % 8);
    return bytes;
}

static void run(const std::vector<uint8_t>& bytes, size_t block = 64) {
    got.clear(); gotCorrected = 0;
    Pocsag::Decoder d; d.reset();
    for (size_t i = 0; i < bytes.size(); i += block) d.feed(&bytes[i], std::min(block, bytes.size() - i), onMsg, nullptr);
}

int main() {
    // 1) BCH: la palabra de sincronismo y la de relleno del estandar son palabras validas
    uint32_t w = Pocsag::SYNC_WORD; uint8_t e;
    CHECK(Pocsag::fixWord(w, e) && e == 0 && w == Pocsag::SYNC_WORD, "sync valid");
    w = Pocsag::IDLE_WORD;
    CHECK(Pocsag::fixWord(w, e) && e == 0, "idle valid");
    // codificacion: la palabra de relleno es la codificacion de sus 21 bits de datos
    CHECK(Pocsag::encodeWord(Pocsag::IDLE_WORD >> 11) == Pocsag::IDLE_WORD, "encode idle");
    CHECK(Pocsag::encodeWord(Pocsag::SYNC_WORD >> 11) == Pocsag::SYNC_WORD, "encode sync");

    // 2) correccion de 1 y 2 bits, y rechazo con 3
    uint32_t base = Pocsag::encodeWord(0x0ABCDE);
    for (int i = 0; i < 32; i++) { uint32_t x = base ^ (1UL << i); CHECK(Pocsag::fixWord(x, e) && x == base && e >= 1, "fix 1 bit"); }
    int okTwo = 0;
    for (int i = 0; i < 32; i++) for (int j = i + 1; j < 32; j++) { uint32_t x = base ^ (1UL << i) ^ (1UL << j); if (Pocsag::fixWord(x, e) && x == base) okTwo++; }
    CHECK(okTwo == 32 * 31 / 2, "fix 2 bits");
    int rejected = 0, total = 0;
    for (int i = 0; i < 32; i += 3) for (int j = i + 1; j < 32; j += 5) for (int k = j + 1; k < 32; k += 4) { uint32_t x = base ^ (1UL << i) ^ (1UL << j) ^ (1UL << k); total++; uint32_t y = x; if (!Pocsag::fixWord(y, e) || y != base) rejected++; }
    CHECK(rejected == total, "3 errors are not 'fixed' into the original word");

    // 3) mensaje alfanumerico simple, en todas las alineaciones de bit
    for (int off = 0; off < 8; off++) {
        Bits s = buildStream({ { 1234567, 3, alphaBits("HELLO DAPNET 73 de EA7JRS") } });
        run(toBytes(s, off, false, 200, 10 + off));
        CHECK(got.size() == 1, "one message");
        if (got.size() == 1) { CHECK(got[0].ric == 1234567, "ric"); CHECK(got[0].func == 3, "func"); CHECK(got[0].alpha == "HELLO DAPNET 73 de EA7JRS", "text"); CHECK(!got[0].corrupt, "not corrupt"); }
    }

    // 4) polaridad invertida (el chip puede dar el flujo al reves)
    { Bits s = buildStream({ { 8, 3, alphaBits("EA7JRS") } }); run(toBytes(s, 3, true, 200, 5));
      CHECK(got.size() == 1 && got[0].alpha == "EA7JRS" && got[0].ric == 8, "inverted stream"); }

    // 5) mensaje largo en varios lotes y dos mensajes seguidos
    { std::string longText; for (int i = 0; i < 12; i++) longText += "0123456789 ABCDEFGHIJ klmnopqrst "; longText = longText.substr(0, 190);
      Bits s = buildStream({ { 2504, 0, numericBits("183000 011126") }, { 4519, 3, alphaBits(longText) }, { 999999, 3, alphaBits("second") } });
      run(toBytes(s, 5, false, 300, 7), 64);
      CHECK(got.size() == 3, "three messages");
      if (got.size() == 3) {
          CHECK(got[0].ric == 2504 && got[0].func == 0 && got[0].numeric.compare(0, 13, "183000 011126") == 0, "numeric time message");
          CHECK(got[1].ric == 4519 && got[1].alpha == longText, "long message across batches");
          CHECK(got[2].ric == 999999 && got[2].alpha == "second", "third message");
      } }

    // 6) errores de bit: 1 bit por palabra se corrige; el texto sale intacto
    { Bits s = buildStream({ { 1000, 3, alphaBits("ERRORES DE BIT CORREGIBLES") } });
      srand(3);
      // se invierte un bit por cada palabra de mensaje a partir del primer sincronismo (los sincronismos no se tocan)
      size_t start = 576 + 32;
      for (size_t i = start; i + 32 <= s.v.size(); i += 32) { if ((i - 576) % 544 == 0) continue; s.v[i + (rand() % 32)] ^= 1; }
      run(toBytes(s, 2, false, 200, 4));
      CHECK(got.size() >= 1, "message with bit errors");
      if (!got.empty()) { CHECK(got[0].alpha == "ERRORES DE BIT CORREGIBLES", "text corrected"); CHECK(gotCorrected > 0, "corrections counted"); } }

    // 7) solo ruido: no se inventa ningun mensaje
    { srand(99); std::vector<uint8_t> noise(20000); for (auto& b : noise) b = rand() & 0xFF; run(noise); CHECK(got.empty(), "noise gives nothing"); }

    // 8) hora
    Pocsag::TimeStamp t;
    CHECK(!Pocsag::parseTime("193500 161018", t), "implausible old date (2018) rejected");
    CHECK(Pocsag::parseTime("20261101183000", t) && t.year == 2026 && t.month == 11 && t.day == 1 && t.hour == 18 && t.minute == 30, "14 digits");
    CHECK(Pocsag::parseTime("183000 011126", t) && t.year == 2026 && t.month == 11 && t.day == 1 && t.hour == 18, "HHMMSS DDMMAA");
    CHECK(Pocsag::parseTime("261101183000", t) && t.year == 2026 && t.month == 11 && t.day == 1 && t.hour == 18, "YYMMDDhhmmss");
    CHECK(!Pocsag::parseTime("hola", t), "not a time");
    CHECK(Pocsag::parseTime("183000 01112600", t) && t.year == 2026 && t.month == 11 && t.day == 1 && t.hour == 18, "numeric padding after the date is ignored");

    // 9) RIC y juego de caracteres
    uint32_t rics[8]; uint8_t n = Pocsag::parseRicList("1234567, 8;224 abc 99999999 2097151", rics, 8);
    CHECK(n == 4 && rics[0] == 1234567 && rics[1] == 8 && rics[2] == 224 && rics[3] == 2097151, "ric list");
    { Pocsag::Message m; m.bits = 0; std::vector<uint8_t> v = alphaBits("[x]{y}~"); for (size_t i = 0; i < v.size(); i++) { if (v[i]) m.raw[i >> 3] |= 0x80 >> (i & 7); m.bits++; }
      char out[64]; Pocsag::decodeAlpha(m, out, sizeof(out), Pocsag::CHARSET_DIN66003); CHECK(std::string(out) == "AexUeaeyuess", "din charset");
      Pocsag::decodeAlpha(m, out, sizeof(out), Pocsag::CHARSET_ASCII); CHECK(std::string(out) == "[x]{y}~", "ascii charset"); }

    printf("fails=%d\n", fails);
    return fails ? 1 : 0;
}
