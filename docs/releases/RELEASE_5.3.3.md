# APRS LoRa+ 5.3.3

- **CAT / Hamlib por WiFi**: servidor *NET rigctl* en el puerto 4532 (`rigctl -m 2 -r aprs-lora-plus.local:4532 f`). Frecuencia, modo (APRS LoRa / APRS FSK), RSSI y potencia; PTT opcional = enviar una baliza APRS; ventana de sintonía configurable. Interruptor en la WebUI y en SYSTEM. Especificación pública (`docs/CAT_HAMLIB.md`) y backend nativo para Hamlib 4.7.2 (`tools/hamlib_backend`). No es una radio de audio. Manual, apartado 33.
- **T-Beam S3 Supreme, hardware externo**: esquema y pines para LED verde, LED rojo, flash de alta intensidad, buzzer KY-006, jacks del manipulador y del FH-2 y salida NMEA al conversor TTL-RS422 (`docs/HARDWARE_SUPREME_EXTRAS.md`). La WebUI identifica cada pin de la cabecera, avisa de repetidos y rellena los sugeridos. Pines por defecto en la Supreme: 38 / 21 / 45 / 39 / 48 (los anteriores chocaban con la radio, el RTC y el IMU). Nuevo en la WebUI: *NMEA output* (pin TX y velocidad).
- **SYSTEM**: se ve el estado del WiFi y hay una vista **LINK** con los detalles de Bluetooth y WiFi (IP, puertos, clientes); submenú con Bluetooth, formato de datos BT y WiFi KISS / CAT.
- **KISS por WiFi** probado con APRSdroid en LoRa y FSK; el nombre de equipo `aprs-lora-plus` se aplica antes de crear la interfaz WiFi.
- **Menú de 3 clics** y pantalla del punto de acceso WiFi con formato de páginas y ventanas pequeñas.
- Sin probar en placa (CAT, pines nuevos, NMEA, vista LINK). El CAT está verificado en PC contra `rigctl` 4.6.5 y 4.7.2.
