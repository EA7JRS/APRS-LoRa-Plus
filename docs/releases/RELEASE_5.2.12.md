# Versión 5.2.12 (2026-11-01)

Incluye todo lo de 5.2.11 (ver `RELEASE_5.2.11.md`: modo DAPNET, calibración de frecuencia en ppm, pantalla CW BEACON, campanas APRS, cada modo emite solo lo suyo, selector de arranque sin solapes) más lo siguiente.

## Selector GFSK / GMSK por perfil FSK
Nuevo campo `fskShaping` en cada perfil LoRa/FSK (0 sin filtro, 1 GFSK BT 0,5 por defecto, 2 GMSK BT 0,3, 3 BT 0,7, 4 BT 1,0), en la WebUI («Modulation / Gaussian filter (TX)») y en `tracker_conf.json`. `LoRa_Utils::fskShapingValue()` lo traduce a `RADIOLIB_SHAPING_*` y se aplica con `setDataShaping()` al abrir el módem FSK y al reconfigurar el perfil. El valor por defecto conserva el comportamiento anterior (BT 0,5). Para GMSK propiamente dicho, desviación = velocidad / 4. El receptor no depende del ajuste; DAPNET no usa filtro.
- Pendiente de verificar en el aire con un SDR (espectro y decodificación en el otro extremo). Sin compilar en PlatformIO.

## WebUI
- El bloque **Quectel L76K** se mueve al final de la sección GPS (tras Navigation y PPS).
- Cabecera: **APRS LoRa+** y, debajo, `Tracker <versión>`; título de la página «APRS LoRa+ Tracker».
- About: se conserva toda la información original (atribución a CA2RXU incluida) y se añade una línea `APRS LoRa+ - by Manuel EA7JRS` entre la información de compilación y la de CA2RXU. La versión de la cabecera se pide al nuevo endpoint `/version` (`versionNumber` del firmware).
- Pruebas: WebUI con el servidor simulado (carga y envío de `fskShaping`, orden de los grupos GPS, cabecera y About), sin errores de JavaScript.

## Versión
`versionNumber` 5.2.12, fecha 2026-11-01 y mensaje de arranque por el puerto serie actualizados.

## Mensajería APRS (LoRa y FSK): abrir y contestar
Revisada la mensajería: la recepción, el registro y la respuesta rápida ya eran comunes a APRS LoRa y APRS FSK (las tramas FSK se reinyectan en el mismo camino que las LoRa), pero abrir un mensaje exigía descubrir *Browse* en el menú, y la lista no indicaba cómo. Ahora:
- Submenú de MESSAGES: **Read / reply** (abre directamente el mensaje recibido sin leer más reciente, o el último recibido y lo marca como leído), *Browse list*, *Mark all read*, *Delete all*, *Back*. Sin mensajes solo aparece *Back*.
- La lista muestra `N new  Hold: read` (o `Hold: read / reply`), y el detalle `Hold: quick reply` / `Hold: reply` si el texto deja libre la última línea.
- Camino corto para contestar: LP → LP (*Read / reply*) → LP (menú del mensaje) → *Quick reply* → 1P → LP.
- Corregido: un mensaje igual al anterior pero de **otra estación** (p. ej. «QSL» seguido de «QSL») se descartaba como repetido; ahora se compara remitente y texto.
- Sin compilar en PlatformIO ni probado en placa.

## Compatibilidad con todas las placas
Revisión de compatibilidad: ver el capítulo 28 del manual (tabla por chip: SX1262/SX1268, SX1276/SX1278 y LLCC68; placas sin botón, sin pantalla APRS y con TFT).
- `lora_utils.cpp`: las funciones auxiliares FSK (`fskBlockLen`, `fskShapingValue`, `fskBitRateKbps`, `fskDevKHz`, `fskRxBwKHz`) pasan a `static inline` y `fskShapingApplied` a `[[maybe_unused]]`: en las placas LLCC68 (sin FSK) solo se usan dentro de bloques desactivados y, con `-Werror -Wall`, habrían dado error de «definida pero no usada».
- SX127x: BT 0,7 no existe en el chip y se sustituye por BT 0,5; el filtro solo se reprograma si cambia (en el SX127x pasa por standby) y se rearma la recepción.
- WebUI: los textos de DAPNET y GFSK/GMSK indican en qué chips funcionan.
- Revisado con `-Wall` en PC para SX1262, SX1278, SX1276 y LLCC68 (sin avisos nuevos); sin compilar en PlatformIO ni probado en placa.

