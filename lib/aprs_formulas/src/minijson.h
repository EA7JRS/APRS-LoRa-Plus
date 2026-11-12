/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Lector de JSON pequeno y sin dependencias (arbol en memoria) para el nucleo: respuestas de servicios web que se
// prueban en el PC (SWPC, NWS). Admite todo JSON valido; ante un error devuelve false y deja el arbol vacio.
#ifndef PLOTTER_MINIJSON_H_
#define PLOTTER_MINIJSON_H_

#include <string>
#include <utility>
#include <vector>

namespace plotter {

struct JsonValue {
    enum Type : unsigned char { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool boolean = false;
    double number = 0;
    std::string text;                                           // String
    std::vector<JsonValue> items;                               // Array
    std::vector<std::pair<std::string, JsonValue>> members;     // Object

    bool isNull() const { return type == Null; }
    bool isNumber() const { return type == Number; }
    bool isString() const { return type == String; }
    bool isArray() const { return type == Array; }
    bool isObject() const { return type == Object; }
    size_t size() const { return type == Array ? items.size() : type == Object ? members.size() : 0; }
    // Miembro o elemento; si no existe, un valor nulo (se pueden encadenar: v["a"]["b"][0])
    const JsonValue& operator[](const char* key) const;
    const JsonValue& operator[](size_t index) const;
    const JsonValue& operator[](int index) const { return (*this)[(size_t)(index < 0 ? 0 : index)]; }
    bool has(const char* key) const;
    // Numero (tambien de una cadena con un numero, "182") o "fallback"
    double num(double fallback = 0) const;
    // Cadena (los numeros salen con su texto corto) o "fallback"
    std::string str(const char* fallback = "") const;
};

bool jsonParse(const std::string& text, JsonValue& out);

}  // namespace plotter
#endif
