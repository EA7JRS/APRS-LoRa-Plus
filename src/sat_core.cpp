/*
 * Nucleo de calculo de satelites (modo SAT). Ver include/sat_core.h
 *
 * Copyright (c) 2026 Manuel - EA7JRS. GPL-3.0-or-later.
 */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "sat_core.h"
#include "sat_math.h"
#define pow satPow
#define fmod satFmod
#define asin satAsin
#define acos satAcos
#define tan satTan

namespace SatCore {

    namespace {

        const double PI_        = 3.14159265358979323846;
        const double DEG2RAD    = PI_ / 180.0;
        const double RAD2DEG    = 180.0 / PI_;
        const double WGS84_A    = 6378.137;                      // km
        const double WGS84_F    = 1.0 / 298.257223563;
        const double WGS84_E2   = WGS84_F * (2.0 - WGS84_F);
        const double OMEGA_E    = 7.2921150e-5;                  // rad/s
        const double C_KMS      = 299792.458;

        // Campo de ancho fijo -> double (sin sscanf: newlib-nano del ESP32 puede no tener formato de float)
        double field(const char* s, int from, int len) {
            char tmp[24];
            if (len > 22) len = 22;
            memcpy(tmp, s + from, len);
            tmp[len] = '\0';
            return strtod(tmp, nullptr);
        }

        bool checksumOk(const char* line) {
            int sum = 0;
            for (int i = 0; i < 68; i++) {
                const char c = line[i];
                if (c >= '0' && c <= '9') sum += c - '0';
                else if (c == '-') sum += 1;
            }
            const char last = line[68];
            return last >= '0' && last <= '9' && (sum % 10) == (last - '0');
        }

        // Campo con punto decimal implicito y exponente: " 28098-4" / "-11606-4" -> 0.28098e-4
        double implied(const char* s, int from) {                // 8 caracteres: signo, 5 digitos, signo del exponente, exponente
            char mant[8];
            mant[0] = s[from];
            memcpy(mant + 1, s + from + 1, 5);
            mant[6] = '\0';
            double m = strtod(mant, nullptr) / 100000.0;
            const int  e = (int)field(s, from + 6, 2);           // "-4" / "+0" / " 0"
            return m * pow(10.0, e);
        }

        struct Vec3 { double x, y, z; };
        inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        inline double norm(const Vec3& a) { return sqrt(dot(a, a)); }

        Vec3 observerEcef(const Observer& o) {
            const double lat = o.latDeg * DEG2RAD, lon = o.lonDeg * DEG2RAD, h = o.altM / 1000.0;
            const double s = sin(lat), c = cos(lat);
            const double N = WGS84_A / sqrt(1.0 - WGS84_E2 * s * s);
            return Vec3{ (N + h) * c * cos(lon), (N + h) * c * sin(lon), (N * (1.0 - WGS84_E2) + h) * s };
        }

        inline double jdFromUnix(double unixTime) { return unixTime / 86400.0 + 2440587.5; }

    }

    double unixFromCivil(int y, int m, int d, int hh, int mm, double ss) {
        // dias desde 1970-01-01 (algoritmo de H. Hinnant)
        y -= m <= 2;
        const long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        const long days = era * 146097 + (long)doe - 719468;
        return days * 86400.0 + hh * 3600.0 + mm * 60.0 + ss;
    }

