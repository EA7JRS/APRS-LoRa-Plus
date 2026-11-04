# APRS LoRa+ 5.3.2

- **KISS por WiFi** (TCP 8001 y UDP opcional): usa la radio LoRa de la placa desde APRSDroid, Xastir, YAAC… a través de tu WiFi de casa o del punto de acceso del móvil. Interruptor en la WebUI (Connectivity > KISS over WiFi), apagado por defecto, con lista de direcciones permitidas. Solo APRS LoRa y placas ESP32-S3 / C3. Ver el manual, apartado 32.
- **Página GROUP al estilo Group Monitor de Yaesu**: cursor con rumbo y distancia, GO TO a la brújula, vista Radar tipo APL con zoom, mensaje rápido a un miembro o a todos los que están IN, y ping.
- **Vigilancia de grupo (ARTS)** y **emergencia remota (EAI)** por mensaje APRS, en APRS LoRa y FSK.
- **Baliza LR-FHSS** con locator de 4, 6 u 8 caracteres (6 por defecto).
- **Teclado Yaesu FH-2** en un pin ADC para enviar los mensajes CW 1–5, con calibración en la WebUI.
- Aviso de actualización de firmware en ventana emergente (ya no desborda la pantalla OLED).
- Sin probar en placa. Con la actualización automática activada, las placas con dos copias de firmware detectan esta versión al arrancar.
