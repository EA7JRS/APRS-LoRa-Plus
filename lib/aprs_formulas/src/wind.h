/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Escala Beaufort y anemometro de cazoletas por pulsos (interruptor de laminas).
#ifndef PLOTTER_WIND_H_
#define PLOTTER_WIND_H_

#include <cstdint>

namespace plotter {

int beaufort(double knots);                 // 0..12, con el viento redondeado al nudo
const char* beaufortNameEs(int force);      // "Calma", "Ventolina", ...

// Cuenta pulsos con filtro de rebotes y los convierte en nudos. Tiempos en microsegundos.
class PulseAnemometer {
   public:
    // knotsPerHz: nudos por cada pulso por segundo (1,2 de partida para cazoletas de 4 cm de radio, sin calibrar)
    explicit PulseAnemometer(double knotsPerHz = 1.2, uint32_t debounceUs = 5000, uint32_t windowUs = 3000000, uint32_t calmUs = 5000000);
    void setFactor(double knotsPerHz) { factor_ = knotsPerHz; }
    double factor() const { return factor_; }
    bool pulse(uint64_t nowUs);             // false si se descarta por rebote
    double knots(uint64_t nowUs);           // media de la ventana; 0 en calma
    double gustKnots() const { return gust_; }
    void resetGust() { gust_ = 0; }
    uint32_t pulseCount() const { return total_; }

   private:
    static constexpr int kMax = 256;
    uint64_t t_[kMax];
    int head_ = 0, n_ = 0;
    uint64_t last_ = 0;
    bool any_ = false;
    uint32_t total_ = 0;
    double factor_, gust_ = 0;
    uint32_t debounceUs_, windowUs_, calmUs_;
};

}  // namespace plotter
#endif
