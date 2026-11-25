# LoRa APRS Tracker V5.2.1 — pantallas APRS (notas técnicas)

*English summary: Meshtastic-style 11-page UI for the Tracker's main screen (MAIN, MESSAGES, LAST APRS, STATIONS, GPS, COMPASS, ALTIMETER, TELEMETRY, CLOCK, LORA, SYSTEM), one-button navigation, WMM2025 declination, QNH, sunrise/sunset, LoRa time-on-air, local forecast, received-emergency alarm with GO TO and a sent SOS beacon. Not built with PlatformIO and not tested on hardware: see section 5. User documentation: `docs/MANUAL_USUARIO.md/.pdf` and `docs/GUIA_RAPIDA.md/.pdf` (Spanish).*

Base: LoRa APRS Tracker V4.0.1 (CA2RXU) con las mejoras de T-Beam S3 Supreme de EA7JRS (`SUPREME_V3_IMPROVEMENTS_over_V4.0.1.md`). Esta entrega **solo añade** las pantallas; el código base es el del zip `LoRa_APRS_Tracker_V4.0.1_supreme_patched` de 22-oct-2026 (idéntico a la entrega anterior salvo los `.gz` web de `data_embed`, que se han incorporado).

## 1. Qué cambia

Las pantallas dejan de ser una opción del menú: son **la pantalla principal** (`menuDisplay == 0`). El menú clásico de configuración se abre con doble pulsación. Se activan en todas las placas con pantalla salvo `HELTEC_WSL_V3_TNC` (`include/display.h` define `HAS_APRS_PAGES`; `#define NO_APRS_PAGES` en `board_pinout.h` las desactiva en una placa). El lienzo lógico es de 128×64 monocromo (`pagesBegin/pagesText/...` en `display.cpp`); las OLED lo usan tal cual, la TFT de 160×80 lo centra y las de 320×240 lo escalan ×2.

## 2. Arquitectura

| Fichero | Contenido |
|---|---|
| `include/pages_core.h`, `src/pages_core.cpp` | Lógica **pura**, sin Arduino, probada en PC: geodesia, lista de estaciones (20), perfil de altitud, tendencia barométrica, fusión de rumbo, registro de mensajes, formatos y unidades, zona horaria y orto/ocaso (NOAA), tiempo en el aire (Semtech), S-meter, pronóstico, detección de alarmas, detector de gestos SOS |
| `include/pages_utils.h`, `src/pages_utils.cpp` | Dibujo de las 11 páginas, submenús, ajustes, hooks (`onAprsPacket`, `onTx`, `onRx`, `onDigipeat`, `onMessage*`), alarma y SOS |
| `include/wmm.h`, `src/wmm.cpp`, `include/wmm_coeffs.h`, `tools/wmm_cof_to_header.py` | Declinación magnética WMM2025 (armónicos esféricos); los coeficientes se generan del `WMM.COF` oficial |
| `src/gps_utils.cpp` | Máscara GNSS en tiempo de ejecución, SBAS, satélites a la vista (`GPS_CONFIG_CONSTELLATIONS`) |
| `src/baro_utils.cpp` | QNH manual (900–1090 hPa) además del automático |
| `src/mag_utils.cpp` | `isAligned()` |
| `src/lora_utils.cpp` | Potencia por perfil con guardado en `/loraPower.dat`, `getPower/powerMin/powerMax/setProfilePower`, hooks de TX |
| `src/msg_utils.cpp` | Hooks de RX y digipeater; LED de alarma |
| `src/button_utils.cpp` | Lectura en bruto del botón para el gesto SOS; se ignoran las pulsaciones de OneButton del mismo gesto |
| `src/station_utils.cpp` | `sendEmergencyBeacon()` (Mic-E `000` + baliza con «EMERGENCY SOS») |
| `src/sleep_utils.cpp` | El GPS no se duerme en emergencia |
| `src/notification_utils.cpp` | `alarmBeep()` y `countdownBeep()` |

Ficheros de datos en flash: `/pagesCfg.dat` (9 enteros, compatible hacia atrás), `/gps_gnss.dat`, `/loraPower.dat`, `/aprsMsgLog.txt`.

