/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "same.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace plotter {

namespace {
constexpr double kBaud = 520.0 + 5.0 / 6.0;            // 520,83 bit/s
constexpr double kMark = 2083.0 + 1.0 / 3.0;           // 1
constexpr double kSpace = 1562.5;                      // 0

struct Ev { const char* code; const char* name; uint8_t level; };
const Ev kEvents[] = {
    {"EAN", "Emergency Action Notification", 3}, {"EAT", "Emergency Action Termination", 0},
    {"NIC", "National Information Center", 0}, {"NPT", "National Periodic Test", 0},
    {"RMT", "Required Monthly Test", 0}, {"RWT", "Required Weekly Test", 0}, {"ADR", "Administrative Message", 0},
    {"DMO", "Practice/Demo Warning", 0}, {"NMN", "Network Message Notification", 0},
    {"AVW", "Avalanche Warning", 3}, {"AVA", "Avalanche Watch", 1}, {"BZW", "Blizzard Warning", 3},
    {"BLU", "Blue Alert", 1}, {"CAE", "Child Abduction Emergency", 2}, {"CDW", "Civil Danger Warning", 3},
    {"CEM", "Civil Emergency Message", 3}, {"CFW", "Coastal Flood Warning", 2}, {"CFA", "Coastal Flood Watch", 1},
    {"DSW", "Dust Storm Warning", 2}, {"EQW", "Earthquake Warning", 3}, {"EVI", "Evacuation Immediate", 3},
    {"EWW", "Extreme Wind Warning", 3}, {"FRW", "Fire Warning", 3}, {"FFW", "Flash Flood Warning", 3},
    {"FFA", "Flash Flood Watch", 1}, {"FFS", "Flash Flood Statement", 1}, {"FLW", "Flood Warning", 2},
    {"FLA", "Flood Watch", 1}, {"FLS", "Flood Statement", 1}, {"HMW", "Hazardous Materials Warning", 3},
    {"HWW", "High Wind Warning", 2}, {"HWA", "High Wind Watch", 1}, {"HUW", "Hurricane Warning", 3},
    {"HUA", "Hurricane Watch", 2}, {"HLS", "Hurricane Statement", 1}, {"LEW", "Law Enforcement Warning", 2},
    {"LAE", "Local Area Emergency", 2}, {"MEP", "Missing and Endangered Persons", 1},
    {"TOE", "911 Telephone Outage Emergency", 2}, {"NUW", "Nuclear Power Plant Warning", 3},
    {"RHW", "Radiological Hazard Warning", 3}, {"SVR", "Severe Thunderstorm Warning", 2},
    {"SVA", "Severe Thunderstorm Watch", 1}, {"SVS", "Severe Weather Statement", 1},
    {"SPW", "Shelter in Place Warning", 3}, {"SMW", "Special Marine Warning", 2},
    {"SPS", "Special Weather Statement", 1}, {"SSA", "Storm Surge Watch", 2}, {"SSW", "Storm Surge Warning", 3},
    {"TOR", "Tornado Warning", 3}, {"TOA", "Tornado Watch", 2}, {"TRW", "Tropical Storm Warning", 2},
    {"TRA", "Tropical Storm Watch", 1}, {"TSW", "Tsunami Warning", 3}, {"TSA", "Tsunami Watch", 2},
    {"VOW", "Volcano Warning", 3}, {"WSW", "Winter Storm Warning", 2}, {"WSA", "Winter Storm Watch", 1},
    {"EWA", "Extreme Wind Watch", 1}, {"FSW", "Flash Freeze Warning", 2}, {"FZW", "Freeze Warning", 1},
    {"SQW", "Snow Squall Warning", 2}, {"WFW", "Wildfire Warning", 3}, {"WFA", "Wildfire Watch", 1},
};

bool digits(const std::string& s, size_t at, size_t n) {
    if (at + n > s.size()) return false;
    for (size_t i = 0; i < n; i++) if (s[at + i] < '0' || s[at + i] > '9') return false;
    return true;
}
}  // namespace

const double kNwrMhz[7] = {162.550, 162.400, 162.475, 162.425, 162.450, 162.500, 162.525};

int nwrChannel(double mhz) {
    for (int i = 0; i < 7; i++) if (std::fabs(mhz - kNwrMhz[i]) < 0.003) return i + 1;
    return 0;
}