## Corrección de compilación (Arduino-ESP32 3.3.12, GCC 14)
`include/pocsag.h`: el método `Decoder::bit()` chocaba con la macro `bit(b)` de `Arduino.h` («macro "bit" passed 3 arguments») y rompía `dapnet_utils.cpp`, `lora_utils.cpp` y `pages_utils.cpp`. Renombrado a `pushBit()`, y el parámetro `max` de `parseRicList()` a `maxCount` por la misma razón. Comprobado el encabezado con las macros de Arduino (`bit`, `min`, `max`, `abs`, `round`, `word`…) definidas y repetida la prueba unitaria (fails=0). Compilación real pendiente de confirmar por el usuario.
- `src/dapnet_utils.cpp`: el texto de la hora (`timeText`) pasa de 24 a 40 bytes; el compilador de Arduino-ESP32 3.3.12 (GCC 14) con `-Werror` daba `-Wformat-truncation` al no poder acotar los campos de la hora recibida. Revisados con `-O2 -Wformat-truncation -Wformat-overflow` los demás ficheros cambiados (sin avisos nuevos).


## Corrección en ppm con tres decimales (calibración fina)

- La corrección global del oscilador pasa de centésimas a **milésimas de ppm** (`radioPpm1000`, ±50,000 ppm). La WebUI admite `0.001` y el botón «Apply to ppm» da tres decimales; la pantalla CW TUNE muestra `Correction %+.3f ppm`. Las configuraciones de 5.2.11 (`radioPpm100`) se migran solas (×10) al arrancar.
- **Por qué no bastaba con el campo:** RadioLib calcula el registro de frecuencia del SX126x con un `float` (≈ 30 Hz de resolución a 433 MHz), de modo que las correcciones finas se perdían. Ahora, tras cada `setFrequency()`/`begin()` en SX1262/SX1268 (APRS LoRa, APRS FSK, CW, escucha estrecha de CW), `LoRa_Utils::refineFrequency()` reescribe el PLL con la palabra exacta (`Hz × 2²⁵ / 32 MHz`, paso 0,954 Hz). En SX127x el paso del chip es ≈ 61 Hz. WSPR, DAPNET y LR-FHSS no cambian (LR-FHSS sigue desactivado).
- Corrección: las versiones anteriores de los documentos hablaban de una resolución de ±4 Hz; el límite real era ≈ 30 Hz por el `float`.
- Prueba en PC de la aritmética del PLL: error < 0,5 Hz en todo el rango ±2 ppm a 432,410 MHz. Sin compilar en PlatformIO ni probado en placa: **tras flashear hay que volver a medir** con el SDR (el valor guardado se conserva, pero la frecuencia real cambiará porque ahora sí se aplica entera).

## Créditos e identidad (APRS LoRa+)

- Pantalla de bienvenida: arriba a la izquierda aparece ahora **EA7JRS** (antes CA2RXU); también en la pantalla de bienvenida clásica de las placas sin páginas. Los mensajes del puerto serie citan «APRS LoRa+ by Manuel EA7JRS (base: CA2RXU LoRa APRS Tracker)».
- WebUI → About: la línea «APRS LoRa+ - by Manuel EA7JRS» lleva ahora «© 2026 Manuel - EA7JRS»; se conserva la línea original de CA2RXU y se añade la cadena de créditos (OE5BPA, DL9SAU, DC2MH, ON4AA; GPL-3.0). Los dos enlaces de apoyo (GitHub Sponsors y PayPal de CA2RXU) quedan **ocultos** (`d-none`, bloque `supportLinks` en `index.html`): para poner enlaces propios basta cambiar los `href` y quitar `d-none`.
- README: nuevo título «APRS LoRa+» y sección «Cadena de créditos». Los avisos de copyright de los fuentes de CA2RXU no se tocan (GPLv3).

## Corrección tras el primer CI en GitHub (ttgo-t-beam-v1_2)

- `pages_utils.cpp` (pantalla GPS): `snprintf(sats, …, "%u/%d", …)` con un búfer de 12 bytes daba `-Werror=format-truncation` en las placas sin `GPS_CONFIG_CONSTELLATIONS` (GCC 14 de GitHub). Búfer de `sats` a 24 bytes y de `line` a 48.
- `common_settings.ini`: `-Wno-error=format-truncation`. El truncado de texto en `snprintf` es habitual y deliberado al ajustar el texto a la pantalla; el aviso se sigue mostrando, pero ya no detiene la compilación. El resto de avisos sigue siendo un error (`-Werror -Wall`).
