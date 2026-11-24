/* Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
 *
 * Simulador de las paginas del tracker en el PC (tools/pages_sim/build.sh): las funciones de dibujo se copian tal cual de
 * src/pages_utils.cpp (extract.py, funcs.txt) y se ejecutan sobre Adafruit_GFX real, con datos de ejemplo. -DOLED = 128x64
 * en un color; sin el, 160x80 a color como las TFT. Si una pagina nueva usa algo que aqui no existe, se anade su sustituto. */
#include "Arduino.h"
#include "Adafruit_GFX.h"
#include <TimeLib.h>
#include <vector>
#include "pages_core.h"
#include "astro.h"
#include "celnav.h"
#include "wind.h"
#include "alerts.h"
#include "net_wx.h"
#include "ephem.h"
#ifdef OLED
#define SW 128
#define SH 64
#else
#define SW 160
#define SH 80
#define HAS_TFT 1
#endif
#define PROGMEM
using std::isnan;
#ifndef OLED
#include "aprs_icons_color.h"
#endif
#include "aprs_icons.h"

uint32_t fakeMillis = 0;
time_t   fakeNow = 0;

// ---------- lienzo ----------
class Canvas : public Adafruit_GFX {
public:
    uint32_t px[SH][SW];
    Canvas() : Adafruit_GFX(SW, SH) { clear(); }
    void clear();
    void drawPixel(int16_t x, int16_t y, uint16_t c) override;
};
static Canvas display;

#define PAGES_W SW
#define PAGES_H SH
#define PAGES_EXTRA (SH - 64)
enum PagesZone : uint8_t { PZ_TOP, PZ_BODY, PZ_BOTTOM };
enum PagesInk : uint8_t { INK_TEXT, INK_HEAD, INK_ACCENT, INK_SEL, INK_FRAME, INK_MUTED, INK_GOOD, INK_WARN, INK_BAD,                              INK_SUN, INK_MOON, INK_SHADE, INK_NIGHT, INK_LV1, INK_LV2, INK_MARIA,                              INK_N1, INK_N2, INK_N3, INK_N4, INK_N5, INK_NTXT, INK_COUNT };
static const uint32_t PALD[]={0x131A22,0x131A22,0xE8EEF3,0x28B5E1,0xFFB347,0x3CE68A,0x28B5E1,0x9FB0BF,0x3CE68A,0xFFB347,0xFF6B6B,0xFFB347,0xF2EBD0,0x2B3A4A,0x3A4A5C,0xFFD84D,0xFF9A3C,0x9C9686,0xFFFF00,0xFFC800,0xFF9600,0xFF0000,0xC80000,0x000000,0x3CE68A,0x28B5E1,0xE8EEF3};
static const uint32_t PALL[]={0xF4F7FA,0xFFFFFF,0x131A22,0x1B8DB8,0xB05E00,0x1E9E5C,0x1B8DB8,0x5C6B7A,0x1E9E5C,0xB05E00,0xC83A3A,0xE08A00,0xE8C45A,0x3A4654,0x9AA8B6,0xB89400,0xC86400,0xA88A3A,0xFFFF00,0xFFC800,0xFF9600,0xFF0000,0xC80000,0x000000,0x1E9E5C,0x1B8DB8,0x131A22};
const uint32_t* PAL = PALD; uint32_t curInk = PALD[2];
#ifdef OLED
bool pagesHasColor() { return false; }
void pagesInk(uint8_t) { curInk = 0x9FE8FF; }
#else
bool pagesHasColor() { return true; }
void pagesInk(uint8_t i) { curInk = PAL[2 + (i < INK_COUNT ? i : 0)]; }
#endif
static uint32_t bgColor() {
#ifdef OLED
    return 0x05070A;
#else
    return PAL[0];
#endif
}
void Canvas::clear() { for (auto& r : px) for (auto& p : r) p = bgColor(); pagesInk(0); }
void Canvas::drawPixel(int16_t x, int16_t y, uint16_t c) { if (x >= 0 && x < SW && y >= 0 && y < SH) px[y][x] = c ? curInk : bgColor(); }

