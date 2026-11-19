/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once

// Destino (tocall) de las tramas APRS que genera el tracker: identifica el dispositivo en aprs.fi y demas. Asignado por el
// registro oficial aprsorg/aprs-deviceid el 2026-11-16: APLJRT = APRS LoRa+ (tracker), APLJRA = APRS LoRa+ for Android.
// Antes "APLRT1" (el de CA2RXU) y, hasta la 5.4.6, "APZLRT" (rango experimental). El rotor sigue con APZLRR.
#define APRS_TOCALL "APLJRT"

// Codigo de equipo Mic-E (2 caracteres al final del comentario, aprs-deviceid "mice"): en Mic-E el destino lleva la latitud y el
// tocall no identifica el equipo. Pedido al registro el 2026-11-19 (propuesto "^J"); vacio = no se manda hasta que lo asignen.
#define APRS_MICE_SUFFIX ""