const char* sameEventName(const std::string& code) {
    for (const auto& e : kEvents) if (code == e.code) return e.name;
    return nullptr;
}

uint8_t sameEventLevel(const std::string& code) {
    for (const auto& e : kEvents) if (code == e.code) return e.level;
    if (code.size() == 3) {
        switch (code[2]) {
            case 'W': return 2;      // Warning
            case 'E': return 2;      // Emergency
            case 'A': return 1;      // Watch
            case 'S': return 1;      // Statement
        }
    }
    return 0;
}

const char* sameOrgName(const std::string& o) {
    if (o == "WXR") return "National Weather Service";
    if (o == "CIV") return "Civil authorities";
    if (o == "EAS") return "Broadcast station";
    if (o == "PEP") return "Primary Entry Point";
    if (o == "EAN") return "Emergency Action Notification Network";
    return "";
}

size_t sameHeaderEnd(const std::string& s) {
    if (s.compare(0, 5, "ZCZC-") != 0) return 0;
    const size_t plus = s.find('+');
    if (plus == std::string::npos) return 0;
    // +TTTT-JJJHHMM-LLLLLLLL-
    if (!digits(s, plus + 1, 4) || s.size() <= plus + 5 || s[plus + 5] != '-') return 0;
    if (!digits(s, plus + 6, 7) || s.size() <= plus + 13 || s[plus + 13] != '-') return 0;
    if (s.size() <= plus + 22 || s[plus + 22] != '-') return 0;
    return plus + 23;
}

bool sameParse(const std::string& h, SameMsg& m) {
    m = SameMsg();
    const size_t end = sameHeaderEnd(h);
    if (!end) return false;
    m.raw = h.substr(0, end);
    // ZCZC-ORG-EEE-PSSCCC-...+TTTT-JJJHHMM-LLLLLLLL-
    if (m.raw.size() < 13 || m.raw[8] != '-' || m.raw[12] != '-') return false;
    m.org = m.raw.substr(5, 3);
    m.event = m.raw.substr(9, 3);
    const size_t plus = m.raw.find('+');
    size_t p = 13;
    while (p < plus) {
        size_t e = m.raw.find_first_of("-+", p);
        if (e == std::string::npos || e > plus) e = plus;
        std::string f = m.raw.substr(p, e - p);
        if (f.size() != 6 || !digits(f, 0, 6)) return false;
        m.fips.push_back(f);
        p = e + 1;
    }
    if (m.fips.empty() || m.fips.size() > 31) return false;
    m.durationMin = std::atoi(m.raw.substr(plus + 1, 2).c_str()) * 60 + std::atoi(m.raw.substr(plus + 3, 2).c_str());
    m.julianDay = std::atoi(m.raw.substr(plus + 6, 3).c_str());
    m.hour = std::atoi(m.raw.substr(plus + 9, 2).c_str());
    m.minute = std::atoi(m.raw.substr(plus + 11, 2).c_str());
    m.sender = m.raw.substr(plus + 14, 8);
    while (!m.sender.empty() && m.sender.back() == ' ') m.sender.pop_back();
    if (m.julianDay < 1 || m.julianDay > 366 || m.hour > 23 || m.minute > 59) return false;
    for (char c : m.org + m.event) if (c < 'A' || c > 'Z') return false;
    return true;
}

