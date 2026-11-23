# APRS LoRa+ 5.4.8

Versión del **tracker**. La app Android lleva su propia numeración; el resto del ecosistema sigue en la 5.4.7.

**Tracker**
- **Botón con la pantalla apagada** (modo ECO o ahorro de batería): la pantalla se enciende en cuanto se aprieta el botón y
  se redibuja al momento; cada pulsación reinicia la cuenta del apagado. Antes la pantalla se apagaba a los 4 s de
  encenderla aunque se pasaran páginas, y la siguiente pulsación solo la volvía a encender.
- **CLOCK > EME** (vista nueva): rebote lunar en la frecuencia en uso (pérdida del trayecto, retardo del eco, Doppler) y,
  del Sol, distancia, irradiancia e índice UV de cielo despejado.
- **WebUI > Config check:** errores, avisos y correcciones seguras antes de guardar (NOCALL, ruta, plan de la IARU,
  contraseña del WiFi AP y PIN de fábrica, enlaces KISS).
- **`/api/status`:** estado del equipo en JSON, también con el KISS por WiFi activo (puerto 80), para S.A.T.-APRS+
  (Radio · APRS LoRa+, v1.55.0).
- **SmartBeacon «APRS standard»** (ajuste 6 de cada baliza, en la WebUI): baliza lenta cada 30 min por debajo de 5 km/h,
  rápida cada 90 s desde 100 km/h, giro de 30° + 240 / velocidad y 15 s como mínimo entre balizas. Mucho menos canal que
  los ajustes de corredor, bici y coche.
- **Remote de la app:** las teclas ◀ ▶ y *Back* hacen de botón del equipo (recorren las vistas de cada página y saltan las
  páginas que el modo no tiene), y las opciones de GNSS, zona horaria, potencia, QNH y declinación salen una a una.
- **TELEMETRY > HF y SPACE WX con el Hp30 de GFZ** (cada media hora, sin tope en 9) cuando la app lo manda y tiene menos
  de 2 h; si no, el Kp como antes.
- Fórmulas comunes nuevas del plotter (`ephem`): rebote lunar, distancia e irradiancia del Sol, horizonte de radio.

**Sin probar en las placas**: compila en la CI para todas; las fórmulas pasan sus pruebas en el PC.

**Binarios rehechos el 23-11-2026** con SmartBeacon «APRS standard», Remote y Hp30, y sin la vista CLOCK > ALMANAC (atascaba el paso de página tras MOON): quien
instaló la 5.4.8 antes de esa fecha puede volver a instalarla para tenerlos.
