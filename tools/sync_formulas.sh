#!/usr/bin/env bash
# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copia en lib/aprs_formulas/ las formulas comunes de APRS LoRa+, que nacen en el nucleo del plotter
# (EA7JRS/APRS-LoRa-Plus-Plotter, lib/plotter_core, con sus pruebas): astro (sol y luna de Meeus), geo, wind (Beaufort)
# y wxcalc (punto de rocio, sensacion termica). No se editan aqui: se cambian en el plotter y se vuelven a copiar.
# Uso: tools/sync_formulas.sh <checkout del plotter>
set -euo pipefail
SRC=${1:?uso: tools/sync_formulas.sh <checkout de APRS-LoRa-Plus-Plotter>}/lib/plotter_core/src
DST=$(cd "$(dirname "$0")/.." && pwd)/lib/aprs_formulas
[ -f "$SRC/astro.h" ] || { echo "no encuentro $SRC"; exit 1; }
for f in astro geo wind wxcalc; do cp "$SRC/$f.h" "$SRC/$f.cpp" "$DST/src/"; done
rev=$(git -C "$SRC" rev-parse --short HEAD 2>/dev/null || echo "?")
printf 'Copia parcial de lib/plotter_core de EA7JRS/APRS-LoRa-Plus-Plotter, commit %s, con tools/sync_formulas.sh. No editar aqui.\n' "$rev" > "$DST/ORIGEN.txt"
echo "lib/aprs_formulas copiado del plotter ($rev)"
