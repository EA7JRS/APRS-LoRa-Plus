/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "alerts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace plotter {

// ---------------------------------------------------------------- nombres
const char* alertSrcName(AlertSrc s) {
    switch (s) {
        case AlertSrc::Aemet: return "AEMET";
        case AlertSrc::MeteoAlarm: return "MeteoAlarm";
        case AlertSrc::Nws: return "NWS";
        case AlertSrc::Tsunami: return "TSUNAMI";
        case AlertSrc::Swpc: return "SWPC";
        case AlertSrc::Same: return "SAME";
        case AlertSrc::Usgs: return "USGS";
        case AlertSrc::Emsc: return "EMSC";
        default: return "ALERT";
    }
}

const char* alertSrcCode(AlertSrc s) {
    switch (s) {
        case AlertSrc::Aemet: return "aemet";
        case AlertSrc::MeteoAlarm: return "ma";
        case AlertSrc::Nws: return "nws";
        case AlertSrc::Tsunami: return "tsu";
        case AlertSrc::Swpc: return "swpc";
        case AlertSrc::Same: return "same";
        case AlertSrc::Usgs: return "usgs";
        case AlertSrc::Emsc: return "emsc";
        default: return "other";
    }
}

AlertSrc alertSrcFromCode(const std::string& c) {
    if (c == "aemet") return AlertSrc::Aemet;
    if (c == "ma") return AlertSrc::MeteoAlarm;
    if (c == "nws") return AlertSrc::Nws;
    if (c == "tsu") return AlertSrc::Tsunami;
    if (c == "swpc") return AlertSrc::Swpc;
    if (c == "same") return AlertSrc::Same;
    if (c == "usgs") return AlertSrc::Usgs;
    if (c == "emsc") return AlertSrc::Emsc;
    return AlertSrc::Other;
}

const char* alertLevelName(uint8_t level) {
    static const char* const N[] = {"INFO", "YELLOW", "ORANGE", "RED"};
    return N[level > 3 ? 3 : level];
}

// ---------------------------------------------------------------- texto
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// Espacios seguidos (y saltos de linea) a uno
std::string squeeze(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (std::isspace((unsigned char)c)) { space = !out.empty(); continue; }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

// Entidades XML / HTML basicas y etiquetas fuera
std::string decodeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '<') {
            if (s.compare(i, 9, "<![CDATA[") == 0) {
                size_t e = s.find("]]>", i + 9);
                if (e == std::string::npos) e = s.size();
                out.append(s, i + 9, e - i - 9);
                i = e + 2;
                continue;
            }
            size_t e = s.find('>', i);
            if (e == std::string::npos) break;
            // <br/> y </p> como espacio
            out += ' ';
            i = e;
            continue;
        }
        if (c == '&') {
            size_t e = s.find(';', i);
            if (e != std::string::npos && e - i <= 8) {
                std::string ent = s.substr(i + 1, e - i - 1);
                bool done = true;
                if (ent == "amp") out += '&';
                else if (ent == "lt") out += '<';
                else if (ent == "gt") out += '>';
                else if (ent == "quot") out += '"';
                else if (ent == "apos") out += '\'';
                else if (ent == "nbsp") out += ' ';
                else if (!ent.empty() && ent[0] == '#') {
                    unsigned cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) ? (unsigned)std::strtoul(ent.c_str() + 2, nullptr, 16)
                                                                                     : (unsigned)std::strtoul(ent.c_str() + 1, nullptr, 10);
                    appendUtf8(out, cp);
                } else done = false;
                if (done) { i = e; continue; }
            }
        }
        out += c;
    }
    return out;
}

bool startsWithNoCase(const std::string& s, size_t at, const char* w) {
    for (size_t k = 0; w[k]; k++) {
        if (at + k >= s.size() || std::tolower((unsigned char)s[at + k]) != std::tolower((unsigned char)w[k])) return false;
    }
    return true;
}

bool containsNoCase(const std::string& s, const char* w) {
    for (size_t i = 0; i < s.size(); i++) if (startsWithNoCase(s, i, w)) return true;
    return false;
}

int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

}  // namespace

std::string asciiFold(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out += (char)c; continue; }
        unsigned cp = 0;
        int extra = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        for (int k = 0; k < extra && i + 1 < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[++i] & 0x3F);
        const char* rep = "?";
        if (cp >= 0xC0 && cp <= 0xC5) rep = "A";
        else if (cp == 0xC7) rep = "C";
        else if (cp >= 0xC8 && cp <= 0xCB) rep = "E";
        else if (cp >= 0xCC && cp <= 0xCF) rep = "I";
        else if (cp == 0xD1) rep = "N";
        else if ((cp >= 0xD2 && cp <= 0xD6) || cp == 0xD8) rep = "O";
        else if (cp >= 0xD9 && cp <= 0xDC) rep = "U";
        else if (cp == 0xDD) rep = "Y";
        else if (cp == 0xDF) rep = "ss";
        else if (cp >= 0xE0 && cp <= 0xE5) rep = "a";
        else if (cp == 0xE7) rep = "c";
        else if (cp >= 0xE8 && cp <= 0xEB) rep = "e";
        else if (cp >= 0xEC && cp <= 0xEF) rep = "i";
        else if (cp == 0xF1) rep = "n";
        else if ((cp >= 0xF2 && cp <= 0xF6) || cp == 0xF8) rep = "o";
        else if (cp >= 0xF9 && cp <= 0xFC) rep = "u";
        else if (cp == 0xFD || cp == 0xFF) rep = "y";
        else if (cp == 0xB0) rep = "o";
        else if (cp == 0xAA) rep = "a";
        else if (cp == 0xBA) rep = "o";
        else if (cp == 0x2013 || cp == 0x2014) rep = "-";
        else if (cp == 0x2018 || cp == 0x2019) rep = "'";
        else if (cp == 0x201C || cp == 0x201D) rep = "\"";
        else if (cp == 0xA0) rep = " ";
        else if (cp >= 0x100 && cp <= 0x17F) {             // latin extendido (alemán, polaco, checo...): la letra base
            static const char base[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
            size_t k = cp - 0x100;
            static char one[2] = {0, 0};
            one[0] = k < sizeof(base) - 1 ? base[k] : '?';
            out += one;
            continue;
        }
        out += rep;
    }
    return out;
}

