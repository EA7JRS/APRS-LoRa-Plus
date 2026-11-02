# LoRa APRS Tracker V5.2.6 — Notas de versión

- **WSPR:** `msUntilSlot()` calcula el índice de ranura sobre el día UTC (se reinicia a las 00:00). Para todos los intervalos válidos (2, 4, 6, 8, 10, 12, 20, 30 y 60 min, que dividen las 720 ranuras del día) el resultado es idéntico al anterior; el cambio solo importaría con intervalos que no dividan 720, que `wsprInterval()` ya ajusta al valor válido más cercano.
- Probado en PC: todas las ranuras y desfases de los intervalos válidos, cada 37 s a lo largo de un día, sin errores.
