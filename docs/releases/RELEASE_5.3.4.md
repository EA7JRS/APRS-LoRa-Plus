# APRS LoRa+ 5.3.4

- **Región IARU automática**: el tracker deduce la región IARU (1, 2 o 3) del GPS y ajusta región, canal y frecuencia de los perfiles LoRa, FSK, FT8 / FT4 / FST4W y CW. Al arrancar, bajo petición (WebUI o SYSTEM) o en marcha, con confirmación en pantalla (*Apply / Keep current*) o directa. Apagada por defecto (WebUI > Transmission mode > *IARU region (auto)*).
- **Control por Bluetooth LE** para la app APRS LoRa+ de Android: estado, sensores, GNSS, DAPNET, NMEA, cambio de modo y perfil, manipulador remoto y actualización de firmware por BLE (`docs/BLE_CONTROL.md`). **Exige emparejamiento con PIN** (WebUI > Bluetooth > *Control PIN*, por defecto 123456: cámbialo). El KISS no cambia.
- **CW por RF, escucha**: S-meter (S9 = -93 dBm), dBm y SNR en la primera línea y **squelch** ajustable (pulsación corta en la escucha, submenú *CW squelch* o WebUI). Ahora la pulsación larga (0,7 s) sale de la escucha.
- **CW, zumbador**: corregido; suena con macros (a mano y con el FH-2), manipulador, entrenador y mando remoto.
- **Instalador web**: corregidos los offsets de las placas ESP32-C3 en el CI.
- **BLE**: el tracker vuelve a anunciar su nombre y admite hasta 4 tramas del móvil en cola.
- Probado en placa en 5.3.3 y antes: FH-2, manipulador, KISS por WiFi, vigilancia de grupo, emergencia remota y FX.25 / IL2P. **Sin probar en placa**: todo lo nuevo de esta versión.
