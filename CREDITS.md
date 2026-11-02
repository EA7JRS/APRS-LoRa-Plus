# Créditos / Credits

**APRS LoRa+** — Manuel EA7JRS, © 2026 Manuel - EA7JRS (código nuevo y ampliaciones).

## Cadena de autoría

```
APRS LoRa+ (Manuel EA7JRS)
  └─ CA2RXU LoRa APRS Tracker/Station — Ricardo Guzman, CA2RXU
       ├─ OE5BPA  Peter — LoRa APRS Tracker original (https://github.com/lora-aprs/LoRa_APRS_Tracker)
       ├─ DL9SAU  Thomas — librería KISS <-> TNC2 (https://github.com/dl9sau/TTGO-T-Beam-LoRa-APRS)
       ├─ DC2MH   Manfred (Mane76) — varios indicativos y velocidad del procesador
       └─ ON4AA   Serge — codificación base91 (https://github.com/aprs434/lora.tracker)
```

Colaboraciones de placas y funciones citadas por la base: S54B (Tilen), OE1JLN (Julian), OE5HWN (Helmut, MeshCom), LA1HSA (Asbjørn), OE2JPO (Johannes, T-Beam S3 Supreme), F4HVV (Valentin), Béla Török, Mathias «mpbraendli», entre otros. El historial completo está en [`docs/BASE_README_CA2RXU.md`](docs/BASE_README_CA2RXU.md).

## Datos y componentes de terceros

- **Lista de equipos APRS**: [aprs-deviceid](https://aprsorg.github.io/aprs-deviceid-web/) (`tools/tocalls.yaml`).
- **Modelo magnético**: coeficientes WMM de NOAA / BGS (`tools/wmm_cof_to_header.py`).
- **Bibliotecas** (descargadas por PlatformIO, cada una con su licencia): RadioLib, TinyGPSPlus, ArduinoJson, NimBLE-Arduino, ESPAsyncWebServer/AsyncTCP, Adafruit (BusIO, Unified Sensor, BME280/BMP280/BME680), Time y APRSPacketLib.
- **WebUI**: Bootstrap (licencia MIT).
- `src/utils.cpp` incluye código de Mateusz Salwach (licencia MIT).

## Licencia

GNU GPL v3 (ver `LICENSE`). Quien redistribuya o modifique este proyecto debe conservar los avisos de copyright de los autores anteriores y publicar los cambios bajo la misma licencia.
