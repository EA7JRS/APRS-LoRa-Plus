# LoRa APRS Tracker V5.2.5 — Notas de versión

- **WSPR:** en `wsprTransmit()` se añade `delay(1)` en cada sondeo (cada 20 ms) durante los 110 s de emisión, para que la tarea IDLE alimente el watchdog (`yield()` no cede a tareas de menor prioridad). Cambio propuesto y comprobado por el usuario en la T-Beam Supreme. Coste: hasta ~1 ms de retraso puntual en el cambio de símbolo y en un paso del dither, que se recupera (el símbolo dura 683 ms).
- **Supreme:** `BOARD_HAS_PSRAM` protegido con `#ifndef` (el entorno ya lo define con `-D`). El usuario confirma PSRAM detectada.
- Sin verificar en el aire: la emisión WSPR completa y su decodificación.