Alert sameToAlert(const SameMsg& m, int64_t now) {
    Alert a;
    a.src = AlertSrc::Same;
    const char* name = sameEventName(m.event);
    a.event = name ? name : "SAME " + m.event;
    a.level = sameEventLevel(m.event);
    std::string area;
    for (size_t i = 0; i < m.fips.size() && i < 6; i++) area += (i ? " " : "") + m.fips[i];
    if (m.fips.size() > 6) area += " +" + std::to_string(m.fips.size() - 6);
    a.area = area;
    a.text = m.sender + (sameOrgName(m.org)[0] ? std::string(" (") + sameOrgName(m.org) + ")" : std::string(""));
    // ano: el de "now"; si saldria mas de medio ano en el futuro, el anterior
    int y = 1970;
    {
        int64_t days = now / 86400;
        int64_t z = days + 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = (unsigned)(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        const int mo = (int)(mp < 10 ? mp + 3 : mp - 9);
        y = (int)(yoe + era * 400 + (mo <= 2));
    }
    int64_t t = unixFromUtc(y, 1, 1, 0, 0, 0) + (int64_t)(m.julianDay - 1) * 86400 + m.hour * 3600 + m.minute * 60;
    if (now && t > now + 183LL * 86400) t = unixFromUtc(y - 1, 1, 1, 0, 0, 0) + (int64_t)(m.julianDay - 1) * 86400 + m.hour * 3600 + m.minute * 60;
    a.sent = a.onset = t;
    a.expires = t + (int64_t)m.durationMin * 60;
    // id estable: la cabecera
    a.id = "same/" + m.raw;
    return a;
}

// ---------------------------------------------------------------- demodulador
void SameDecoder::begin(float fs) {
    fs_ = fs;
    n_ = (int)std::lround(fs / kBaud);
    if (n_ < 4) n_ = 4;
    step_ = (float)(kBaud / fs);
    mwr_ = (float)std::cos(2 * kPi * kMark / fs);
    mwi_ = (float)-std::sin(2 * kPi * kMark / fs);
    swr_ = (float)std::cos(2 * kPi * kSpace / fs);
    swi_ = (float)-std::sin(2 * kPi * kSpace / fs);
    ring_.assign((size_t)n_ * 4, 0.0f);
    reset();
}

void SameDecoder::reset() {
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    idx_ = 0;
    aMI_ = aMQ_ = aSI_ = aSQ_ = 0;
    mr_ = sr_ = 1;
    mi_ = si_ = 0;
    phase_ = 0;
    prevD_ = 0;
    shift_ = 0;
    synced_ = false;
    bitCount_ = 0;
    burst_.clear();
    recent_.clear();
    out_.clear();
    emittedGroup_ = false;
}

void SameDecoder::feed(const float* x, size_t n) {
    for (size_t i = 0; i < n; i++) sample(x[i]);
    timeouts();
}

void SameDecoder::feed(const int16_t* x, size_t n) {
    for (size_t i = 0; i < n; i++) sample(x[i] * (1.0f / 32768.0f));
    timeouts();
}

bool SameDecoder::poll(std::string& header) {
    timeouts();
    if (out_.empty()) return false;
    header = out_.front();
    out_.erase(out_.begin());
    return true;
}

void SameDecoder::sample(float x) {
    if (n_ == 0) return;
    t_++;
    // osciladores (giro de un fasor; se renormaliza de vez en cuando)
    float r = mr_ * mwr_ - mi_ * mwi_;
    mi_ = mr_ * mwi_ + mi_ * mwr_;
    mr_ = r;
    r = sr_ * swr_ - si_ * swi_;
    si_ = sr_ * swi_ + si_ * swr_;
    sr_ = r;
    if (++renorm_ >= 256) {
        renorm_ = 0;
        float k = 1.0f / std::sqrt(mr_ * mr_ + mi_ * mi_);
        mr_ *= k; mi_ *= k;
        k = 1.0f / std::sqrt(sr_ * sr_ + si_ * si_);
        sr_ *= k; si_ *= k;
    }
    // correlacion de un bit (ventana deslizante de n_ muestras)
    float* slot = &ring_[(size_t)idx_ * 4];
    const float vMI = x * mr_, vMQ = x * mi_, vSI = x * sr_, vSQ = x * si_;
    aMI_ += vMI - slot[0];
    aMQ_ += vMQ - slot[1];
    aSI_ += vSI - slot[2];
    aSQ_ += vSQ - slot[3];
    slot[0] = vMI; slot[1] = vMQ; slot[2] = vSI; slot[3] = vSQ;
    if (++idx_ >= n_) idx_ = 0;
    const float em = aMI_ * aMI_ + aMQ_ * aMQ_, es = aSI_ * aSI_ + aSQ_ * aSQ_;
    const float d = (em - es) / (em + es + 1e-12f);
    // reloj: en cada cambio de signo la fase deberia ser 0,5 (se muestrea en el centro del bit)
    if ((d > 0) != (prevD_ > 0)) {
        const float err = phase_ - 0.5f;
        phase_ -= err * 0.3f;
    }
    prevD_ = d;
    phase_ += step_;
    if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
        onBit(d > 0 ? 1 : 0);
    }
}