## 3. Detalles de funcionamiento

- **Estaciones:** 20 en RAM (se pierden al reiniciar). Un paquete sin posición no borra la última conocida. Tipo por símbolo y velocidad (≥ 3 km/h = móvil). Equipo por tocall o sufijo Mic-E; estado Mic-E (`000` = Emergency).
- **Altitud:** perfil de 96 puntos con decimación; ascenso/descenso con histéresis de 3 m. Altitud barométrica fusionada con GPS (`GPS_BARO_ALTITUDE`).
- **Tendencia:** una muestra cada 10 min, 3 h de historia (extrapolada desde 1 h).
- **Rumbo:** filtro complementario giroscopio + magnetómetro + rumbo GPS (> 8 km/h) trabajando en **norte verdadero**; el magnetómetro sin alineación aprendida se corrige con la declinación (WMM2025 o manual). Con `compassMode` se fuerza una fuente.
- **Tiempo en el aire:** fórmula de Semtech AN1200.13 (cabecera explícita, CRC, preámbulo 8). Cubos de 1 minuto para la última hora. Comprobada con 41,22 ms (SF7, 10 B) y 991,23 ms (SF12, 10 B).
- **S-meter:** S9 = −93 dBm, 6 dB por punto (VHF/UHF, > 30 MHz); por encima S9+dB.
- **Pronóstico:** reglas con presión (reducida a nivel del mar si se conoce la altitud), tendencia de 3 h, humedad y T−Td. Orientativo, no es un modelo meteorológico.
- **Alarma recibida:** Mic-E `000` o palabras completas SOS/EMERGENCY/MAYDAY (emergencia) y MOB/MAN OVERBOARD. Sirena cada 2,5 s, LED rápido (ligado a `ledMessage`), pantalla de alarma, GO TO con la última posición conocida. Misma estación: silencio 10 min; buzzer hasta 10 min.
- **SOS propio:** gesto (5 cortas < 300 ms, o `···−−−···` con raya 300–700 ms, huecos ≤ 1,2 s), cuenta atrás 5 s cancelable, dos paquetes cada 30 s hasta cancelar, GPS forzado.

## 4. Cómo se verificó

| Comprobación | Resultado |
|---|---|
| Pruebas de `pages_core` en PC (estaciones, mensajes, perfil de altitud, tendencia, IAQ, formatos, zona horaria, orto/ocaso, parseo WX, dispositivos, unidades y coordenadas) | ✅ sin fallos |
| Fusión de rumbo en PC | ✅ sin fallos |
| WMM2025 contra una implementación independiente en Python | ✅ coinciden |
| QNH manual (`baro_utils`) en PC | ✅ |
| Tiempo en el aire, bitrate, S-meter (tabla completa S1–S9+20), tracker de la última hora | ✅ |
| Pronóstico (reglas) y detección de alarmas (palabras completas) | ✅ |
| Detector de gestos SOS (5 cortas, SOS, incompletos, pausas largas, pulsación larga) | ✅ |
| Dibujo de las páginas y submenús en un simulador 128×64 (capturas en `docs/img`) | ✅ revisado visualmente |
| `pages_utils.cpp` con `-Wall -Wextra` contra stubs | ✅ sin avisos |
| Compilación completa con PlatformIO | ⬜ imposible en el entorno de desarrollo |
| `gps_utils.cpp` (constelaciones/SBAS) | ⬜ no se pudo compilar |
| En la placa: botón y gestos, SOS por radio, pitidos y LED, SPIFFS, refresco de brújula, cambio de constelaciones, potencia de LoRa | ⬜ pendiente |

## 5. Limitaciones conocidas

