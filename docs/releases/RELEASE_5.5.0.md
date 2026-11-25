# APRS LoRa+ tracker 5.5.0

Versión unificada de todo el ecosistema APRS LoRa+ (tracker, app Android, rotor, Display, plotter, pasarela NMEA 2000
y OBD-II) en la 5.5.0.

## Radio

- **RadioLib 8.0.0.** Control de la radio migrado a la API nueva de RadioLib (estructuras de configuración, frecuencias
  y velocidades en Hz / bps, voltaje del TCXO por enumerado). Capa de compatibilidad propia (`radiolib_compat.h`) que
  despacha según el chip (SX126x con TCXO, SX127x sin él), probada en el PC contra la 7.x y la 8.x. APRS no cambia.

## Modos nuevos

- **SSDV** (slow-scan digital video). Emite y recibe imágenes JPEG en paquetes SSDV (UKHAS) por LoRa (237 B útiles) o
  FSK con Reed-Solomon (205 B), con el `SSDVClient` de RadioLib. Origen de la imagen: patrón de prueba, URL por WiFi,
  **subida por BLE desde la app** o cámara OV2640. Codificador JPEG propio (`jo_jpeg`) para el patrón y la cámara.
  Ajustes en la WebUI (grupo `ssdv`); control por BLE con `ssdv` / `ssdvtx` / `ssdvimg`.
- **RTTY** (baliza de radioteletipo). Sentencia UKHAS `$$INDICATIVO,contador,hora,lat,lon,alt,sats*CRC` con el GPS o
  texto libre; Baudot ITA2 o ASCII, velocidad, desplazamiento y bits de parada configurables (grupo `rtty`).
- **POCSAG** (emisión). Envía un aviso POCSAG a un RIC con el `PagerClient` de RadioLib (512 / 1200 / 2400 bps,
  numérico o alfanumérico); desde la app con la orden BLE `pocsag` (grupo `pocsag`).

En las placas de radio FM (SA868 / T-TWR) y en el transponder MAPRS (muy justo de DRAM) estos modos no se incluyen.

## WebUI por la red local

- **`wifiAP.webMode`.** Además del punto de acceso de configuración, el equipo puede unirse a la red local y servir la
  WebUI, reutilizando la WiFi del enlace KISS / OTA; a demanda (modo de configuración) o siempre; mDNS
  `aprs-lora-plus.local`. Antes solo se llegaba a la configuración por el punto de acceso propio, lo que costaba cuando
  el equipo ya estaba en marcha.

## CW / Morse

- **Decodificador adaptativo** (umbral punto/raya según el ritmo real del operador, no fijo).
- **Macros** `{RST}` `{CNT}` `{GRID}` y número de serie de concurso que se incrementa solo.
- **Trainer Koch adaptativo** que pondera los caracteres fallados y muestra el progreso.

## Mantenimiento

- **AsyncTCP 3.6.1** (compatible con ESPAsyncWebServer 3.12.1).
- Capturas de las páginas en el README y simulador para rehacerlas (`tools/pages_sim`).
- Registro sin el «gpio_pullup_en: GPIO number error» del botón en las T-Beam de GPIO solo de entrada.

Compila en la CI para todas las placas. **La migración a RadioLib 8 y los modos nuevos están sin probar todavía en la
T-Beam Supreme.**