void pagesZone(uint8_t) {}
void pagesFillRect(int x, int y, int w, int h, bool on) { display.fillRect(x, y, w, h, on ? 1 : 0); }
void pagesDrawRect(int x, int y, int w, int h, bool on) { display.drawRect(x, y, w, h, on ? 1 : 0); }
void pagesDrawLine(int x0, int y0, int x1, int y1, bool on) { display.drawLine(x0, y0, x1, y1, on ? 1 : 0); }
void pagesDrawCircle(int x, int y, int r, bool on) { display.drawCircle(x, y, r, on ? 1 : 0); }
void pagesFillCircle(int x, int y, int r, bool on) { display.fillCircle(x, y, r, on ? 1 : 0); }
void pagesFillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, bool on) { display.fillTriangle(x0, y0, x1, y1, x2, y2, on ? 1 : 0); }
static uint32_t rgb565(uint16_t c) { uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31; return ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 31); }
void pagesIcon4(int x, int y, int size, const uint16_t* colors, const uint8_t* alphas, const uint8_t* pixels, bool onInk) {
    for (int j = 0; j < size; j++) for (int i = 0; i < size; i++) {
        const int k = j * size + i; const uint8_t b = pixels[k >> 1]; const uint8_t v = (k & 1) ? (b & 0x0F) : (b >> 4);
        if (!v) continue;
        const int X = x + i, Y = y + j; if (X < 0 || X >= SW || Y < 0 || Y >= SH) continue;
        const uint32_t c = rgb565(colors[v - 1]), u = onInk ? curInk : display.px[Y][X]; const uint32_t a = alphas[v - 1];
        auto mix = [&](int sh) { return (((c >> sh) & 255) * a + ((u >> sh) & 255) * (255 - a)) / 255; };
        display.px[Y][X] = (mix(16) << 16) | (mix(8) << 8) | mix(0);
    }
}
void pagesText(int x, int y, const char* text, uint8_t size, bool inverted) {
    display.setTextSize(size);
    if (inverted) display.setTextColor(0, 1); else display.setTextColor(1);
    display.setCursor(x, y);
    display.print(text);
}

