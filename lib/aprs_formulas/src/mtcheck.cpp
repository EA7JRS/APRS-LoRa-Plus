/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "mtcheck.h"
#include "minijson.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace plotter {
namespace mtcheck {

const char* const MARS_URL = "https://www.itu.int/mmsapp/ShipStation/list";

static void clean(const char* in, char* out, size_t size) {     // A-Z 0-9, maximo 7
    size_t n = 0;
    for (; in && *in && n + 1 < size && n < 7; in++) {
        const char c = (char)toupper((unsigned char)*in);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out[n++] = c;
    }
    out[n] = 0;
}

bool validKey(const char* key) {
    if (!key || strlen(key) != 40) return false;
    for (const char* p = key; *p; p++) if (!isxdigit((unsigned char)*p)) return false;
    return true;
}

std::string searchUrl(const char* key, uint32_t mmsi) {
    char b[160];
    snprintf(b, sizeof(b), "https://services.marinetraffic.com/api/shipsearch/%s?mmsi=%lu&protocol=jsono", key ? key : "", (unsigned long)mmsi);
    return b;
}

static bool hasWord(const std::string& s, const char* w) {
    std::string u(s);
    for (char& c : u) c = (char)toupper((unsigned char)c);
    return u.find(w) != std::string::npos;
}

Result parse(int httpCode, const std::string& body, const char* call, char* mtCall, size_t size) {
    if (size) mtCall[0] = 0;
    if (httpCode == 429) return LIMIT;
    if (httpCode == 401 || httpCode == 403) return BAD_KEY;
    JsonValue v;
    if (!jsonParse(body, v)) return FAILED;
    // Error de la API: {"errors":[{"code":"...","detail":"..."}]}
    const JsonValue& errs = v.isObject() ? v["errors"] : v;
    if (v.isObject() && errs.isArray()) {
        std::string d;
        for (size_t i = 0; i < errs.size(); i++) d += errs[i]["detail"].str() + " " + errs[i]["code"].str() + " ";
        if (hasWord(d, "KEY")) return BAD_KEY;
        if (hasWord(d, "RATE") || hasWord(d, "LIMIT") || hasWord(d, "CREDIT")) return LIMIT;
        if (hasWord(d, "NO DATA") || hasWord(d, "NOT FOUND") || hasWord(d, "NO RESULT")) return NOT_FOUND;
        return FAILED;
    }
    if (httpCode != 200) return FAILED;
    const JsonValue* list = &v;
    if (v.isObject() && v["DATA"].isArray()) list = &v["DATA"];
    if (!list->isArray()) return FAILED;
    char want[8];
    clean(call, want, sizeof(want));
    bool any = false;
    for (size_t i = 0; i < list->size(); i++) {
        char got[8];
        clean((*list)[i]["CALLSIGN"].str().c_str(), got, sizeof(got));
        if (!got[0]) continue;
        if (want[0] && strcmp(got, want) == 0) { snprintf(mtCall, size, "%s", got); return MATCH; }
        if (!any) { snprintf(mtCall, size, "%s", got); any = true; }
    }
    return any ? MISMATCH : NOT_FOUND;
}

const char* resultEs(Result r) {
    switch (r) {
        case MATCH:     return "Validado en MarineTraffic";
        case MISMATCH:  return "MarineTraffic tiene otro distintivo para este MMSI";
        case NOT_FOUND: return "Este MMSI no est\xC3\xA1 en MarineTraffic: compru\xC3\xA9" "balo en el MARS de la UIT";
        case BAD_KEY:   return "Clave de MarineTraffic no v\xC3\xA1lida";
        case LIMIT:     return "MarineTraffic: cupo de consultas agotado";
        default:        return "Sin respuesta de MarineTraffic";
    }
}

const char* resultEn(Result r) {
    switch (r) {
        case MATCH:     return "Verified in MarineTraffic";
        case MISMATCH:  return "MarineTraffic has another call sign for this MMSI";
        case NOT_FOUND: return "This MMSI is not in MarineTraffic: check it in the ITU MARS";
        case BAD_KEY:   return "MarineTraffic key not valid";
        case LIMIT:     return "MarineTraffic: call quota used up";
        default:        return "No answer from MarineTraffic";
    }
}

void pairKey(uint32_t mmsi, const char* call, char* out, size_t size) {
    char c[8];
    clean(call, c, sizeof(c));
    snprintf(out, size, "%09lu/%s", (unsigned long)mmsi, c);
}

bool verified(uint8_t source, const char* storedPair, uint32_t mmsi, const char* call) {
    if (source != MARINETRAFFIC && source != MARS) return false;
    char c[8];
    clean(call, c, sizeof(c));
    if (!c[0] || mmsi < 200000000UL || mmsi > 799999999UL) return false;
    char k[24];
    pairKey(mmsi, call, k, sizeof(k));
    return storedPair && strcmp(storedPair, k) == 0;
}

const char* sourceName(uint8_t s) { return s == MARINETRAFFIC ? "MarineTraffic" : (s == MARS ? "ITU MARS" : ""); }

}  // namespace mtcheck
}  // namespace plotter
