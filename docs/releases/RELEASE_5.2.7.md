# LoRa APRS Tracker V5.2.7 — Notas de versión

Correcciones propuestas por el usuario (revisión de código), aplicadas con un matiz:

1. **GPS siempre activo en modo WSPR** (`gpsSleep()`): el modo eco de un perfil ya no apaga el GPS, del que depende la hora.
2. **UART del GPS vaciada durante la emisión** (`wsprTransmit()`): en los 110 s no se desborda el buffer y TinyGPS sigue actualizando la hora.
3. **Locator**: si no hay fix en vivo, se usa la última posición conocida (`ownPosition()`); con un locator de 4 caracteres (~100 km) es suficiente.
4. **Hora**: se tolera hasta 10 s de antigüedad de la sentencia (se suma a la hora) y se rechaza una fecha anterior a 2024 (arranque en frío). **No** se añade el respaldo del reloj del sistema: tiene resolución de 1 s y puede derivar, así que sin hora GPS no se emite.
5. **`wsprEnd()`**: fuerza la reconfiguración completa del módem tras `transmitDirect()`, igual que `cwRxEnd()`.

Sin compilar con PlatformIO ni probar en la placa.
