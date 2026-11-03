# Changelog

## 5.3.1 — en desarrollo (sin compilar ni publicar)
APRS FSK 9600: nuevo selector **Packet protocol (TX)** por perfil FSK: **AX.25** (como hasta ahora), **FX.25** (corrección de errores Reed-Solomon con 16, 32 o 64 bytes de control; sigue siendo AX.25 para las estaciones que no lo conocen) o **IL2P** (cabecera compacta + Reed-Solomon; solo la oyen estaciones IL2P). La recepción entiende AX.25, FX.25 e IL2P a la vez, sin ajustes. Codificadores y decodificadores escritos desde las especificaciones y verificados en PC contra Dire Wolf (GPL-2.0 o posterior): los 11 formatos FX.25 en los dos sentidos y 12 tramas IL2P idénticas byte a byte. Sin probar en placa. Las tramas que no caben en un bloque de 255 bytes se emiten como AX.25 normal. Ver `docs/FSK_G3RUH.md`.

**Instalación y actualización del firmware (5.3.1):**
- **Instalador web USB** (`web/index.html`, publicado en GitHub Pages): conecta la placa por USB, pulsa «Detectar placa» y la página lee placa y versión instalada, la compara con la última publicada e instala con un clic (Chrome/Edge; ordenador o Android). Conserva la configuración (no escribe la partición de datos). Los manifiestos y las imágenes los genera el CI (`tools/make_web_assets.py`) y **solo se publican con una release estable** (no con pre-releases ni ejecuciones manuales).
- **La placa se identifica**: línea `APRS-LoRa-Plus: board=… version=… chip=… ota=…` al arrancar y como respuesta a `APRS-INFO?` por el puerto serie; `GET /api/info` en la WebUI.
- **Actualización desde la WebUI** (Administración > Firmware update), solo en placas con dos copias de firmware (T-Beam S3 Supreme, T-Beam 1W): subir un `.bin` (se rechaza si es de otra placa) o «Check for updates / Install» por internet usando tu WiFi (comprobación SHA-256 y de tamaño antes de activar).
- **Actualización automática** opcional: en cada arranque la placa mira tu WiFi, busca una versión más nueva y, si lo permites, la instala. No actúa si el AP de configuración está siempre activo.
- Placas sin segunda copia (ESP32 clásicas, ESP32-C3, Heltec S3, T3S3…): solo por el instalador USB. T-Deck: sin esta función (núcleo Arduino antiguo).
- Sin probar en placa y sin compilar. Si una imagen defectuosa se instala, el arranque falla y hay que reinstalar por USB (el bootloader de Arduino no vuelve atrás solo).

## 5.3.0 — 2026-11-03
Nuevos modos **FT8 beacon**, **FT4 beacon** y **FST4W beacon** (120 y 300 s; solo transmisión, SX1262/SX1268, ranura sincronizada por GPS, GFSK generado por software; codificador de ft8_lib, MIT). Tarjeta propia en la WebUI con región IARU y aviso de segmento; enlace de GitHub de APRS LoRa+ visible en About; estudio de cobertura de todos los modos (`docs/ESTUDIO_COBERTURA.pdf`). LR-FHSS se puede activar desde la WebUI (apagado por defecto). D-Star/DPRS queda para 5.4. El código FT/FST4W solo se enlaza en placas SX1262/SX1268. Codificadores verificados en PC; sin probar en placa. NimBLE 2.5.2 y partición de 3,75 MiB en las ESP32 clásicas. Ver `docs/releases/RELEASE_5.3.0.md`.

## 5.2.12 — 2026-11-01
Selector GFSK/GMSK (filtro gaussiano del emisor) por perfil FSK; el bloque del L76K pasa al final de la sección GPS de la WebUI; cabecera «APRS LoRa+ / Tracker versión» y línea «APRS LoRa+ - by Manuel EA7JRS» añadida en About (la información original se conserva; la versión de la cabecera se lee del firmware). Incluye todo lo de 5.2.11 (modo DAPNET, calibración ppm, pantalla CW BEACON, campanas, modo estricto). Ver `docs/releases/RELEASE_5.2.12.md`.

