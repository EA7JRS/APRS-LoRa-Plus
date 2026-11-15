/*
 * Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter (lo copian APRS LoRa+ Display y el rotor).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * gunzip sin dependencias (C++ puro): descomprime un archivo gzip que ya esta en memoria (deflate, RFC 1950 / 1951 / 1952)
 * y entrega la salida por trozos, con una ventana de 32 KB. Admite varios miembros seguidos y comprueba el CRC-32.
 * Escrito para los avisos de AEMET (un .tar.gz con todos los CAP en vigor); sirve para cualquier gzip.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace gz {

using Sink = std::function<void(const uint8_t* data, size_t n)>;

// 0 = bien; negativo = error (-1 no es gzip, -2 datos cortados, -3 datos no validos, -4 CRC, -5 sin memoria)
int gunzip(const uint8_t* in, size_t n, const Sink& out);
const char* errorText(int code);

} // namespace gz
