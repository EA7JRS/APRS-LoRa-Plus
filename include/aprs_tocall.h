/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once

// Destino (tocall) de las tramas APRS que genera el tracker: identifica el dispositivo en aprs.fi y demas (registro oficial
// aprsorg/aprs-deviceid). Antes "APLRT1", el de CA2RXU. Mientras no haya uno asignado a APRS LoRa+, rango experimental APZ
// ("APZLRT", libre en el registro; la app usa APZLRP y el rotor APZLRR). Al asignarlo, cambiar solo esta linea.
#define APRS_TOCALL "APZLRT"