- Sin compilar con PlatformIO y sin probar en hardware (arriba). Primero compila y prueba con un segundo equipo cerca antes de fiarte del SOS.
- Backends TFT (Heltec Wireless Tracker, T-Deck) solo con comprobación de sintaxis.
- `HELTEC_WSL_V3_TNC` no tiene pantalla.
- La lista de estaciones no se guarda; las posiciones de **objetos** APRS no se muestran (la biblioteca las pone a cero); códigos de equipo Yaesu/Kenwood tomados de memoria.
- Cambiar de constelación reinicia el receptor y puede perder el fix unos segundos.
- La potencia guardada (`/loraPower.dat`) manda sobre la de la configuración web para ese perfil.
- El AIS marino no se decodifica por radio: la alarma MOB reacciona a mensajes APRS con esas palabras.
- La pantalla de alarma cerrada no se puede reabrir.
- El pronóstico es local y orientativo.
- Al hacer el gesto SOS despacio pueden abrirse brevemente menús intermedios (WiFi AP con 3 o 4 pulsaciones); al reconocer el SOS todo vuelve a MAIN.

## 6. Documentación

- `docs/GUIA_RAPIDA.md` / `.pdf` — guía rápida (2 páginas).
- `docs/MANUAL_USUARIO.md` / `.pdf` — manual de usuario.
- `docs/img/` — capturas simuladas de las pantallas.

## 7. Librerías: base de CA2RXU y APRS LoRa+

**Antes** = las que lleva el tracker de Ricardo Guzmán (CA2RXU), LoRa APRS Tracker 4.0.1 del 2026-10-07, del que parte el proyecto. **Ahora** = las que lleva APRS LoRa+ 5.4.9 a 2026-11-24 (`common_settings.ini` y los `platformio.ini` de `variants/`).

| Librería | Antes (CA2RXU 4.0.1) | Ahora (APRS LoRa+ 5.4.9) | Nota |
|---|---|---|---|
| Plataforma `espressif32` | 6.12.0 | **8.0.0** | Arduino core 3.3.x (IDF 5.5). `playTone` adaptado a la API LEDC del core 3 (`ledcAttach` / `ledcDetach`). Las variantes T-Deck siguen en 6.3.1 en los dos. |
| RadioLib | 7.6.0 | **8.0.0** | Desaparece `radio.XTAL` (0 V de TCXO = XTAL) y en la 8.0.0 `begin*()` pasa a structs y los ajustes a enteros: capa `include/radiolib_compat.h` (`RLCompat::*`), en la rama `radiolib-8` pendiente de prueba en placa (ver 7.2). |
| NimBLE-Arduino | 1.4.1 | **2.5.2** | Cambio de API: callbacks con `NimBLEConnInfo&` (y `reason` en `onDisconnect`), `service->start()` ya no hace falta, `enableScanResponse` y `setPreferredParams`. Portado en `ble_utils.cpp`; sobre ella va el servicio de control por BLE. **Exige C++17** (`-std=gnu++17`). |
| Adafruit GFX Library | 1.12.6 | **1.13.0** | Solo correcciones (esp8266, nRF52840, canvas). |
| XPowersLib | 0.2.4 | **0.3.3** | La inicialización del AXP192 / AXP2101 es idéntica. `getTemperature()` puede devolver `NAN` si la lectura no es válida. |
| Adafruit BusIO | 1.17.4 | 1.17.4 | Sin cambio. |
| Adafruit Unified Sensor | 1.1.15 | 1.1.15 | Sin cambio. |
| Adafruit BME280 / BMP280 / BME680 | 2.3.0 / 3.0.0 / 2.0.6 | 2.3.0 / 3.0.0 / 2.0.6 | Sin cambio. |
| Adafruit SHTC3 | 1.0.2 | 1.0.2 | Sin cambio (una variante). |
| Adafruit SSD1306 / SH110X | 2.5.17 / 2.1.15 | 2.5.17 / 2.1.15 | Sin cambio. |
| TFT_eSPI | 2.5.43 | 2.5.43 | Sin cambio (placas con TFT). |
| ArduinoJson | 7.4.3 | 7.4.3 | Sin cambio. |
| AsyncTCP | 3.5.0 | **3.6.1** | Mantenimiento de ESP32Async. |
| ESPAsyncWebServer | 3.12.1 | 3.12.1 | Sin cambio. |
| TinyGPSPlus | 1.1.0 | 1.1.0 | Sin cambio. |
| Time | 1.6.1 | 1.6.1 | Sin cambio. |
| esp-logger | 1.0.0 | 1.0.0 | Sin cambio. |
| APRSPacketLib (CA2RXU) | 1.0.10 | 1.0.10 | Sin cambio. |
| OneButton (shaggydog) | 1.5.0 | 1.5.0 | Sin cambio. |

