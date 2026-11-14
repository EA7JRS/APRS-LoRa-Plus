/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Comprobacion del distintivo de llamada maritimo del MMSI (Manuel, 2026-11-14: «validado con MarineTraffic»; en el plotter
// y en el transponder APRS LoRa+ MAPRS). MarineTraffic: busqueda por MMSI de su API (shipsearch, con la clave de pago del
// usuario; solo conoce barcos que ha recibido por AIS de verdad). Si no esta, el registro oficial es el MARS de la UIT
// (gratuito, sin API): se consulta a mano y se confirma. El distintivo vale para el par MMSI + distintivo comprobado: si
// cambia cualquiera de los dos, deja de estar validado. Copiado al tracker con tools/sync_formulas.sh.
#ifndef PLOTTER_MTCHECK_H_
#define PLOTTER_MTCHECK_H_

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace plotter {
namespace mtcheck {

extern const char* const MARS_URL;              // busqueda de estaciones de barco del MARS de la UIT

enum Source : uint8_t { NONE = 0, MARINETRAFFIC = 1, MARS = 2 };
enum Result { MATCH, MISMATCH, NOT_FOUND, BAD_KEY, LIMIT, FAILED };

bool        validKey(const char* key);          // 40 caracteres hexadecimales
std::string searchUrl(const char* key, uint32_t mmsi);
// Respuesta de shipsearch (protocol=jsono). mtCall: el distintivo que tiene MarineTraffic (si lo hay)
Result      parse(int httpCode, const std::string& body, const char* call, char* mtCall, size_t size);
const char* resultEs(Result r);
const char* resultEn(Result r);

// Par validado: "224123456/EBXY" (distintivo en mayusculas, solo A-Z 0-9)
void        pairKey(uint32_t mmsi, const char* call, char* out, size_t size);
bool        verified(uint8_t source, const char* storedPair, uint32_t mmsi, const char* call);
const char* sourceName(uint8_t source);         // "MarineTraffic", "ITU MARS", ""

}  // namespace mtcheck
}  // namespace plotter
#endif