// ---------- lo que esperan las funciones copiadas ----------
#define DEG_C "\xF7" "C"
struct Loc { uint32_t age() const { return 400; } bool v = true; double la = 37.3891, lo = -5.9845; bool isValid() const { return v; } double lat() const { return la; } double lng() const { return lo; } };
struct GTime { bool isValid() const { return true; } uint32_t age() const { return 500; } int hour() const { return 13; } int minute() const { return 30; } int second() const { return 0; } };
struct GVal { double v; bool ok = true; bool isValid() const { return ok; } bool isUpdated() const { return true; } uint32_t age() const { return 500; } double value() const { return v; } double meters() const { return v; } double feet() const { return v * 3.28084; } double kmph() const { return v; } double knots() const { return v / 1.852; } double mph() const { return v / 1.609; } double deg() const { return v; } double hdop() const { return v; } int hour() const { return 13; } int minute() const { return 30; } int second() const { return 0; } int year() const { return 2026; } int month() const { return 11; } int day() const { return 19; } };
struct Gps { Loc location; GTime time; GVal altitude{187}, satellites{11}, hdop{0.9}, speed{42}, course{118}, date{0}; uint32_t passedChecksum() { return 5321; } uint32_t failedChecksum() { return 2; } uint32_t charsProcessed() { return 812345; } uint32_t sentencesWithFix() { return 4011; } } gps;
bool gpsIsActive = true;
struct LoraTypeS { long fskFrequency = 0; float fskBitRate = 9.6f, fskFreqDev = 3.0f; int iaruRegion = 1; long frequency = 433775000; int spreadingFactor = 12; long signalBandwidth = 125000; int codingRate4 = 5; int power = 20; bool ais = false, fsk = false; };
struct BeaconS { String callsign = "EA7JRS-7", symbol = "[", overlay = "/", comment = "APRS LoRa+", micE = "", status = ""; bool smartBeaconActive = true; int smartBeaconSetting = 2; };
typedef LoraTypeS LoraType;
enum { MODE_APRS_LORA = 0, MODE_APRS_FSK = 1, MODE_MORSE = 2, MODE_WSPR = 3 };
struct ConfigS { int operatingMode = 0; struct { int wpm = 18, keyPin = 4, squelchDbm = -110, dahPin = -1; } cw; struct { int powerDbm = 20; long freqHz = 432300000; int intervalMin = 10; } wspr; struct { bool enabled = true; int source = 0; } rotor; int nonSmartBeaconRate = 15; LoraTypeS loraTypes[4]; BeaconS beacons[3]; String path = "WIDE1-1"; } Config;
BeaconS* currentBeacon = &Config.beacons[0]; LoraTypeS* currentLoRaType = &Config.loraTypes[0];
int loraIndex = 0; bool smartBeaconActive = true, sendUpdate = false, beaconManualMode = false; uint32_t txInterval = 180000, lastTxTime = 0;
namespace Utils { const char* getMaidenheadLocator(double lat, double lon, int n) { static char b[12]; double lo = lon + 180, la = lat + 90; b[0] = 'A' + (int)(lo / 20); b[1] = 'A' + (int)(la / 10); b[2] = '0' + (int)fmod(lo / 2, 10); b[3] = '0' + (int)fmod(la, 10); b[4] = 'a' + (int)(fmod(lo, 2) * 12); b[5] = 'a' + (int)(fmod(la, 1) * 24); b[6] = 0; return b; } }
namespace MSG_Utils { int getNumWLNKMails() { return 0; } int getNumAPRSMessages() { return 2; } }
namespace LoRa_Utils { long liveFreqHz() { return 433775000L; } bool isAisActive() { return false; } int getPower() { return 20; } bool fskAvailable() { return true; } }
namespace MAG_Utils { float heading() { return 118; } bool isHeadingValid() { return true; } bool isReady() { return true; } float getHeading() { return 118; } bool calibrated() { return true; } }
namespace WSPR { const int SYMBOLS = 162; int nearestValidPower(int p) { return p; } }
namespace CWMorse { struct Envelope { static constexpr float MIN_SNR_DB = 6; }; }
namespace RotorFollow { enum : int { SRC_SUN = 6, SRC_MOON = 7 }; }
bool keyboardConnected = false;
namespace GPS_Utils { bool hasLastPosition() { return false; } double getLastLat() { return 0; } double getLastLng() { return 0; } float spd = 42, alt = 187, crs = 118; float getSpeedKmph() { return spd; } float getAltitudeMeters() { return alt; } float getCourseDeg() { return crs; } bool isMoving() { return true; } bool ppsLocked() { return true; } }
namespace TIME_Utils { enum { SRC_NONE, SRC_GPS, SRC_NTP, SRC_DAPNET }; int source() { return SRC_GPS; } const char* sourceName() { return "GPS"; } }
namespace RTC_Utils { bool isReady() { return true; } }
namespace ALERTS {
    plotter::SpaceWx sw; float ssn = -1;
    const plotter::SpaceWx& space() { return sw; }
    bool hasSpace() { return true; }
    uint32_t spaceAgeS() { return 600; }
    float spaceSsn() { return ssn; }
    float hp = 3.3f; float spaceHp() { return hp; }
    float spaceGeomag(bool* isHp = nullptr) { if (isHp) *isHp = true; return hp; }
}
namespace WX_Utils {
    struct Reading { float tempC = 14.2f, humidity = 71, pressureHpa = 1012.4f; bool hasHumidity = true, hasPressure = true, hasGas = false; };
    Reading cur; bool have = true;
    void refreshForScreen() {}
    bool getReading(Reading& r) { r = cur; return have; }
}
namespace NET_WX {
    Data nd; bool have = true; float d3 = -1.6f; bool haveD3 = true;
    bool get(Data& out) { out = nd; return have; }
    bool delta3h(float& h) { h = d3; return have && haveD3; }
    uint32_t ageS() { return 420; }
    const char* owmAqiName(int a) { static const char* n[] = {"", "Good", "Fair", "Moderate", "Poor", "Very poor"}; return n[a < 0 || a > 5 ? 0 : a]; }
    const char* usAqiName(int) { return "Good"; }
    bool forecast(char* out, size_t n, float* d3o) {
        Data d; if (!get(d) || !d.hasP) return false;
        float delta = 0; const bool hd = delta3h(delta);
        const float hum = d.hasH && d.hasT ? d.humidity : -1.0f;
        const float dew = hum >= 0 ? PAGES_Core::dewPointC(d.tempC, d.humidity) : 0.0f;
        PAGES_Core::ForecastExtra ex;
        if (d.hasWind) { ex.windDeg = d.windDeg; ex.windMs = d.windMs; }
        if (d.hasClouds) ex.clouds = d.clouds;
        if (d.hasRain) ex.rainMmH = d.rainMmH;
        PAGES_Core::forecastText(d.pressureHpa, hd, hd ? PAGES_Core::PressureTrend::classifyDelta(delta) : PAGES_Core::TREND_UNKNOWN,
                                 d.hasT ? d.tempC : 15.0f, hum, dew, ex, out, n, hd ? delta : NAN);
        if (d3o) *d3o = hd ? delta : NAN;
        return true;
    }
}