    bool parseTle(const char* name, const char* l1, const char* l2, Sat& out) {
        memset(&out, 0, sizeof(out));
        if (l1 == nullptr || l2 == nullptr) return false;
        if (strlen(l1) < 69 || strlen(l2) < 69) return false;
        if (l1[0] != '1' || l2[0] != '2') return false;
        if (!checksumOk(l1) || !checksumOk(l2)) return false;
        if (strncmp(l1 + 2, l2 + 2, 5) != 0) return false;                     // mismo numero de catalogo

        if (name != nullptr) {
            strncpy(out.name, name, sizeof(out.name) - 1);
            int n = (int)strlen(out.name);
            while (n > 0 && (out.name[n - 1] == ' ' || out.name[n - 1] == '\r' || out.name[n - 1] == '\n')) out.name[--n] = '\0';
        }
        char cat[6];
        memcpy(cat, l1 + 2, 5);
        cat[5] = '\0';
        out.noradId = atoi(cat);

        const int    epochYr   = (int)field(l1, 18, 2);
        const double epochDays = field(l1, 20, 12);
        const double ndot      = field(l1, 33, 10);                           // rev/dia^2 (el TLE ya trae el 1/2 del convenio)
        const double nddot     = implied(l1, 44);
        const double bstar     = implied(l1, 53);

        const double inc   = field(l2, 8, 8);
        const double raan  = field(l2, 17, 8);
        const double ecc   = field(l2, 26, 7) / 1.0e7;
        const double argp  = field(l2, 34, 8);
        const double mo    = field(l2, 43, 8);
        const double nrev  = field(l2, 52, 11);

        if (epochDays < 1.0 || epochDays >= 367.0 || nrev <= 0.0 || ecc < 0.0 || ecc >= 1.0) return false;

        const int year = epochYr < 57 ? 2000 + epochYr : 1900 + epochYr;
        double jd = 0, jdF = 0;
        SGP4Funcs::jday_SGP4(year, 1, 1, 0, 0, 0.0, jd, jdF);
        jdF += epochDays - 1.0;

        elsetrec& r = out.rec;
        r.epochyr     = epochYr;
        r.epochdays   = epochDays;
        r.jdsatepoch  = jd;
        r.jdsatepochF = jdF;
        strncpy(r.satnum, cat, sizeof(r.satnum) - 1);

        const double xpdotp = 1440.0 / (2.0 * PI_);                             // rev/dia -> rad/min
        const double noKozai = nrev / xpdotp;
        const double ndotU   = ndot / (xpdotp * 1440.0);
        const double nddotU  = nddot / (xpdotp * 1440.0 * 1440.0);

        if (!SGP4Funcs::sgp4init(wgs72, 'i', cat, (jd + jdF) - 2433281.5, bstar, ndotU, nddotU, ecc,
                                 argp * DEG2RAD, inc * DEG2RAD, mo * DEG2RAD, noKozai, raan * DEG2RAD, r)) return false;
        out.epochUnix = (jd + jdF - 2440587.5) * 86400.0;
        out.valid     = true;
        return true;
    }

    double tleAgeDays(const Sat& s, double nowUnix) {
        return (nowUnix - s.epochUnix) / 86400.0;
    }

    bool look(Sat& s, const Observer& obs, double unixTime, Look& out) {
        if (!s.valid) return false;
        const double jd     = jdFromUnix(unixTime);
        const double tsince = (jd - (s.rec.jdsatepoch + s.rec.jdsatepochF)) * 1440.0;
        double r[3], v[3];
        if (!SGP4Funcs::sgp4(s.rec, tsince, r, v)) return false;
        if (s.rec.error != 0) return false;

        // TEME -> ECEF (giro con el tiempo sidereo de Greenwich; sin movimiento polar ni UT1-UTC: errores de segundos de arco)
        const double th = SGP4Funcs::gstime_SGP4(jd), c = cos(th), sn = sin(th);
        const Vec3 re{ c * r[0] + sn * r[1], -sn * r[0] + c * r[1], r[2] };
        const Vec3 vr{ c * v[0] + sn * v[1], -sn * v[0] + c * v[1], v[2] };
        const Vec3 ve{ vr.x + OMEGA_E * re.y, vr.y - OMEGA_E * re.x, vr.z };    // velocidad respecto a la Tierra que gira

        const Vec3 o   = observerEcef(obs);
        const Vec3 rho{ re.x - o.x, re.y - o.y, re.z - o.z };
        const double range = norm(rho);

        const double lat = obs.latDeg * DEG2RAD, lon = obs.lonDeg * DEG2RAD;
        const double sl = sin(lat), cl = cos(lat), so = sin(lon), co = cos(lon);
        const double east  = -so * rho.x + co * rho.y;
        const double north = -sl * co * rho.x - sl * so * rho.y + cl * rho.z;
        const double up    =  cl * co * rho.x + cl * so * rho.y + sl * rho.z;

        out.azDeg   = atan2(east, north) * RAD2DEG;
        if (out.azDeg < 0) out.azDeg += 360.0;
        out.elDeg   = asin(up / range) * RAD2DEG;
        out.rangeKm = range;
        out.rangeRateKmS = dot(rho, ve) / range;

        // punto subsatelite (Bowring iterativo)
        const double p = sqrt(re.x * re.x + re.y * re.y);
        double phi = atan2(re.z, p * (1.0 - WGS84_E2)), h = 0;
        for (int i = 0; i < 5; i++) {
            const double sp = sin(phi), N = WGS84_A / sqrt(1.0 - WGS84_E2 * sp * sp);
            h   = p / cos(phi) - N;
            phi = atan2(re.z, p * (1.0 - WGS84_E2 * N / (N + h)));
        }
        out.subLatDeg = phi * RAD2DEG;
        out.subLonDeg = atan2(re.y, re.x) * RAD2DEG;
        out.altKm     = h;
        return true;
    }

