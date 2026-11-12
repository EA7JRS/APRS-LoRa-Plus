/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "minijson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace plotter {

namespace {

const JsonValue kNull;

struct Parser {
    const char* p;
    const char* end;
    int depth = 0;

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    bool lit(const char* s) {
        size_t n = std::strlen(s);
        if ((size_t)(end - p) < n || std::strncmp(p, s, n) != 0) return false;
        p += n;
        return true;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool hex4(unsigned& v) {
        if (end - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; i++) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (p >= end || *p != '"') return false;
        p++;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c != '\\') { out += c; continue; }
            if (p >= end) return false;
            char e = *p++;
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned lo;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        if (p >= end) return false;
        p++;
        return true;
    }
    bool value(JsonValue& v) {
        if (++depth > 64) return false;
        ws();
        if (p >= end) return false;
        bool ok = true;
        if (*p == '{') {
            p++;
            v.type = JsonValue::Object;
            ws();
            if (p < end && *p == '}') { p++; depth--; return true; }
            while (ok) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (p >= end || *p != ':') return false;
                p++;
                v.members.emplace_back(std::move(key), JsonValue());
                if (!value(v.members.back().second)) return false;
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; break; }
                return false;
            }
        } else if (*p == '[') {
            p++;
            v.type = JsonValue::Array;
            ws();
            if (p < end && *p == ']') { p++; depth--; return true; }
            while (true) {
                v.items.emplace_back();
                if (!value(v.items.back())) return false;
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; break; }
                return false;
            }
        } else if (*p == '"') {
            v.type = JsonValue::String;
            ok = string(v.text);
        } else if (lit("true")) { v.type = JsonValue::Bool; v.boolean = true; }
        else if (lit("false")) { v.type = JsonValue::Bool; v.boolean = false; }
        else if (lit("null")) { v.type = JsonValue::Null; }
        else {
            const char* s = p;
            if (p < end && (*p == '-' || *p == '+')) p++;
            while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+')) p++;
            if (p == s) return false;
            v.type = JsonValue::Number;
            v.text.assign(s, (size_t)(p - s));
            v.number = std::strtod(v.text.c_str(), nullptr);
        }
        depth--;
        return ok;
    }
};

}  // namespace

const JsonValue& JsonValue::operator[](const char* key) const {
    if (type != Object) return kNull;
    for (const auto& m : members) if (m.first == key) return m.second;
    return kNull;
}

const JsonValue& JsonValue::operator[](size_t index) const {
    if (type != Array || index >= items.size()) return kNull;
    return items[index];
}

bool JsonValue::has(const char* key) const {
    if (type != Object) return false;
    for (const auto& m : members) if (m.first == key) return true;
    return false;
}

double JsonValue::num(double fallback) const {
    if (type == Number) return number;
    if (type == String && !text.empty()) {
        char* e = nullptr;
        double v = std::strtod(text.c_str(), &e);
        if (e != text.c_str()) return v;
    }
    return fallback;
}

std::string JsonValue::str(const char* fallback) const {
    if (type == String || type == Number) return text;
    if (type == Bool) return boolean ? "true" : "false";
    return fallback;
}

bool jsonParse(const std::string& text, JsonValue& out) {
    out = JsonValue();
    Parser ps{text.data(), text.data() + text.size()};
    if (!ps.value(out)) { out = JsonValue(); return false; }
    ps.ws();
    if (ps.p != ps.end) { out = JsonValue(); return false; }
    return true;
}

}  // namespace plotter