using namespace PAGES_Core;
namespace {
    enum Page : uint8_t { PAGE_MAIN, PAGE_MSG, PAGE_CW, PAGE_CWB, PAGE_WSPR, PAGE_LRF, PAGE_DAP, PAGE_SAT, PAGE_ROT, PAGE_LAST, PAGE_LIST, PAGE_GPS, PAGE_COMPASS, PAGE_ALT, PAGE_WX, PAGE_CLOCK, PAGE_LINK, PAGE_BOARD, PAGE_TEAM, PAGE_FM, PAGE_BCAST, PAGE_AIS, PAGE_COUNT };
    const char* const PAGE_TITLES[PAGE_COUNT] = {
        "MAIN", "MESSAGES", "MORSE", "CW BEACON", "WSPR", "LR-FHSS", "DAPNET", "SATELLITE", "ROTOR", "LAST APRS", "STATIONS", "GPS", "COMPASS", "ALTIMETER", "TELEMETRY", "CLOCK", "LORA", "SYSTEM", "GROUP", "FM", "BCAST", "AIS"
    };
    enum SosState : uint8_t { SOS_IDLE, SOS_COUNTDOWN, SOS_ACTIVE };
    uint8_t sosState = SOS_IDLE;
    const int PW = PAGES_W, XR = PAGES_W - 128, COLS = PAGES_W / 6, ROW_Y0 = 11, ROW_PITCH = PAGES_EXTRA ? 10 : 9, ROW_HINT_Y = ROW_Y0 + 5 * ROW_PITCH;
    const uint8_t MIRROR_ROWS = 8;
    char mirrorRows[MIRROR_ROWS][28] = {}, mirrorTitle[24] = "", mirrorRight[12] = "";
    int rowLimit = COLS, rowDy = 0;
    const char* batteryText() { return "87%"; }
    HeardList heard; PressureTrend pressureTrend; IaqEstimator iaq;
    float frostT18 = 0, frostTd18 = 0; time_t frostSampleT = 0;
    bool listBrowse = false; uint8_t listCursor = 0; char listCursorCall[11] = "";
    uint8_t listSort = SORT_RECENT, listFilter = FILTER_ALL, unitSystem = UNITS_METRIC;
    uint8_t zoneMode = ZONE_AUTO_EU; int8_t zoneHalfHours = 0; bool clock24h = true; uint32_t lastGpsTimeMs = 0;
    uint8_t clockView = 0, page = PAGE_CLOCK;
    struct Item { const char* label; int cmd; };
    uint8_t submenuSel = 0;
    // menu de SUN / MOON como lo arma itemsFor / labelOf con el rotor activado
    const char* clkLabels[5];
    const Item* itemsFor(uint8_t, uint8_t& count) {
        static Item it[5];
        const bool moon = clockView == 2, on = Config.rotor.source == (moon ? 7 : 6);
        clkLabels[0] = on ? (moon ? "Rotor: stop Moon" : "Rotor: stop Sun") : (moon ? "Rotor to Moon" : "Rotor to Sun");
        clkLabels[1] = "Zone"; clkLabels[2] = "Format: 24h"; clkLabels[3] = "Sync from GPS"; clkLabels[4] = "Back";
        for (int i = 0; i < 5; i++) it[i] = Item{clkLabels[i], i};
        count = 5; return it;
    }
    const char* labelOf(const Item& i) { return i.label; }
    // --- mas paginas (2026-11-24) ---
#ifdef OLED
    const int APRS_ICON_PX = APRS_ICON_SIZE;
#else
    const int APRS_ICON_PX = 40;
#endif
    AirtimeTracker airtime; uint32_t rxCount = 148, txCount = 37, digiCount = 6;
    bool cwRxActive = false; int cwRxWpm = 20; float cwRxDbm = -92, cwRxSnr = 14; String cwText = "CQ CQ DE EA7JRS EA7JRS K"; char cwPending[12] = "";
    const char* cwModeLabel() { return "IAMB"; } int cwPin(int p) { return p; }
    void cwSMeter(float dbm, char* out, size_t n) { sMeterText((int)dbm, out, n); }
    int wsprProgress = -1; bool wsprOn = true; char wsprStatus[24] = ""; uint32_t wsprNextAt = 0, wsprTxCount = 14;
    void wsprCall(char* c) { strcpy(c, "EA7JRS"); } void wsprGrid(char* g) { strcpy(g, "IM77"); } int wsprCorrHz() { return -12; }
    bool wsprMsOfDay(uint32_t& ms) { ms = (13 * 3600 + 30 * 60 + 12) * 1000UL; return true; } unsigned wsprInterval() { return 10; }
    void gnssTags(char* out, size_t size) { snprintf(out, size, "GPS GAL BDS"); }
    void drawGnss() {}
    uint8_t gpsView = 0, coordFormat = COORD_DD, lastIndex = 0, lastView = 0; uint32_t gpsOffSinceMs = 0, lastRxMs = 0; char lastRxCall[11] = "";
    enum MsgMode : uint8_t { MM_LIST, MM_BROWSE, MM_DETAIL };
    MessageLog msgLog; uint8_t msgMode = MM_LIST, msgCursor = 0;
    bool vsStopped = false; uint32_t sosStartMs = 0; const uint32_t SOS_COUNTDOWN_MS = 5000;
    bool northTrue = true; uint8_t compassMode = 0, altSpanMode = 0;
    HeadingFusion fusion; AltProfile altProfile;
    bool compassTarget = true; char targetCall[11] = "EA7JRS-9";
    float currentDeclination(bool* fromModel = nullptr) { if (fromModel) *fromModel = true; return -0.6f; }
#include "extracted.inc"
}

