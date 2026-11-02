# LoRa APRS Tracker V5.2.1 — Notas de versión

**Base:** V5.2.0 · **Fecha:** 2026-10-26

## Cambios
- **LAST APRS (vista 1):** en lugar de la mini brújula se dibuja, en la esquina inferior derecha, el **icono APRS de 24×24** del símbolo de la estación, con las tablas primaria (`/`) y secundaria (`\\`) y la letra o el dígito de superposición en el centro (también los dígitos del formato comprimido, que viajan como a–j). Las filas 3 a 5 se acortan a 17 caracteres para dejarle sitio. Si la estación no tiene símbolo conocido se mantiene la mini brújula. Los iconos salen de las hojas aprs-symbols del iGate (`tools/gen_aprs_icons.py` genera `include/aprs_icons.h`, unos 14 KB de flash).
- **ALTIMETER:** la velocidad vertical marca `VS 0.0` cuando el tracker lleva parado 10 s (GPS por debajo de 1,5 km/h o acelerómetro en reposo, y sin indicios de movimiento), en lugar del ruido del barómetro o del GPS. Al ponerse en marcha vuelve a calcularse desde cero.
- Versión mostrada: 5.2.1.

## Verificación
Probado en el simulador 128×64 (10 símbolos incluidos superposición, sin símbolo, y la secuencia parado/marcha/parado del altímetro). No compilado con PlatformIO ni probado en la placa. Las imágenes se convirtieron a 1 bit con un umbral fijo: algunos símbolos muy detallados se ven simplificados.