void SameDecoder::onBit(int b) {
    shift_ = (uint8_t)((shift_ >> 1) | (b ? 0x80 : 0));
    if (!synced_) {
        if (shift_ == 0xAB) {
            synced_ = true;
            bitCount_ = 0;
            burst_.clear();
        }
        return;
    }
    if (++bitCount_ < 8) return;
    bitCount_ = 0;
    const uint8_t c = shift_;
    if (c == 0xAB && burst_.empty()) return;                   // aun en el preambulo
    if (c < 0x20 || c > 0x7E || burst_.size() > 280) { endBurst(); return; }
    burst_ += (char)c;
    if (burst_.size() >= 4 && burst_.compare(0, 4, "NNNN") == 0) { endBurst(); return; }
    if (sameHeaderEnd(burst_)) endBurst();
}

void SameDecoder::endBurst() {
    synced_ = false;
    std::string s = burst_;
    burst_.clear();
    if (s.size() >= 4 && s.compare(0, 4, "NNNN") == 0) {
        burstCount_++;
        if (!(lastEmit_ == "NNNN" && t_ - lastEmitT_ < (uint64_t)(fs_ * 10))) emit("NNNN");
        return;
    }
    const size_t end = sameHeaderEnd(s);
    if (s.compare(0, 4, "ZCZC") != 0 && !(s.size() > 8 && s.find("ZCZC") == std::string::npos && s[3] == '-')) return;
    burstCount_++;
    addBurst(end ? s.substr(0, end) : s);
}

void SameDecoder::addBurst(const std::string& s) {
    // las de hace mas de 12 s son de otro aviso
    for (size_t i = 0; i < recent_.size();) {
        if (t_ - recent_[i].t > (uint64_t)(fs_ * 12)) recent_.erase(recent_.begin() + (long)i);
        else i++;
    }
    if (recent_.empty()) emittedGroup_ = false;
    if (emittedGroup_) { recent_.push_back({s, t_}); return; }
    for (const auto& r : recent_) {
        if (r.text == s && sameHeaderEnd(s)) { emit(s); emittedGroup_ = true; recent_.push_back({s, t_}); return; }
    }
    recent_.push_back({s, t_});
    if (recent_.size() >= 3) {                                  // mayoria letra a letra
        const std::string& a = recent_[recent_.size() - 3].text;
        const std::string& b = recent_[recent_.size() - 2].text;
        const std::string& c = recent_[recent_.size() - 1].text;
        const size_t n = std::max(a.size(), std::max(b.size(), c.size()));
        std::string v;
        for (size_t i = 0; i < n; i++) {
            const char x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0, z = i < c.size() ? c[i] : 0;
            const char m = (x == y || x == z) ? x : (y == z ? y : x);
            if (!m) break;
            v += m;
        }
        const size_t end = sameHeaderEnd(v);
        SameMsg msg;
        if (end && sameParse(v, msg)) { emit(v.substr(0, end)); emittedGroup_ = true; }
    }
}

void SameDecoder::timeouts() {
    // una sola copia valida y nada mas en 6 s: se da por buena
    if (emittedGroup_ || recent_.empty()) return;
    if (t_ - recent_.front().t < (uint64_t)(fs_ * 6)) return;
    SameMsg msg;
    for (const auto& r : recent_) {
        if (sameParse(r.text, msg)) { emit(msg.raw); emittedGroup_ = true; return; }
    }
    recent_.clear();
}

void SameDecoder::emit(const std::string& s) {
    if (s == lastEmit_ && t_ - lastEmitT_ < (uint64_t)(fs_ * 120)) return;  // la misma cabecera otra vez (repetida por la emisora)
    lastEmit_ = s;
    lastEmitT_ = t_;
    out_.push_back(s);
}

void sameSynth(const std::string& text, float fs, std::vector<float>& out, int repeats) {
    double ph = 0, t = 0;
    const double spb = fs / kBaud;
    std::string frame(16, (char)0xAB);
    frame += text;
    for (int r = 0; r < repeats; r++) {
        double acc = 0;
        for (unsigned char c : frame) {
            for (int k = 0; k < 8; k++) {
                const int b = (c >> k) & 1;
                const double f = b ? kMark : kSpace;
                acc += spb;
                const int n = (int)std::floor(acc);
                acc -= n;
                for (int i = 0; i < n; i++) {
                    ph += 2 * kPi * f / fs;
                    out.push_back((float)(0.5 * std::sin(ph)));
                    t++;
                }
            }
        }
        for (int i = 0; i < (int)fs; i++) out.push_back(0.0f);   // 1 s de silencio
    }
    (void)t;
}

}  // namespace plotter