// ---------- PNG ----------
#include <zlib.h>
static void png(const char* path, int scale) {
    const int W = SW * scale + 8 * scale, H = SH * scale + 8 * scale;      // marco oscuro alrededor
    std::vector<uint8_t> raw;
    for (int y = 0; y < H; y++) {
        raw.push_back(0);
        for (int x = 0; x < W; x++) {
            const int X = x / scale - 4, Y = y / scale - 4;
            uint8_t r = 0x10, g = 0x12, b = 0x16;
            if (X >= 0 && X < SW && Y >= 0 && Y < SH) { uint32_t c = display.px[Y][X]; r = c >> 16; g = (c >> 8) & 255; b = c & 255; }
            raw.push_back(r); raw.push_back(g); raw.push_back(b);
        }
    }
    uLongf zl = compressBound(raw.size()); std::vector<uint8_t> z(zl);
    compress2(z.data(), &zl, raw.data(), raw.size(), 9); z.resize(zl);
    FILE* f = fopen(path, "wb");
    auto be = [&](uint32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(b, 1, 4, f); };
    auto chunk = [&](const char* t, const std::vector<uint8_t>& d) {
        be(d.size()); fwrite(t, 1, 4, f); if (!d.empty()) fwrite(d.data(), 1, d.size(), f);
        uLong c = crc32(0, (const Bytef*)t, 4); if (!d.empty()) c = crc32(c, d.data(), d.size()); be(c);
    };
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10}; fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ih = {(uint8_t)(W >> 24), (uint8_t)(W >> 16), (uint8_t)(W >> 8), (uint8_t)W, (uint8_t)(H >> 24), (uint8_t)(H >> 16), (uint8_t)(H >> 8), (uint8_t)H, 8, 2, 0, 0, 0};
    chunk("IHDR", ih); chunk("IDAT", z); chunk("IEND", {});
    fclose(f);
}

static void shot(const char* name, void (*fn)()) {
    display.clear();
    fn();
    char p[256]; snprintf(p, sizeof(p), "%s/%s.png", getenv("OUTDIR") ? getenv("OUTDIR") : "out", name);
    png(p, 4);
    printf("%s\n", p);
}

