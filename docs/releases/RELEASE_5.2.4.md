# LoRa APRS Tracker V5.2.4 — Notas de versión

- **T-Beam Supreme:** el entorno `ttgo_t_beam_s3_SUPREME_v3` no activaba la PSRAM, por eso SYSTEM mostraba `n/a`. Se añaden `board_build.arduino.memory_type = qio_qspi` y `-D BOARD_HAS_PSRAM` (8 MB QSPI).
- El firmware no usa todavía la PSRAM para nada; solo se detecta y se muestra su uso.
- Sin compilar con PlatformIO ni probar en la placa. Si tras compilar sigue en `n/a` o la placa no arranca, habría que probar `qio_opi` o quitar el flag.
