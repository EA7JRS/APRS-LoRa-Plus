# LoRa APRS Tracker V5.2.8 — Notas de versión

Revisión externa de `cw_morse.cpp`, aplicada en parte:

- **Aplicado:** `Decoder::keyUp()` ignora pulsos de menos de 12 ms (rebote de contactos del manipulador o pico de ruido en RF; un punto a 40 WPM dura 30 ms). Ya no crea puntos fantasma ni mueve el estimador de velocidad.
- **Aplicado con cambio:** el estimador adaptativo no se actualiza con pulsaciones de más de 6 unidades (portadora larga, manipulador atascado). No se añade el límite inferior (unit/3) de la propuesta: con la velocidad inicial muy por debajo de la real (5 WPM y un operador a 20 WPM) bloquearía la adaptación para siempre.
- **No aplicado:** el cambio de caída del pico en `Envelope` (0,0003 → 0,0002 / 0,0008). La propuesta no corrige el problema que describe (arrancar con la portadora ya presente) y acelerar la caída podría hacer perder señales débiles con QSB. Sin datos de campo que lo justifiquen, se mantiene.
- **Keyer:** sin cambios; la lógica de paletas Iambic A/B era correcta.

Probado en PC: "TEST" a 15 WPM decodificado con y sin rebotes; convergencia de 5 a 19 WPM con puntos a 20 WPM. Sin compilar con PlatformIO.