int main(int argc, char** argv) {
    fakeMillis = 3000 * 1000;
    const char* t = argc > 1 ? argv[1] : "2026-11-19T13:30:00";
    struct tm m = {}; strptime(t, "%Y-%m-%dT%H:%M:%S", &m); fakeNow = timegm(&m);
    // estaciones de ejemplo
    const uint32_t base = fakeMillis;
    heard.update("EA7URS-10", '#', 0, true, 37.20f, -5.80f, 412, 0, 0, -101, 4.5f, base - 60000, "APLRG1", "", "Digi URS Sevilla");
    heard.update("EA7JRS-9", '>', 0, true, 37.41f, -5.95f, 45, 40, 90, -88, 9.0f, base - 300000, "APLJRT", "WIDE1-1", "APRS LoRa+ en ruta");
    heard.update("EB7ABC-7", '[', 0, true, 36.53f, -6.29f, 0, 0, 0, -115, -6.0f, base - 900000);
    heard.update("ED7ZAB-3", '#', 0, true, 37.89f, -4.78f, 0, 0, 0, -97, 2.0f, base - 1800000);
    heard.update("EA7XYZ", '-', 0, true, 37.39f, -5.99f, 0, 0, 0, -60, 11.0f, base - 3600000 * 2);
    // tiempo de internet y espacial
    NET_WX::Data& d = NET_WX::nd; memset(&d, 0, sizeof(d));
    strcpy(d.src, "OWM"); strcpy(d.place, "Sevilla"); d.hasT = d.hasH = d.hasP = d.hasWind = true; d.tempC = 15.1f; d.humidity = 76; d.pressureHpa = 1011.0f;
    d.windMs = 6.2f; d.windDeg = 230; d.hasClouds = true; d.clouds = 75; d.aqiOwm = 2; d.aqiUs = -1;
    plotter::SpaceWx& w = ALERTS::sw; w.g = 1; w.r = 0; w.s = 0; float kp[8] = {2.3f, 3.0f, 3.7f, 4.3f, 5.0f, 4.0f, 3.3f, 2.7f};
    for (int i = 0; i < 8; i++) w.kp[i] = kp[i]; w.kpCount = 8; w.sfi = 152; w.bz = -4.2f; w.bt = 6; w.windKms = 487; w.xray = "C3.1";
    ALERTS::ssn = 118;
    if (getenv("STORM")) { w.g = 4; w.r = 3; w.s = 2; w.xray = "X1.4"; float k2[8] = {3.3f, 4.7f, 5.3f, 6.0f, 7.0f, 7.7f, 8.7f, 9.0f}; for (int i = 0; i < 8; i++) w.kp[i] = k2[i]; w.windKms = 812; w.bz = -18; w.bt = 24; }
    if (getenv("MOD")) { w.g = 2; w.r = 1; w.s = 0; w.xray = "M2.4"; float k2[8] = {2.7f, 3.3f, 4.0f, 5.0f, 5.7f, 6.3f, 5.0f, 4.3f}; for (int i = 0; i < 8; i++) w.kp[i] = k2[i]; }
    if (getenv("LIGHT")) { PAL = PALL; display.clear(); }
    const char* which = argc > 2 ? argv[2] : "all";
    Config.rotor.source = argc > 3 ? atoi(argv[3]) : 0;
    auto want = [&](const char* n) { return !strcmp(which, "all") || strstr(which, n); };
    // mas paginas: datos de ejemplo
    msgLog.addReceived("EA7JRS-9", "Llegando al refugio, QRV en 20 min", (uint32_t)fakeNow - 600);
    msgLog.addSent("EA7JRS-9", "Recibido, te espero en 433.775", (uint32_t)fakeNow - 420, 12); msgLog.markAcked("EA7JRS-9", 12);
    msgLog.addReceived("EB7ABC-7", "73 y buena ruta", (uint32_t)fakeNow - 180);
    msgLog.addSent("EA7URS-10", "Prueba de mensajes APRS LoRa+", (uint32_t)fakeNow - 60, 13);
    for (int i = 0; i < 90; i++) altProfile.add(140.0f + 55.0f * sinf(i / 14.0f) + i * 0.6f + 6.0f * sinf(i / 3.0f), (uint32_t)i * 10000u);
    for (int i = 0; i < 40; i++) fusion.update(0.1f, false, 0, true, 118.0f, true, 118.0f, 42.0f);
    if (want("messages")) shot("messages", [] { msgMode = MM_LIST; drawMessages(); });
    if (want("msgdetail")) shot("message", [] { msgMode = MM_DETAIL; msgCursor = 3; drawMessages(); msgMode = MM_LIST; msgCursor = 0; });
    if (want("alt")) shot("altimeter", [] { drawAltimeter(); });
    if (want("compass")) shot("compass", [] { drawCompass(); });
    if (want("sos")) { sosStartMs = fakeMillis - 1800; shot("sos", [] { drawSosCountdown(); }); }
    if (want("list"))  shot("stations", [] { drawList(); });
    if (want("clock")) shot("clock", [] { clockView = 0; drawClock(); });
    if (want("sun"))   shot("sun", [] { clockView = 1; drawSun(); });
    if (want("moon"))  shot("moon", [] { clockView = 2; drawMoon(); });
    if (want("wx"))    { const uint32_t k = fakeMillis; fakeMillis = 0; shot("telemetry", [] { drawWxSensors(); }); fakeMillis = k; }
    if (want("cmoon")) { const uint32_t k = fakeMillis; fakeMillis = 4000; shot("clock_moon", [] { clockView = 0; drawClock(); }); fakeMillis = k; }
    if (want("sel"))   { listBrowse = true; listCursor = 1; shot("stations_sel", [] { drawList(); }); listBrowse = false; }
    if (want("netwx")) shot("telemetry_net", [] { drawNetWx(NET_WX::nd); });
    if (want("space")) shot("spacewx", [] { drawSpaceView(); });
    if (want("hf"))    shot("hf", [] { drawHfView(); });
    if (want("menusun"))  shot("menu_sun", [] { clockView = 1; drawSun(); drawSubmenu(); });
    if (want("menumoon")) shot("menu_moon", [] { clockView = 2; drawMoon(); drawSubmenu(); });
    // --- mas paginas ---
    {
        const uint32_t e = (uint32_t)fakeNow;
        msgLog.addReceived("EA7JRS-9", "Llego en 20 min, QRV en 145.500", e - 5400);
        msgLog.addSent("EA7URS-10", "Recibido, te espero en el puerto", e - 4800, 12); msgLog.markAcked("EA7URS-10", 12);
        msgLog.addReceived("EB7ABC-7", "CQ CQ de EB7ABC portable IM76", e - 1500);
        msgLog.addSent("EA7JRS-9", "Baliza recibida 5/9", e - 600, 13);
        msgLog.addReceived("ED7ZAB-3", "Repetidor en pruebas hoy", e - 120);
        for (int i = 0; i < 90; i++) altProfile.add(140.0f + 60.0f * sinf(i / 14.0f) + i * 0.8f, (uint32_t)i * 10000);
        for (int i = 0; i < 20; i++) fusion.update(0.5f, false, 0, true, 118, true, 118, 42);
    }
    if (want("msg"))   shot("messages", [] { msgMode = MM_LIST; drawMessages(); });
    if (want("msgd"))  shot("message_detail", [] { msgMode = MM_DETAIL; msgCursor = 2; drawMessages(); msgMode = MM_LIST; msgCursor = 0; });
    if (want("alt"))   shot("altimeter", [] { drawAltimeter(); });
    if (want("comp"))  shot("compass", [] { drawCompass(); });
    if (want("sos"))   shot("sos", [] { sosState = SOS_COUNTDOWN; sosStartMs = fakeMillis - 2000; drawSosCountdown(); sosState = SOS_IDLE; });
    if (want("eme"))   shot("eme", [] { drawEme(); });
    strcpy(lastRxCall, "EA7URS-10"); lastRxMs = fakeMillis - 60000; lastTxTime = fakeMillis - 70000;
    airtime.addTx(fakeMillis - 600000, 9200); airtime.addTx(fakeMillis - 120000, 4100); airtime.addRx(fakeMillis - 60000, 61000);
    if (want("main"))  { msgLog.markAllRead(); shot("main", [] { drawMain(); }); }
    if (want("lora"))  shot("lora", [] { drawLink(); });
    if (want("morse")) shot("morse", [] { drawCwPage(); });
    if (want("morserx")) { cwRxActive = true; cwText = "CQ CQ DE EA5XYZ EA5XYZ PSE K"; strcpy(cwPending, "EA"); shot("morse_rx", [] { drawCwPage(); }); cwRxActive = false; cwPending[0] = 0; }
    if (want("wspr"))  { wsprNextAt = fakeMillis + 228000; shot("wspr", [] { drawWsprPage(); }); }
    if (want("wsprtx")) { wsprProgress = 97; shot("wspr_tx", [] { drawWsprPage(); }); wsprProgress = -1; }
    if (want("gps"))   shot("gps", [] { gpsView = 0; drawGps(); });
    if (want("last"))  shot("last_aprs", [] { lastIndex = 1; lastView = 0; drawLast(); });
    if (want("last2")) shot("last_aprs_pos", [] { lastIndex = 1; lastView = 1; drawLast(); lastView = 0; });
    return 0;
}