std::string xmlText(const std::string& xml, const char* name, size_t from) {
    const std::string open = std::string("<") + name;
    size_t a = from;
    while (true) {
        a = xml.find(open, a);
        if (a == std::string::npos) return "";
        char next = a + open.size() < xml.size() ? xml[a + open.size()] : '\0';
        if (next == '>' || next == ' ' || next == '/' || next == '\t' || next == '\n' || next == '\r') break;
        a += open.size();
    }
    size_t gt = xml.find('>', a);
    if (gt == std::string::npos) return "";
    if (xml[gt - 1] == '/') return "";                          // <tag/>
    const std::string close = std::string("</") + name + ">";
    size_t e = xml.find(close, gt + 1);
    if (e == std::string::npos) return "";
    return trim(squeeze(decodeXml(xml.substr(gt + 1, e - gt - 1))));
}

std::vector<std::string> xmlBlocks(const std::string& doc, const char* tag) {
    std::vector<std::string> out;
    const std::string open = std::string("<") + tag, close = std::string("</") + tag + ">";
    size_t a = 0;
    while ((a = doc.find(open, a)) != std::string::npos) {
        char next = a + open.size() < doc.size() ? doc[a + open.size()] : '\0';
        if (next != '>' && next != ' ' && next != '\n' && next != '\r' && next != '\t') { a += open.size(); continue; }
        size_t e = doc.find(close, a);
        if (e == std::string::npos) break;
        out.push_back(doc.substr(a, e + close.size() - a));
        a = e + close.size();
    }
    return out;
}

XmlBlockSplitter::XmlBlockSplitter(const char* tag, size_t maxBlock)
    : open_(std::string("<") + tag), close_(std::string("</") + tag + ">"), max_(maxBlock) {}