    double dopplerDown(double f, double rr) { return f * (1.0 - rr / C_KMS); }
    double dopplerUpPre(double f, double rr) { return f / (1.0 - rr / C_KMS); }

    const char* cardinal(double az) {
        static const char* N8[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        int i = (int)floor((az + 22.5) / 45.0) % 8;
        if (i < 0) i += 8;
        return N8[i];
    }

    // ---------------------------------------------------------------- archivo de datos

    namespace {
        // Siguiente campo separado por '|' (hasta el final de la linea): copia en dst y devuelve el puntero tras el separador (o nullptr)
        const char* nextField(const char* p, const char* end, char* dst, size_t cap) {
            size_t n = 0;
            while (p < end && *p != '|' && *p != '\n' && *p != '\r') { if (n + 1 < cap) dst[n++] = *p; p++; }
            dst[n] = '\0';
            if (p < end && *p == '|') return p + 1;
            return nullptr;
        }
    }

    bool parseTxLine(const char* line, Transmitter& t) {
        memset(&t, 0, sizeof(t));
        if (line[0] != 'T' || line[1] != '|') return false;
        const char* end = line + strlen(line);
        const char* p = line + 2;
        char f[40];
        long v[4];
        for (int i = 0; i < 4; i++) {
            p = nextField(p, end, f, sizeof(f));
            v[i] = atol(f);
            if (p == nullptr) return false;
        }
        t.upLo = v[0]; t.upHi = v[1] < v[0] ? v[0] : v[1]; t.dnLo = v[2]; t.dnHi = v[3] < v[2] ? v[2] : v[3];
        p = nextField(p, end, t.mode, sizeof(t.mode));  if (!p) return false;
        p = nextField(p, end, f, sizeof(f));            if (!p) return false;
        t.baud = atoi(f);
        p = nextField(p, end, f, sizeof(f));            if (!p) return false;
        t.invert = f[0] == '1';
        nextField(p, end, t.desc, sizeof(t.desc));
        return true;
    }

    long txUpCenter(const Transmitter& t)   { return t.upLo ? (t.upLo + t.upHi) / 2 : 0; }
    long txDownCenter(const Transmitter& t) { return t.dnLo ? (t.dnLo + t.dnHi) / 2 : 0; }

    bool txIn70cm(const Transmitter& t) {
        return (t.upLo >= 420000000L && t.upLo <= 450000000L) || (t.dnLo >= 420000000L && t.dnLo <= 450000000L);
    }

    size_t parseBlock(const char* text, size_t len, Block& out) {
        memset(&out, 0, sizeof(out));
        size_t i = 0;
        // salta comentarios y lineas vacias hasta "S|"
        while (i < len) {
            if (text[i] == 'S' && i + 1 < len && text[i + 1] == '|') break;
            while (i < len && text[i] != '\n') i++;
            if (i < len) i++;
        }
        if (i >= len) return 0;
        const size_t start = i;
        const char* end = text + len;
        const char* p = text + i + 2;
        char f[16];
        p = nextField(p, end, f, sizeof(f));
        out.noradId = atoi(f);
        if (p) nextField(p, end, out.name, sizeof(out.name));
        while (i < len && text[i] != '\n') i++;
        if (i < len) i++;
        // lineas del bloque hasta la proxima "S|"
        while (i < len) {
            if (text[i] == 'S' && i + 1 < len && text[i + 1] == '|') break;
            size_t e = i;
            while (e < len && text[e] != '\n') e++;
            size_t n = e - i;
            while (n > 0 && (text[i + n - 1] == '\r' || text[i + n - 1] == ' ')) n--;
            if (n >= 69 && text[i] == '1' && text[i + 1] == ' ') { memcpy(out.line1, text + i, n > 71 ? 71 : n); }
            else if (n >= 69 && text[i] == '2' && text[i + 1] == ' ') { memcpy(out.line2, text + i, n > 71 ? 71 : n); }
            else if (n > 2 && text[i] == 'T' && text[i + 1] == '|' && out.txCount < MAX_TX) {
                char line[200];
                size_t m = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
                memcpy(line, text + i, m);
                line[m] = '\0';
                if (parseTxLine(line, out.tx[out.txCount])) out.txCount++;
            }
            i = e < len ? e + 1 : e;
        }
        return i - start;
    }

    // ---------------------------------------------------------------- pasos

    namespace {
        bool elAt(Sat& s, const Observer& o, double t, double& el, double* az = nullptr) {
            Look l;
            if (!look(s, o, t, l)) return false;
            el = l.elDeg;
            if (az) *az = l.azDeg;
            return true;
        }

        // Instante en que la elevacion cruza `level` entre t0 (lado `below0`) y t1; biseccion hasta ~0,1 s
        double refine(Sat& s, const Observer& o, double t0, double t1, double level, bool risingToward1) {
            for (int i = 0; i < 9; i++) {
                const double tm = 0.5 * (t0 + t1);
                double el = 0;
                if (!elAt(s, o, tm, el)) break;
                const bool above = el >= level;
                if (above == risingToward1) t1 = tm; else t0 = tm;
            }
            return 0.5 * (t0 + t1);
        }
    }

    void passSearchStart(PassSearch& ps, double fromUnix, double horizonSec, double minElDeg) {
        memset(&ps, 0, sizeof(ps));
        ps.from = fromUnix;
        ps.to = fromUnix + horizonSec;
        ps.minElDeg = minElDeg;
        ps.t = fromUnix;
        ps.phase = 0;
        ps.prevEl = -999.0;
        ps.stepSec = 20.0;
    }

    int passSearchAdvance(PassSearch& ps, Sat& s, const Observer& obs, int steps) {
        while (steps-- > 0 && ps.phase < 2) {
            if ((ps.phase == 0 && ps.t > ps.to + ps.stepSec) || ps.t > ps.to + 3600.0) { ps.phase = 3; break; }     // un paso empezado se termina aunque pase del horizonte
            double el = 0, az = 0;
            if (!elAt(s, obs, ps.t, el, &az)) { ps.phase = 4; break; }

            if (ps.phase == 0) {
                if (el >= ps.minElDeg) {
                    ps.pass.aosUnix = (ps.prevEl < -900.0) ? ps.t : refine(s, obs, ps.t - ps.stepSec, ps.t, ps.minElDeg, true);
                    double a = 0;
                    elAt(s, obs, ps.pass.aosUnix, a, &ps.pass.aosAzDeg);
                    ps.pass.maxElDeg = el;
                    ps.pass.tcaUnix  = ps.t;
                    ps.phase = 1;
                }
            } else if (ps.phase == 1) {
                if (el > ps.pass.maxElDeg) { ps.pass.maxElDeg = el; ps.pass.tcaUnix = ps.t; }
                if (el < ps.minElDeg) {
                    ps.pass.losUnix = refine(s, obs, ps.t - ps.stepSec, ps.t, ps.minElDeg, false);
                    double a = 0;
                    elAt(s, obs, ps.pass.losUnix, a, &ps.pass.losAzDeg);
                    // maximo exacto: ternaria alrededor del mejor muestreo
                    double lo = ps.pass.tcaUnix - ps.stepSec, hi = ps.pass.tcaUnix + ps.stepSec;
                    if (lo < ps.pass.aosUnix) lo = ps.pass.aosUnix;
                    if (hi > ps.pass.losUnix) hi = ps.pass.losUnix;
                    for (int i = 0; i < 14; i++) {
                        const double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
                        double e1 = 0, e2 = 0;
                        if (!elAt(s, obs, m1, e1) || !elAt(s, obs, m2, e2)) break;
                        if (e1 < e2) lo = m1; else hi = m2;
                    }
                    ps.pass.tcaUnix = 0.5 * (lo + hi);
                    elAt(s, obs, ps.pass.tcaUnix, ps.pass.maxElDeg);
                    ps.phase = 2;
                    break;
                }
            }
            ps.prevEl = el;
            ps.t += ps.stepSec;
        }
        return ps.phase;
    }

}