## 5.2.11 — 2026-10-31
WebUI reorganizada: sección GPS (constelaciones, SBAS, AssistNow, PPS…; u-blox M10 y L76K), «Transmission mode» con un grupo por modo, «Beacon User Profile». APRS FSK con región/canal/frecuencia/potencia propios; CW con dos perfiles (Beacon y Telegrafía); WSPR con frecuencia fija; CAD mejorado (backoff exponencial, escucha por RSSI en FSK); baliza LR-FHSS restaurada y desactivada. Corregido el arranque de APRS FSK en LoRa. Soporte específico del Quectel L76K (constelaciones, 1–5 Hz, NMEA reducida, estado de antena). Campanas APRS (melodías tipo Yaesu FTM-400DR) para zumbador pasivo KY-006. Pantalla CW BEACON propia en modo MORSE. Calibración de frecuencia global en ppm para todos los modos. Cada modo emite solo lo suyo (sin paquetes APRS en CW/WSPR/LR-FHSS). Nuevo modo DAPNET (receptor POCSAG, solo recepción) y selector de modo del arranque sin solapes. Ver `docs/releases/RELEASE_5.2.11.md`.

## 5.2.10 — 2026-10-29
WSPR: el afinado de tonos deja de modular el PLL a 1 kHz (causaba un peine de líneas cada 50 Hz en la señal); cada símbolo se emite en dos tramos con un solo cambio. Nuevas opciones «Screen off while transmitting» y «Drift compensation», tabla de calibración por potencia y registro de temperaturas internas por serie. Ver `docs/releases/RELEASE_5.2.10.md`.


## 5.2.9 — 2026-10-27
Cabeceras de copyright de Manuel - EA7JRS en el código propio. Ver `docs/releases/RELEASE_5.2.9.md`.

## 5.2.8 — 2026-10-27
CW: antirrebote de 12 ms en el decodificador y límite superior para adaptar la velocidad. Ver `docs/releases/RELEASE_5.2.8.md`.

## 5.2.7 — 2026-10-27
WSPR: GPS siempre activo en modo WSPR, UART del GPS vaciada durante la emisión, locator con la última posición conocida, restauración limpia del SX1262. Ver `docs/releases/RELEASE_5.2.7.md`.

## 5.2.6 — 2026-10-27
WSPR: el índice de ranura se reinicia a las 00:00 UTC en `msUntilSlot()` (revisión externa). Ver `docs/releases/RELEASE_5.2.6.md`.

## 5.2.5 — 2026-10-26
WSPR: delay(1) cada 20 ms en la emisión (evita el watchdog; aportación del usuario); Supreme: guarda ifndef para BOARD_HAS_PSRAM. Ver `docs/releases/RELEASE_5.2.5.md`.

## 5.2.4 — 2026-10-26
Supreme: PSRAM QSPI activada en el entorno (la pantalla SYSTEM mostraba n/a). Ver `docs/releases/RELEASE_5.2.4.md`.

## 5.2.3 — 2026-10-26
TELEMETRY: matriz de pronóstico, base de nubes, tipo de aire y helada nocturna; línea de gas oculta sin BME680. Ver `docs/releases/RELEASE_5.2.3.md`.

## 5.2.2 — 2026-10-26
Identificación de equipos actualizada con la lista oficial aprs-deviceid (440 tocalls, 26 sufijos Mic-E, 6 prefijos Mic-E clásicos). Ver `docs/releases/RELEASE_5.2.2.md`.

## 5.2.1 — 2026-10-26
Iconos APRS en LAST APRS; velocidad vertical 0 con el tracker parado. Ver `docs/releases/RELEASE_5.2.1.md`.

## 5.2.0 — 2026-10-26
Modo WSPR beacon (pantalla WSPR, GPS, calibración de frecuencia). Ver `docs/releases/RELEASE_5.2.0.md`.

## 5.1.0 — 2026-10-25
Selector de modo, FSK 9600 G3RUH, plan de bandas IARU, modo MORSE (macros, manipulador, escucha RF, trainer). Ver `docs/releases/RELEASE_5.1.0.md`.

## 5.0.0
Interfaz de 11 pantallas APRS con un botón, alarmas y baliza SOS (EA7JRS) sobre CA2RXU 4.0.1 para T-Beam S3 Supreme.