bool XmlBlockSplitter::push(char c) {
    if (!inside_) {
        tail_ += c;
        if (tail_.size() > open_.size() + 1) tail_.erase(0, tail_.size() - open_.size() - 1);
        // "<entry>" o "<entry " (no "<entryX")
        if (tail_.size() == open_.size() + 1 && tail_.compare(0, open_.size(), open_) == 0 &&
            (c == '>' || c == ' ' || c == '\n' || c == '\r' || c == '\t')) {
            inside_ = true;
            overflow_ = false;
            block_ = tail_;
            tail_.clear();
        }
        return false;
    }
    if (!overflow_) {
        block_ += c;
        if (block_.size() > max_) { overflow_ = true; block_.clear(); }
    }
    // fin: se mira en los ultimos bytes (en desbordamiento, una ventana corta)
    if (overflow_) {
        tail_ += c;
        if (tail_.size() > close_.size()) tail_.erase(0, tail_.size() - close_.size());
        if (tail_ == close_) { inside_ = false; tail_.clear(); }
        return false;
    }
    if (block_.size() >= close_.size() && block_.compare(block_.size() - close_.size(), close_.size(), close_) == 0) {
        inside_ = false;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- tiempo
int64_t unixFromUtc(int y, int mo, int d, int h, int mi, int s) {
    return daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

int64_t parseIsoTime(const std::string& in) {
    const std::string s = trim(in);
    int y, mo, d, h = 0, mi = 0, sec = 0;
    if (s.size() < 10 || std::sscanf(s.c_str(), "%4d-%2d-%2d", &y, &mo, &d) != 3) return 0;
    size_t i = 10;
    if (i < s.size() && (s[i] == 'T' || s[i] == ' ')) {
        if (std::sscanf(s.c_str() + i + 1, "%2d:%2d", &h, &mi) != 2) return 0;
        i += 6;
        if (i < s.size() && s[i] == ':') { sec = std::atoi(s.c_str() + i + 1); i += 3; }
        while (i < s.size() && (s[i] == '.' || std::isdigit((unsigned char)s[i]))) i++;     // fracciones
    }
    int64_t t = unixFromUtc(y, mo, d, h, mi, sec);
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        int oh = 0, om = 0;
        std::sscanf(s.c_str() + i + 1, "%2d:%2d", &oh, &om);
        if (s.size() > i + 3 && s[i + 3] != ':') om = std::atoi(s.substr(i + 3, 2).c_str());   // +0100
        const int off = (oh * 60 + om) * 60;
        t -= s[i] == '+' ? off : -off;
    }
    return t;
}

// ---------------------------------------------------------------- niveles
uint8_t alertLevelFromSeverity(const std::string& sev) {
    if (containsNoCase(sev, "extreme")) return 3;
    if (containsNoCase(sev, "severe")) return 2;
    if (containsNoCase(sev, "moderate")) return 1;
    return 0;
}

int alertLevelFromColour(const std::string& t) {
    if (containsNoCase(t, "red") || containsNoCase(t, "rojo")) {
        // "red" dentro de otras palabras ("covered", "reduced"): solo como palabra suelta
        for (size_t i = 0; i < t.size(); i++) {
            bool start = i == 0 || !std::isalpha((unsigned char)t[i - 1]);
            if (!start) continue;
            if (startsWithNoCase(t, i, "rojo") && (i + 4 >= t.size() || !std::isalpha((unsigned char)t[i + 4]))) return 3;
            if (startsWithNoCase(t, i, "red") && (i + 3 >= t.size() || !std::isalpha((unsigned char)t[i + 3]))) return 3;
        }
    }
    if (containsNoCase(t, "orange") || containsNoCase(t, "naranja")) return 2;
    if (containsNoCase(t, "yellow") || containsNoCase(t, "amarillo")) return 1;
    if (containsNoCase(t, "green") || containsNoCase(t, "verde")) return 0;
    return -1;
}

// ---------------------------------------------------------------- MeteoAlarm
bool meteoAlarmParseEntry(const std::string& e, Alert& a) {
    a = Alert();
    a.src = AlertSrc::MeteoAlarm;
    a.event = xmlText(e, "cap:event");
    a.area = xmlText(e, "cap:areaDesc");
    const std::string title = xmlText(e, "title");
    if (a.event.empty() && title.empty()) return false;
    const std::string status = xmlText(e, "cap:status"), type = xmlText(e, "cap:message_type");
    if (!status.empty() && status != "Actual") return false;
    if (type == "Cancel") return false;
    const int colour = alertLevelFromColour(title);
    a.level = colour >= 0 ? (uint8_t)colour : alertLevelFromSeverity(xmlText(e, "cap:severity"));
    if (a.event.empty()) a.event = title;
    a.text = title;
    a.sent = parseIsoTime(xmlText(e, "cap:sent"));
    a.onset = parseIsoTime(xmlText(e, "cap:onset"));
    if (!a.onset) a.onset = parseIsoTime(xmlText(e, "cap:effective"));
    a.expires = parseIsoTime(xmlText(e, "cap:expires"));
    const std::string ident = xmlText(e, "cap:identifier"), zone = xmlText(e, "value");
    a.id = (ident.empty() ? xmlText(e, "id") : ident) + "/" + zone;
    return true;
}

const char* meteoAlarmFeedName(const char* iso) {
    static const char* const T[][2] = {
        {"AT", "austria"}, {"BE", "belgium"}, {"BA", "bosnia-herzegovina"}, {"BG", "bulgaria"}, {"HR", "croatia"},
        {"CY", "cyprus"}, {"CZ", "czechia"}, {"DK", "denmark"}, {"EE", "estonia"}, {"FI", "finland"}, {"FR", "france"},
        {"DE", "germany"}, {"GR", "greece"}, {"HU", "hungary"}, {"IS", "iceland"}, {"IE", "ireland"}, {"IL", "israel"},
        {"IT", "italy"}, {"LV", "latvia"}, {"LT", "lithuania"}, {"LU", "luxembourg"}, {"MT", "malta"}, {"MD", "moldova"},
        {"ME", "montenegro"}, {"NL", "netherlands"}, {"MK", "republic-of-north-macedonia"}, {"NO", "norway"},
        {"PL", "poland"}, {"PT", "portugal"}, {"RO", "romania"}, {"RS", "serbia"}, {"SK", "slovakia"},
        {"SI", "slovenia"}, {"ES", "spain"}, {"SE", "sweden"}, {"CH", "switzerland"}, {"UA", "ukraine"},
        {"GB", "united-kingdom"}};
    if (!iso) return nullptr;
    for (const auto& r : T) if (std::toupper((unsigned char)iso[0]) == r[0][0] && std::toupper((unsigned char)iso[1]) == r[0][1]) return r[1];
    return nullptr;
}

// ---------------------------------------------------------------- AEMET (RSS)
std::string aemetPhenomenonEn(const std::string& es) {
    static const char* const T[][2] = {
        {"Lluvias", "Rain"}, {"Tormentas", "Thunderstorms"}, {"Nieblas", "Fog"}, {"Vientos", "Wind"},
        {"Costeros", "Coastal events"}, {"Fenómenos costeros", "Coastal events"}, {"Nevadas", "Snow"},
        {"Temperaturas máximas", "High temperatures"}, {"Temperaturas mínimas", "Low temperatures"},
        {"Polvo en suspensión", "Dust"}, {"Aludes", "Avalanches"}, {"Galernas", "Galerna"}, {"Rissagas", "Rissaga"},
        {"Deshielos", "Thaw"}, {"Olas de calor", "Heat wave"}, {"Olas de frío", "Cold wave"}};
    for (const auto& r : T) if (es == r[0]) return r[1];
    return es;
}

namespace {
// "18:00 04-11-2026 CET (UTC+1)" desde "at"; segundos Unix o 0
int64_t aemetTime(const std::string& s, size_t at) {
    int h, mi, d, mo, y;
    if (std::sscanf(s.c_str() + at, "%d:%d %d-%d-%d", &h, &mi, &d, &mo, &y) != 5) return 0;
    int64_t t = unixFromUtc(y, mo, d, h, mi, 0);
    size_t u = s.find("(UTC", at);
    if (u != std::string::npos && u - at < 40) {
        int off = 0;
        if (s[u + 4] == '+' || s[u + 4] == '-') off = std::atoi(s.c_str() + u + 4);
        t -= off * 3600;
    }
    return t;
}
}  // namespace

bool aemetParseRssItem(const std::string& item, Alert& a) {
    a = Alert();
    a.src = AlertSrc::Aemet;
    const std::string title = xmlText(item, "title");
    // "Aviso. Nivel amarillo. Lluvias. Costa granadina"
    if (title.compare(0, 6, "Aviso.") != 0) return false;
    std::vector<std::string> parts;
    size_t p = 0;
    while (p <= title.size()) {
        size_t e = title.find(". ", p);
        if (e == std::string::npos) e = title.size();
        parts.push_back(trim(title.substr(p, e - p)));
        p = e + 2;
    }
    if (parts.size() < 4) return false;
    const int lv = alertLevelFromColour(parts[1]);
    if (lv < 0) return false;
    a.level = (uint8_t)lv;
    a.event = aemetPhenomenonEn(parts[2]);
    a.area = parts[3];
    for (size_t k = 4; k < parts.size(); k++) a.area += ". " + parts[k];
    const std::string desc = xmlText(item, "description");
    // "Aviso de <detalle> de nivel amarillo de 18:00 04-11-2026 CET (UTC+1) a 07:59 05-11-2026 CET (UTC+1)."
    size_t lvl = desc.find(" de nivel ");
    if (desc.compare(0, 9, "Aviso de ") == 0 && lvl != std::string::npos) a.text = desc.substr(9, lvl - 9);
    else a.text = desc;
    size_t from = desc.find(" de ", lvl == std::string::npos ? 0 : lvl + 10);
    if (from != std::string::npos) {
        a.onset = aemetTime(desc, from + 4);
        size_t to = desc.find(" a ", from + 4);
        if (to != std::string::npos) a.expires = aemetTime(desc, to + 3);
    }
    a.id = xmlText(item, "guid");
    if (a.id.empty()) a.id = title + "@" + std::to_string((long long)a.onset);
    return true;
}

// ---------------------------------------------------------------- CAP
std::vector<LatLon> capPolygon(const std::string& t) {
    std::vector<LatLon> out;
    const char* p = t.c_str();
    while (*p) {
        char* e;
        double la = std::strtod(p, &e);
        if (e == p) { p++; continue; }
        if (*e != ',') { p = e; continue; }
        const char* q = e + 1;
        double lo = std::strtod(q, &e);
        if (e == q) break;
        out.push_back({la, lo});
        p = e;
    }
    return out;
}

bool pointInPolygon(LatLon pt, const std::vector<LatLon>& poly) {
    bool in = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n ? n - 1 : 0; i < n; j = i++) {
        const LatLon& a = poly[i];
        const LatLon& b = poly[j];
        if (((a.lat > pt.lat) != (b.lat > pt.lat)) &&
            (pt.lon < (b.lon - a.lon) * (pt.lat - a.lat) / (b.lat - a.lat) + a.lon)) in = !in;
    }
    return in;
}

bool capParse(const std::string& xml, std::vector<Alert>& out, std::vector<std::vector<LatLon>>* polygons, const char* lang) {
    if (xml.find("<alert") == std::string::npos) return false;
    const std::string status = xmlText(xml, "status"), msgType = xmlText(xml, "msgType");
    if (!status.empty() && status != "Actual") return false;
    if (msgType == "Cancel") return false;
    const std::string ident = xmlText(xml, "identifier");
    const int64_t sent = parseIsoTime(xmlText(xml, "sent"));
    std::vector<std::string> infos = xmlBlocks(xml, "info");
    if (infos.empty()) return false;
    const std::string* info = &infos[0];
    for (const auto& i : infos) {
        const std::string l = xmlText(i, "language");
        if (lang && !l.empty() && startsWithNoCase(l, 0, lang)) { info = &i; break; }
    }
    // Nivel: parametro "AEMET-Meteoalerta nivel" o un color en el evento; si no, la gravedad
    int lv = -1;
    for (const auto& par : xmlBlocks(*info, "parameter")) {
        if (containsNoCase(xmlText(par, "valueName"), "nivel") || containsNoCase(xmlText(par, "valueName"), "awareness_level")) {
            lv = alertLevelFromColour(xmlText(par, "value"));
            if (lv < 0) {                                       // MeteoAlarm: "2; yellow; Moderate"
                const std::string v = xmlText(par, "value");
                if (!v.empty() && std::isdigit((unsigned char)v[0])) lv = std::max(0, std::atoi(v.c_str()) - 1);
            }
        }
    }
    const std::string event = xmlText(*info, "event"), headline = xmlText(*info, "headline");
    if (lv < 0) lv = alertLevelFromColour(event);
    if (lv < 0) lv = alertLevelFromSeverity(xmlText(*info, "severity"));
    Alert base;
    base.src = AlertSrc::Other;
    base.level = (uint8_t)std::min(3, std::max(0, lv));
    base.event = event;
    base.text = xmlText(*info, "description");
    if (base.text.empty()) base.text = headline;
    base.instruction = xmlText(*info, "instruction");
    base.sent = sent;
    base.onset = parseIsoTime(xmlText(*info, "onset"));
    if (!base.onset) base.onset = parseIsoTime(xmlText(*info, "effective"));
    base.expires = parseIsoTime(xmlText(*info, "expires"));
    std::vector<std::string> areas = xmlBlocks(*info, "area");
    if (areas.empty()) areas.push_back("");
    int k = 0;
    for (const auto& ar : areas) {
        Alert a = base;
        a.area = xmlText(ar, "areaDesc");
        a.id = ident + (areas.size() > 1 ? "/" + std::to_string(k) : "");
        std::vector<LatLon> poly;
        for (const auto& pg : xmlBlocks(ar, "polygon")) {
            std::vector<LatLon> one = capPolygon(xmlText(pg, "polygon"));
            poly.insert(poly.end(), one.begin(), one.end());
            if (polygons) break;                                // el primero (los demas, a parte)
        }
        if (!poly.empty()) {
            double la = 0, lo = 0;
            for (const auto& q : poly) { la += q.lat; lo += q.lon; }
            a.lat = la / poly.size();
            a.lon = lo / poly.size();
        }
        if (polygons) polygons->push_back(poly);
        out.push_back(a);
        k++;
    }
    return true;
}

// ---------------------------------------------------------------- NWS
bool nwsParse(const std::string& json, std::vector<Alert>& out) {
    JsonValue v;
    if (!jsonParse(json, v)) return false;
    const JsonValue& feats = v["features"];
    if (!feats.isArray()) return false;
    for (const auto& f : feats.items) {
        const JsonValue& p = f["properties"];
        if (p.str() == "" && !p.isObject()) continue;
        if (p["status"].str("Actual") != "Actual" || p["messageType"].str() == "Cancel") continue;
        Alert a;
        a.src = AlertSrc::Nws;
        a.id = p["id"].str();
        a.event = p["event"].str();
        a.area = p["areaDesc"].str();
        a.level = alertLevelFromSeverity(p["severity"].str());
        a.text = p["headline"].str();
        a.instruction = p["instruction"].str();
        a.sent = parseIsoTime(p["sent"].str());
        a.onset = parseIsoTime(p["onset"].str());
        if (!a.onset) a.onset = parseIsoTime(p["effective"].str());
        a.expires = parseIsoTime(p["ends"].str());
        if (!a.expires) a.expires = parseIsoTime(p["expires"].str());
        a.text = squeeze(a.text);
        a.instruction = squeeze(a.instruction);
        // poligono: centro aproximado
        const JsonValue& g = f["geometry"];
        if (g["type"].str() == "Polygon" && g["coordinates"][0].isArray()) {
            const JsonValue& ring = g["coordinates"][0];
            double la = 0, lo = 0;
            size_t n = 0;
            for (const auto& c : ring.items) if (c.size() >= 2) { lo += c[0].num(); la += c[1].num(); n++; }
            if (n) { a.lat = la / n; a.lon = lo / n; }
        }
        if (a.id.empty()) continue;
        out.push_back(a);
    }
    return true;
}

// ---------------------------------------------------------------- tsunami.gov
bool tsunamiParseEntry(const std::string& e, Alert& a) {
    a = Alert();
    a.src = AlertSrc::Tsunami;
    const std::string region = xmlText(e, "title");
    const std::string sum = xmlText(e, "summary");
    if (region.empty() && sum.empty()) return false;
    auto field = [&](const char* name) -> std::string {
        size_t p = sum.find(name);
        if (p == std::string::npos) return "";
        p += std::strlen(name);
        while (p < sum.size() && (sum[p] == ':' || sum[p] == ' ')) p++;
        // hasta el siguiente "Xxxx:" conocido
        static const char* const next[] = {"Bulletin Issue Time", "Preliminary Magnitude", "Lat/Lon", "Affected Region", "Note", "Definition", "Category"};
        size_t e2 = sum.size();
        for (const char* n : next) {
            size_t q = sum.find(n, p);
            if (q != std::string::npos && q < e2) e2 = q;
        }
        return trim(sum.substr(p, e2 - p));
    };
    std::string cat = field("Category");
    a.level = containsNoCase(cat, "warning") ? 3 : containsNoCase(cat, "advisory") ? 2 : containsNoCase(cat, "watch") ? 1 : 0;
    if (containsNoCase(cat, "cancel")) a.level = 0;
    a.event = "Tsunami " + (cat.empty() ? std::string("message") : cat);
    a.area = region;
    const std::string mag = field("Preliminary Magnitude");
    a.text = (mag.empty() ? std::string("") : "M" + mag + " ") + region;
    const std::string la = xmlText(e, "geo:lat"), lo = xmlText(e, "geo:long");
    if (!la.empty() && !lo.empty()) { a.lat = std::atof(la.c_str()); a.lon = std::atof(lo.c_str()); }
    // "2026.10.25 02:27:01 UTC"
    const std::string bt = field("Bulletin Issue Time");
    int y, mo, d, h, mi, s;
    if (std::sscanf(bt.c_str(), "%d.%d.%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) a.sent = unixFromUtc(y, mo, d, h, mi, s);
    else a.sent = parseIsoTime(xmlText(e, "updated"));
    a.onset = a.sent;
    a.expires = a.sent ? a.sent + 24 * 3600 : 0;
    a.id = xmlText(e, "id");
    if (a.id.empty()) a.id = region + "@" + std::to_string((long long)a.sent);
    return true;
}

// ---------------------------------------------------------------- terremotos
namespace {
void quakeFill(Alert& a, double mag, const std::string& place, double lat, double lon, double depthKm, int64_t t) {
    char ev[24];
    std::snprintf(ev, sizeof(ev), "M%.1f Earthquake", mag);
    a.event = ev;
    a.area = place;
    a.lat = lat;
    a.lon = lon;
    char tx[32];
    std::snprintf(tx, sizeof(tx), "Depth %.0f km", std::fabs(depthKm));
    a.text = tx;
    a.sent = a.onset = t;
    a.expires = t ? t + 24 * 3600 : 0;
}
}  // namespace

std::string emscUrl(double lat, double lon, int64_t since, double minMag, double radius) {
    int y, mo, d;
    const int64_t days = since / 86400;
    // fecha civil del dia (algoritmo inverso de daysFromCivil)
    {
        int64_t z = days + 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = (unsigned)(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        d = (int)(doy - (153 * mp + 2) / 5 + 1);
        mo = (int)(mp < 10 ? mp + 3 : mp - 9);
        y = (int)(yoe + era * 400 + (mo <= 2));
    }
    const int sec = (int)(since % 86400);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "https://www.seismicportal.eu/fdsnws/event/1/query?format=json&limit=50&minmag=%.1f&latitude=%.2f&longitude=%.2f&maxradius=%.0f"
                  "&start=%04d-%02d-%02dT%02d:%02d:%02d",
                  minMag, lat, lon, radius, y, mo, d, sec / 3600, sec / 60 % 60, sec % 60);
    return buf;
}

bool quakeParseUsgs(const std::string& json, std::vector<Alert>& out) {
    JsonValue v;
    if (!jsonParse(json, v) || !v["features"].isArray()) return false;
    for (const auto& f : v["features"].items) {
        const JsonValue& p = f["properties"];
        const JsonValue& c = f["geometry"]["coordinates"];
        if (p["type"].str("earthquake") != "earthquake" || c.size() < 2) continue;
        Alert a;
        a.src = AlertSrc::Usgs;
        a.id = "usgs/" + f["id"].str();
        quakeFill(a, p["mag"].num(), p["place"].str(), c[1].num(), c[0].num(), c[2].num(), (int64_t)(p["time"].num() / 1000));
        if (p["tsunami"].num() > 0) a.text += " tsunami?";
        out.push_back(a);
    }
    return true;
}

bool quakeParseEmsc(const std::string& json, std::vector<Alert>& out) {
    JsonValue v;
    if (!jsonParse(json, v) || !v["features"].isArray()) return false;
    for (const auto& f : v["features"].items) {
        const JsonValue& p = f["properties"];
        if (!p.isObject()) continue;
        Alert a;
        a.src = AlertSrc::Emsc;
        a.id = "emsc/" + p["unid"].str(f["id"].str().c_str());
        std::string place = p["flynn_region"].str();
        // "SOUTH OF FIJI ISLANDS" -> "South of fiji islands"? se deja en mayusculas, como lo publica
        quakeFill(a, p["mag"].num(), place, p["lat"].num(), p["lon"].num(), p["depth"].num(), parseIsoTime(p["time"].str()));
        out.push_back(a);
    }
    return true;
}

uint8_t quakeLevel(double m, double km) {
    if ((m >= 6 && km < 300) || (m >= 7 && km < 1000)) return 3;
    if ((m >= 5 && km < 300) || (m >= 6 && km < 1000) || (m >= 7.5 && km < 3000)) return 2;
    if ((m >= 4 && km < 100) || (m >= 5 && km < 600) || (m >= 6 && km < 2000)) return 1;
    return 0;
}

double quakeMag(const Alert& a) {
    return a.event.size() > 1 && a.event[0] == 'M' ? std::atof(a.event.c_str() + 1) : 0;
}

bool quakeSame(const Alert& a, const Alert& b) {
    if (!a.hasPos() || !b.hasPos()) return false;
    const double km = gcDistanceNm({a.lat, a.lon}, {b.lat, b.lon}) * 1.852;
    return km < 60 && std::llabs((long long)(a.onset - b.onset)) < 90 && std::fabs(quakeMag(a) - quakeMag(b)) < 0.6;
}

void quakeSetLevels(std::vector<Alert>& q, LatLon here, bool haveHere) {
    // duplicados: se queda el de EMSC si esta cerca (mas datos locales), si no el primero
    for (size_t i = 0; i < q.size(); i++) {
        for (size_t j = i + 1; j < q.size();) {
            if (quakeSame(q[i], q[j])) {
                if (q[j].src == AlertSrc::Emsc && q[i].src != AlertSrc::Emsc) q[i] = q[j];
                q.erase(q.begin() + (long)j);
            } else j++;
        }
    }
    static const char* const card[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    for (auto& a : q) {
        const std::string depth = a.text.substr(0, a.text.find(" km") == std::string::npos ? a.text.size() : a.text.find(" km") + 3);
        if (!haveHere || !a.hasPos()) { a.level = quakeLevel(quakeMag(a), 1e9); continue; }
        const double km = gcDistanceNm(here, {a.lat, a.lon}) * 1.852;
        const double brg = gcInitialBearing(here, {a.lat, a.lon});
        a.level = quakeLevel(quakeMag(a), km);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%s, %.0f km %s", depth.c_str(), km, card[(int)std::floor((brg + 22.5) / 45.0) % 8]);
        a.text = buf;
    }
}

// ---------------------------------------------------------------- SWPC
const char* swpcScaleName(char b) {
    switch (b) {
        case 'G': return "Geomagnetic storm";
        case 'R': return "Radio blackout";
        case 'S': return "Radiation storm";
        default: return "Space weather";
    }
}

int kpToG(float kp) {
    if (kp >= 9) return 5;
    if (kp >= 8) return 4;
    if (kp >= 7) return 3;
    if (kp >= 6) return 2;
    if (kp >= 5) return 1;
    return 0;
}

std::string xrayClass(double w) {
    if (!(w > 0)) return "";
    static const char cls[] = "ABCMX";
    int k = (int)std::floor(std::log10(w)) + 8;             // 1e-8 A, 1e-7 B, 1e-6 C, 1e-5 M, 1e-4 X
    if (k < 0) k = 0;
    if (k > 4) k = 4;
    const double mult = w / std::pow(10.0, k - 8);
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%c%.1f", cls[k], mult);
    return buf;
}

bool swpcParseScales(const std::string& json, SpaceWx& w) {
    JsonValue v;
    if (!jsonParse(json, v) || !v.isObject()) return false;
    const JsonValue& t = v["0"];
    if (!t.isObject()) return false;
    w.g = (int)t["G"]["Scale"].num(-1);
    w.r = (int)t["R"]["Scale"].num(-1);
    w.s = (int)t["S"]["Scale"].num(-1);
    w.gMax = -1;
    for (const char* k : {"1", "2", "3"}) w.gMax = std::max(w.gMax, (int)v[k]["G"]["Scale"].num(-1));
    return true;
}

bool swpcParseKp(const std::string& json, SpaceWx& w) {
    JsonValue v;
    if (!jsonParse(json, v) || !v.isArray() || v.size() == 0) return false;
    std::vector<std::pair<int64_t, float>> rows;
    for (const auto& r : v.items) {
        if (r.isArray()) {                                      // [["time_tag","Kp",...], ["2026-11-05 00:00:00.000","2.33",...]]
            if (r.size() < 2 || !std::isdigit((unsigned char)r[0].str().c_str()[0])) continue;
            rows.push_back({parseIsoTime(r[0].str()), (float)r[1].num(-1)});
        } else if (r.isObject()) {                              // [{"time_tag":"...","Kp":2.33,...}]
            const JsonValue& kp = r.has("Kp") ? r["Kp"] : r["kp"];
            rows.push_back({parseIsoTime(r["time_tag"].str()), (float)kp.num(-1)});
        }
    }
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const std::pair<int64_t, float>& p) { return p.second < 0 || p.first == 0; }), rows.end());
    if (rows.empty()) return false;
    std::sort(rows.begin(), rows.end());
    const size_t n = std::min<size_t>(8, rows.size());
    w.kpCount = (int)n;
    for (size_t i = 0; i < n; i++) w.kp[i] = rows[rows.size() - n + i].second;
    w.kpTime = rows.back().first;
    return true;
}

bool swpcParseSummary(const std::string& json, SpaceWx& w) {
    JsonValue v;
    if (!jsonParse(json, v)) return false;
    const JsonValue& o = v.isArray() ? v[size_t(0)] : v;
    if (!o.isObject()) return false;
    bool any = false;
    if (o.has("Flux")) { w.sfi = (float)o["Flux"].num(-1); any = true; }
    if (o.has("Bz")) { w.bz = (float)o["Bz"].num(1000); any = true; }
    if (o.has("Bt")) { w.bt = (float)o["Bt"].num(-1); any = true; }
    if (o.has("WindSpeed")) { w.windKms = (float)o["WindSpeed"].num(-1); any = true; }
    return any;
}

bool swpcParseXray(const std::string& json, SpaceWx& w) {
    JsonValue v;
    if (!jsonParse(json, v) || !v.isArray()) return false;
    for (size_t i = v.size(); i-- > 0;) {
        const JsonValue& r = v[i];
        if (r["energy"].str() == "0.1-0.8nm" && r["flux"].num(0) > 0) { w.xray = xrayClass(r["flux"].num()); return true; }
    }
    return false;
}

namespace {
const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
// "2026 Aug 27 1200 UTC" -> segundos Unix
int64_t swpcTime(const std::string& s) {
    int y, d, hm;
    char mon[4] = {0};
    if (std::sscanf(s.c_str(), "%d %3s %d %d", &y, mon, &d, &hm) != 4) return 0;
    for (int m = 0; m < 12; m++) if (std::strcmp(mon, kMonths[m]) == 0) return unixFromUtc(y, m + 1, d, hm / 100, hm % 100, 0);
    return 0;
}
std::string lineAfter(const std::string& msg, const char* tag) {
    size_t p = msg.find(tag);
    if (p == std::string::npos) return "";
    p += std::strlen(tag);
    size_t e = msg.find_first_of("\r\n", p);
    return trim(msg.substr(p, e == std::string::npos ? std::string::npos : e - p));
}
}  // namespace

bool swpcParseAlerts(const std::string& json, int64_t now, std::vector<Alert>& out) {
    JsonValue v;
    if (!jsonParse(json, v) || !v.isArray()) return false;
    std::vector<std::string> cancelled;
    std::vector<Alert> found;
    for (const auto& p : v.items) {
        const std::string msg = p["message"].str();
        const std::string code = lineAfter(msg, "Space Weather Message Code:");
        const int64_t issued = parseIsoTime(p["issue_datetime"].str());
        if (now && issued && now - issued > 48 * 3600) continue;
        std::string type, title;
        for (const char* t : {"EXTENDED WARNING:", "CANCEL WARNING:", "CANCEL WATCH:", "CONTINUED ALERT:", "ALERT:", "WARNING:", "WATCH:", "SUMMARY:"}) {
            // al principio de una linea
            size_t at = msg.find(t);
            while (at != std::string::npos && at > 0 && msg[at - 1] != '\n') at = msg.find(t, at + 1);
            if (at != std::string::npos) { type = t; title = lineAfter(msg, t); break; }
        }
        if (type.empty() || type == "SUMMARY:") continue;
        // el codigo del aviso cancelado o ampliado: "Cancel Serial Number: 1234" / "Extension to Serial Number:"
        if (type.compare(0, 6, "CANCEL") == 0) {
            const std::string ser = lineAfter(msg, "Cancel Serial Number:");
            if (!ser.empty()) cancelled.push_back(ser);
            continue;
        }
        Alert a;
        a.src = AlertSrc::Swpc;
        const std::string serial = lineAfter(msg, "Serial Number:");
        a.id = "swpc/" + code + "/" + serial;
        if (type == "EXTENDED WARNING:") {
            const std::string ext = lineAfter(msg, "Extension to Serial Number:");
            if (!ext.empty()) cancelled.push_back(ext);         // la ampliacion sustituye al aviso anterior
        }
        a.event = title;
        // "NOAA Scale: G3 - Strong"; en los WATCH, "Category G2 Predicted"
        std::string scale = lineAfter(msg, "NOAA Scale:");
        if (scale.empty()) {
            size_t c = msg.find("Category ");
            if (c != std::string::npos && type == "WATCH:") scale = msg.substr(c + 9, 2);
        }
        int lvScale = 0;
        if (scale.size() >= 2 && (scale[0] == 'G' || scale[0] == 'R' || scale[0] == 'S') && std::isdigit((unsigned char)scale[1])) {
            lvScale = scale[1] - '0';
            a.area = scale.substr(0, 2) + " " + swpcScaleName(scale[0]);
        } else {
            a.area = "Space weather";
        }
        a.level = lvScale >= 4 ? 3 : lvScale == 3 ? 2 : lvScale >= 1 ? 1 : 0;
        a.text = type.substr(0, type.size() - 1);
        for (char& c : a.text) c = (char)std::tolower((unsigned char)c);
        if (!a.text.empty()) a.text[0] = (char)std::toupper((unsigned char)a.text[0]);
        a.sent = issued;
        a.onset = swpcTime(lineAfter(msg, "Valid From:"));
        if (!a.onset) a.onset = issued;
        a.expires = swpcTime(lineAfter(msg, "Valid To:"));
        if (!a.expires) a.expires = swpcTime(lineAfter(msg, "Now Valid Until:"));
        if (!a.expires) a.expires = issued ? issued + (type == "ALERT:" || type == "CONTINUED ALERT:" ? 6 : 24) * 3600 : 0;
        if (now && a.expires && a.expires < now) continue;
        found.push_back(a);
        (void)serial;
    }
    for (auto& a : found) {
        bool gone = false;
        for (const auto& c : cancelled) if (a.id.size() > c.size() && a.id.compare(a.id.size() - c.size(), c.size(), c) == 0 && a.id[a.id.size() - c.size() - 1] == '/') gone = true;
        if (!gone) out.push_back(a);
    }
    return true;
}

void swpcScaleAlerts(const SpaceWx& w, int64_t now, std::vector<Alert>& out) {
    const struct { char b; int v; } s[] = {{'G', w.g}, {'R', w.r}, {'S', w.s}};
    for (const auto& x : s) {
        if (x.v < 1) continue;
        Alert a;
        a.src = AlertSrc::Swpc;
        char id[16];
        std::snprintf(id, sizeof(id), "swpc/scale/%c", x.b);
        a.id = id;
        char ev[40];
        std::snprintf(ev, sizeof(ev), "%c%d %s", x.b, x.v, swpcScaleName(x.b));
        a.event = ev;
        a.area = "Space weather";
        a.level = x.v >= 4 ? 3 : x.v == 3 ? 2 : 1;
        a.sent = a.onset = now;
        a.expires = now + 6 * 3600;
        out.push_back(a);
    }
}

// ---------------------------------------------------------------- lista
bool AlertList::upsert(const Alert& a) {
    for (auto& x : items_) {
        if (x.id != a.id) continue;
        const bool raised = a.level > x.level;
        x = a;
        sort();
        return raised;
    }
    if (items_.size() >= kMax) {                                // lleno: fuera el menos grave (si este lo es mas)
        sort();
        if (items_.back().level > a.level) return false;
        items_.pop_back();
    }
    items_.push_back(a);
    sort();
    return true;
}

std::vector<std::string> AlertList::replaceSource(AlertSrc src, const std::vector<Alert>& fresh) {
    std::vector<std::string> news;
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Alert& x) {
        if (x.src != src) return false;
        for (const auto& f : fresh) if (f.id == x.id) return false;
        return true;
    }), items_.end());
    for (const auto& f : fresh) if (upsert(f)) news.push_back(f.id);
    return news;
}