Propias de APRS LoRa+, dentro del repositorio (`lib/`): `aprs_formulas` (astronomía, viento y meteorología, compartidas con el plotter), `ft8lib` (FT8 / FT4) y `libm17` (M17).

Opciones de compilación que cambian respecto a la base: `-std=gnu++17`; `-Wno-error=cpp`, `implicit-fallthrough`, `format-truncation`, `int-in-bool-context` y `misleading-indentation` (avisos de las librerías con el compilador nuevo, que con `-Werror` paraban la compilación); y NimBLE sin los papeles de central y observador.

**Compilado y probado:** todas las placas compilan en la CI con estas versiones y el firmware está probado en placa.

### 7.1 Ajustes tras la primera compilación (espressif32 8.0.0 / Arduino core 3.3.12)
- `RadioLib.h` emite un `#warning` por usar USB CDC (`ARDUINO_USB_CDC_ON_BOOT=1`) y con `-Werror` era un error: se añadió `-Wno-error=cpp` en `common_settings.ini`.
- El ESP32-S3 no tiene Bluetooth clásico y el core 3.x no define `BluetoothSerial` para él: `bluetooth_utils.h/.cpp` quedan protegidos con `HAS_BT_CLASSIC` (como ya estaba en el resto del código) y se quitó el `#include <BluetoothSerial.h>` suelto de `LoRa_APRS_Tracker.cpp`.
- Arduino core 3.3.x: la libreria `Networking` (cabeceras `Network.h`, `NetworkInterface.h`) la usan WiFi y AsyncTCP, pero el LDF de PlatformIO no les pasa su ruta de inclusion (un simple `-I` fijo tampoco bastó). `tools/fix_network_include.py` (en `extra_scripts` de `platformio.ini`) localiza la carpeta que contiene `Network.h` dentro del framework y la anade a `CPPPATH`; en cores 2.x no hace nada. Tras cambiar de plataforma conviene `pio run -t clean` y borrar `.pio`.
- `TinyGPS++.cpp` (libreria TinyGPSPlus 1.1.0) tiene un `switch` con caida intencionada que el compilador nuevo (GCC 14) marca como `implicit-fallthrough`; con `-Werror` era error. Se anade `-Wno-error=implicit-fallthrough` (sigue como aviso).

### 7.2 Estado de actualizaciones (noviembre 2026)

Versiones más nuevas publicadas que las que fija el proyecto. No se adoptan automáticamente: las de impacto alto se dejan congeladas hasta revisar y probar los cambios de API en placa.

| Librería | En el proyecto | Disponible | Impacto | Nota |
|---|---|---|---|---|
| [RadioLib](https://github.com/jgromes/RadioLib/releases) | 7.8.1 | **8.0.0** | Alto (rompe API) | La 8.0.0 cambia `begin()` / `beginFSK()` / `beginLRFHSS()` a structs de configuración (`ConfigLoRa_t`, `ConfigFSK_t`, `ConfigLRFHSS_t`), pasa `setFrequency` / `setBandwidth` / `setBitRate` / `setFrequencyDeviation` / `setRxBandwidth` a enteros en Hz / bps y `setTCXO()` a un enum de voltaje. **Migración hecha en la rama `radiolib-8`** con una capa `include/radiolib_compat.h` (funciones `RLCompat::*`) que compila igual con 7.8.x y 8.0.x; el firmware solo usa LoRa / FSK / LR-FHSS, no LoRaWAN. Pendiente: Manuel la prueba en la T-Beam Supreme (SX1262) antes de unir a `main`. |
| [AsyncTCP](https://components.espressif.com/components/esp32async/asynctcp/) | **3.6.1** | 3.6.1 | — | Subida el 2026-11-24 (era 3.5.0); mantenimiento de `ESP32Async`, compatible con `ESPAsyncWebServer 3.12.1`. |

El resto de librerías de la tabla del apartado 7 están en su última versión publicada a esta fecha.
