# APRS LoRa+ 5.4.8

Versión del **tracker**. La app Android lleva su propia numeración; el resto del ecosistema sigue en la 5.4.7.

**Tracker**
- **Botón con la pantalla apagada** (modo ECO o ahorro de batería): la pantalla se enciende en cuanto se aprieta el botón y
  se redibuja al momento; cada pulsación reinicia la cuenta del apagado. Antes la pantalla se apagaba a los 4 s de
  encenderla aunque se pasaran páginas, y la siguiente pulsación solo la volvía a encender.
- **CLOCK > ALMANAC** (vista nueva): próxima fase de la luna, perigeo o apogeo, marea de equilibrio, próximos eclipses de
  Luna y de Sol y próximo equinoccio o solsticio.
- **CLOCK > EME** (vista nueva): rebote lunar en la frecuencia en uso (pérdida del trayecto, retardo del eco, Doppler) y,
  del Sol, distancia, irradiancia e índice UV de cielo despejado.
- **WebUI > Config check:** errores, avisos y correcciones seguras antes de guardar (NOCALL, ruta, plan de la IARU,
  contraseña del WiFi AP y PIN de fábrica, enlaces KISS).
- **`/api/status`:** estado del equipo en JSON, también con el KISS por WiFi activo (puerto 80), para S.A.T.-APRS+
  (Radio · APRS LoRa+, v1.55.0).
- Fórmulas comunes nuevas del plotter (`ephem`): fases verdaderas, eclipses, estaciones, marea de equilibrio, rebote
  lunar, horizonte de radio.

**Sin probar en las placas**: compila en la CI para todas; las fórmulas pasan sus pruebas en el PC.