void AlertList::expire(int64_t now) {
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Alert& x) { return !x.activeAt(now); }), items_.end());
}

bool AlertList::remove(const std::string& id) {
    for (size_t i = 0; i < items_.size(); i++) if (items_[i].id == id) { items_.erase(items_.begin() + (long)i); return true; }
    return false;
}

const Alert* AlertList::find(const std::string& id) const {
    for (const auto& x : items_) if (x.id == id) return &x;
    return nullptr;
}

int AlertList::maxLevel() const {
    int m = -1;
    for (const auto& x : items_) m = std::max(m, (int)x.level);
    return m;
}

void AlertList::sort() {
    std::stable_sort(items_.begin(), items_.end(), [](const Alert& a, const Alert& b) {
        if (a.level != b.level) return a.level > b.level;
        return a.onset < b.onset;
    });
}

bool alertZoneMatch(const std::string& area, const std::string& zones) {
    if (trim(zones).empty()) return true;
    std::string a = asciiFold(area);
    for (char& c : a) c = (char)std::tolower((unsigned char)c);
    size_t p = 0;
    while (p <= zones.size()) {
        size_t e = zones.find(',', p);
        if (e == std::string::npos) e = zones.size();
        std::string z = asciiFold(trim(zones.substr(p, e - p)));
        for (char& c : z) c = (char)std::tolower((unsigned char)c);
        if (!z.empty() && a.find(z) != std::string::npos) return true;
        p = e + 1;
    }
    return false;
}

}  // namespace plotter
