# APRS LoRa+

[![release](https://img.shields.io/github/v/release/EA7JRS/APRS-LoRa-Plus?include_prereleases&color=red)](https://github.com/EA7JRS/APRS-LoRa-Plus/releases) [![build](https://github.com/EA7JRS/APRS-LoRa-Plus/actions/workflows/commit.yml/badge.svg)](https://github.com/EA7JRS/APRS-LoRa-Plus/actions/workflows/commit.yml) ![license](https://img.shields.io/badge/license-GPL--3.0-blue) ![platform](https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--S3%20%7C%20ESP32--C3-lightgrey)

**APRS LoRa+** es una ampliación del firmware [CA2RXU LoRa APRS Tracker/Station](https://github.com/richonguzman/LoRa_APRS_Tracker) de Ricardo Guzman (CA2RXU) para placas ESP32 con módulo LoRa y GPS. Desarrollado por **Manuel EA7JRS** ([@EA7JRS](https://github.com/EA7JRS)).

Mantiene todo lo de la base (APRS LoRa, GPS, mensajes, telemetría, Bluetooth/KISS, Winlink, WebUI…) y añade nuevos modos de radio, una interfaz de pantallas con un solo botón y herramientas de calibración.

> **English:** APRS LoRa+ extends the CA2RXU LoRa APRS Tracker/Station firmware (ESP32 + LoRa + GPS) with new radio modes (APRS FSK 9600 G3RUH, CW/Morse, WSPR beacon, DAPNET pager receiver), an 11-page one-button UI, quick-reply messaging, global frequency (ppm) calibration and a reorganised Web UI. Documentation is mostly in Spanish; the user manual and quick guide are also available in English (see below). GPL-3.0.

## Novedades respecto a la base

| Área | Qué aporta |
|---|---|
| **Modos de funcionamiento** | Selector al arrancar: *APRS LoRa*, *APRS FSK 9600* (G3RUH, AX.25), *MORSE / CW*, *WSPR beacon*, *FT8 beacon*, *FT4 beacon*, *FST4W beacon* y *DAPNET pager* (receptor POCSAG, solo recepción). *LR-FHSS* (experimental) se activa desde la WebUI; viene apagado. |
| **Interfaz** | 11 pantallas APRS navegables con un botón (principal, última estación, mensajes, GPS, telemetría, brújula, altímetro, reloj, SOS…), pantalla de bienvenida APRS LoRa+, alarmas y baliza SOS. |
| **Mensajería** | Lectura de mensajes recibidos y respuesta con mensajes rápidos predefinidos, sin teclado. |
| **Calibración** | Corrección global del oscilador en ppm (tres decimales) para TX y RX, portadora de prueba CW para medir con un SDR y calibración WSPR por offset y por potencia. |
| **FSK** | Región/canal/frecuencia/potencia propios y filtro gaussiano GFSK/GMSK por perfil. |
| **GPS** | Constelaciones, SBAS, AssistNow y PPS; u-blox M10 y Quectel L76K (este último con 1–5 Hz). |
| **WebUI** | Reorganizada por secciones, con un grupo de ajustes por modo y cabecera «APRS LoRa+ / Tracker versión». |
| **KISS por WiFi** *(5.3.2, probado en placa)* | TNC KISS por TCP (8001) y UDP opcional para APRSDroid, Xastir o YAAC a través de tu WiFi o del punto de acceso del móvil. Interruptor en la WebUI. Solo APRS LoRa y APRS FSK en ESP32-S3 / C3. Ver el manual, apartado 32. |
| **CAT por WiFi y USB (Hamlib)** *(5.3.3, sin probar)* | Servidor NET rigctl (puerto 4532, `rigctl -m 2`) y el mismo protocolo por el puerto USB: frecuencia, modo, RSSI y potencia; PTT opcional = baliza APRS. Especificación pública y backend de Hamlib 4.7.2 en `docs/CAT_HAMLIB.md` y `tools/hamlib_backend`. Manual, apartado 33. |
| **Hardware externo en la T-Beam S3 Supreme** *(5.3.3, sin probar)* | Esquema y pines para LED rojo (Tx) y verde (Rx), flash de 1 W, buzzer KY-006, jacks del manipulador y del FH-2 y salida NMEA; la WebUI identifica cada pin y rellena los sugeridos. Ver `docs/HARDWARE_SUPREME_EXTRAS.md`. |
| **Modo SAT y rotor** *(5.3.5, sin probar)* | Satélites en 70 cm (y LoRa / TinyGS, Inmarsat): azimut, elevación, Doppler, recepción y emisión con precompensación. El rotor (rotctld, GS-232, BLE) sigue un satélite, la última estación APRS oída o la última baliza CW con locator. Ver `docs/SAT_MODE.md`. |
| **LilyGO T-TWR Plus** *(5.3.6, sin probar)* | Radio FM SA868: APRS AFSK 1200, M17, FM de voz con memorias y tonos, DAPNET, CW, satélites con el VFO siguiendo el Doppler, receptor AIS; emisión limitada a las bandas de aficionado de la región IARU (GPS). Ver `docs/T_TWR.md`. |
| **Satélites por grupos, AIS y tiempo de internet** *(5.3.6, sin probar)* | Archivo de satélites con todas las bandas, grupos de CelesTrak y favoritos; receptor AIS (T-TWR, módulo UART o RTL-SDR de la app) con alarma de colisión; tiempo y calidad del aire de internet desde la app para TELEMETRY y el QNH del altímetro; radio comercial por I2C con RDS. |
| **Menos flash** *(5.3.5, sin probar en placa)* | Configuración por tabla y WebUI más ligera: la T-Beam Supreme baja del 91,1 % al 85,2 % de su partición. La actualización por internet pasa a la app Android. |
| **Región IARU automática** *(5.3.4, sin probar)* | Deduce la región IARU 1/2/3 del GPS y ajusta canal y frecuencia de los perfiles (LoRa, FSK, FT8/FT4/FST4W, CW), al arrancar, bajo petición o en marcha, con confirmación en pantalla o directa. Manual, apartado 20. |
| **Control por BLE con PIN** *(5.3.4, sin probar)* | Segundo servicio BLE para la app de Android (estado, sensores, GNSS, DAPNET, NMEA, modo y perfil, manipulador remoto, firmware por BLE) con emparejamiento por PIN. Ver `docs/BLE_CONTROL.md`. |
| **CW por RF: S-meter y squelch** *(5.3.4, sin probar)* | La escucha CW muestra S-meter, dBm y SNR y tiene squelch ajustable desde el tracker y la WebUI; el zumbador suena con macros, manipulador y entrenador. |
| **Instalar y actualizar** *(5.3.1, instalador web probado)* | Instalador web por USB que reconoce la placa y la versión instalada, actualización desde la WebUI (archivo) y desde la app Android (internet del móvil, por Bluetooth) en placas con dos copias de firmware. Ver el manual, apartado 30. |
| **Identificación de equipos** | Lista oficial aprs-deviceid (tocalls y sufijos Mic-E). |

Detalle completo en [`CHANGELOG.md`](CHANGELOG.md) y en las notas de cada versión ([`docs/releases/`](docs/releases)).

## Documentación

| Documento | Contenido |
|---|---|
| [`docs/GUIA_RAPIDA.md`](docs/GUIA_RAPIDA.md) / [`docs/QUICK_GUIDE_EN.md`](docs/QUICK_GUIDE_EN.md) | Guía rápida (ES / EN) |
| [`docs/MANUAL_USUARIO.md`](docs/MANUAL_USUARIO.md) / [`docs/USER_MANUAL_EN.md`](docs/USER_MANUAL_EN.md) | Manual de usuario (ES / EN), también en PDF |
| [`docs/MODOS.md`](docs/MODOS.md) | Modos de funcionamiento y selector de arranque |
| [`docs/CW_MORSE.md`](docs/CW_MORSE.md), [`docs/FSK_G3RUH.md`](docs/FSK_G3RUH.md), [`docs/WSPR.md`](docs/WSPR.md), [`docs/WSPR_CALIBRACION.md`](docs/WSPR_CALIBRACION.md) | Modos CW, FSK y WSPR; calibración de frecuencia |
| [`docs/LRFHSS_BALIZA.md`](docs/LRFHSS_BALIZA.md) | Baliza LR-FHSS (desactivada) |
| [`docs/APRS_PANTALLAS_TRACKER.md`](docs/APRS_PANTALLAS_TRACKER.md) | Notas técnicas de las pantallas y estado de pruebas |
| [`docs/BASE_README_CA2RXU.md`](docs/BASE_README_CA2RXU.md) | README original de la base (por atribución) |

## Placas

El proyecto conserva todas las placas de la base (T-Beam v0.7 / v1.x / S3 Supreme / 1W, Heltec V2 / V3 / Wireless Tracker / Wireless Stick Lite V3, TTGO LoRa32, T-Deck, T3S3, LightTracker Plus, MeshCom, DIY con SX127x / E22 / E220, ESP32-C3, etc.). La lista completa está en [`platformio.ini`](platformio.ini) y en la carpeta [`variants/`](variants).

No todos los modos están disponibles en todos los chips:

| Modo | SX1262 / SX1268 | SX1276 / SX1278 | LLCC68 |
|---|:-:|:-:|:-:|
| APRS LoRa | ✔ | ✔ | ✔ |
| APRS FSK 9600 | ✔ | ✔ | ✘ |
| CW / WSPR / FT8 / FT4 / FST4W / DAPNET | ✔ | ✘ | ✘ |

**LilyGO T-TWR Plus** (`lilygo_t_twr_plus`, Rev2.0 y Rev2.1, *sin probar en placa*): no lleva LoRa sino un transceptor FM SA868; hace **APRS analógico AFSK 1200** en VHF (144,800 MHz) con el módem en el ESP32-S3. Ver [`docs/T_TWR.md`](docs/T_TWR.md).

Las placas sin botón (ESP32-C3 SuperMini DIY, Heltec HT-CT62) no tienen selector de arranque y las placas TNC sin pantalla no muestran páginas. Ver el capítulo «Compatibilidad por placa» del manual.

## Compilar

Requiere [PlatformIO](https://platformio.org/) (Arduino-ESP32 3.x, plataforma `espressif32 @ 8.0.0`).

```bash
git clone https://github.com/EA7JRS/APRS-LoRa-Plus.git && cd APRS-LoRa-Plus
pio run -e ttgo_t_beam_s3_SUPREME_v3            # firmware (cambia el entorno por tu placa)
pio run -e ttgo_t_beam_s3_SUPREME_v3 -t upload  # grabar firmware
pio run -e ttgo_t_beam_s3_SUPREME_v3 -t uploadfs   # grabar el sistema de archivos (configuración)
```

La WebUI (`data_embed/`) se comprime automáticamente en cada compilación (`tools/compress.py`), por lo que los `.gz` no se guardan en el repositorio. Para ver los entornos disponibles: `pio project config | grep "^\[env:"`.

El código se compila con `-Werror -Wall`. Los avisos `#warning`, de `switch` con caída intencionada y de truncado de texto en `snprintf` (muy habituales al recortar texto para una pantalla de 21 caracteres) se muestran pero no detienen la compilación; cualquier otro aviso sí.

### Pruebas en PC

La carpeta [`tools/`](tools) incluye pruebas que se ejecutan en el ordenador sin placa (decodificador POCSAG, tonos WSPR, LR-FHSS, campanas, CASIC). Cada archivo indica en su cabecera cómo compilarlo. Por ejemplo:

```bash
g++ -std=c++17 -Wall -Iinclude tools/test_pocsag.cpp -o /tmp/test_pocsag && /tmp/test_pocsag
```

## Estado de las pruebas

El proyecto está en desarrollo activo. Se compila y se prueba en las placas del autor (por ejemplo T-Beam S3 Supreme); **muchas funciones nuevas no se han verificado en todas las placas** y algunas (por ejemplo DAPNET en el aire) están pendientes de confirmación en campo. Los documentos indican qué está probado y qué no. Si lo pruebas en otra placa, abre un [issue](https://github.com/EA7JRS/APRS-LoRa-Plus/issues/new/choose) con el entorno de PlatformIO, el modo y el resultado.

## Créditos y licencia

APRS LoRa+ (Manuel EA7JRS) se basa en **CA2RXU LoRa APRS Tracker** (Ricardo Guzman, CA2RXU), que a su vez se basa en el trabajo de OE5BPA, DL9SAU, DC2MH y ON4AA, además de numerosos colaboradores. Ver [`CREDITS.md`](CREDITS.md).

Licencia **GNU GPL v3** ([`LICENSE`](LICENSE)). Los archivos modificados de la base conservan su aviso de copyright original; el código nuevo lleva «Copyright (C) 2026 Manuel - EA7JRS».

73 de EA7JRS.
