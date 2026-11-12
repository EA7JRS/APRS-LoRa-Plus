# APRS LoRa+ 5.4.2

Tracker, app Android, rotor, Display, plotter náutico (con el instrumento de viento) y pasarela NMEA 2000 con el mismo
número de versión. El gateway sigue en espera.

**Tracker**
- **Firmware oficial o modificado:** el evento `info` lleva la huella del firmware (`elf`) y `ota/<placa>.json` la del
  publicado (`elfSha`); la app muestra OFFICIAL o CUSTOM FIRMWARE (solo informativo).
- **Instalador web, grupo Náutica:** APRS LoRa+ Plotter (Waveshare ESP32-S3-Touch-LCD-7B) y el instrumento de viento
  APRS LoRa+ Wind (Waveshare ESP32-S3-Touch-LCD-4); al detectar `device=wind` pasa al grupo Náutica.
- **Donativos:** botón PayPal.me (paypal.me/ea7jrs) en *About* de la WebUI y en el instalador web.
- T-Deck: arreglo del rastro del vigilante de tareas con ESP-IDF 4.4.

**App Android** (versionCode 50402)
- Meteorología APRS (posiciones con tiempo, informes sin posición), Mic-E con su estado, objetos e ítems, tramas de
  terceros; pantalla **WX** con las estaciones meteorológicas.
- OFFICIAL / CUSTOM FIRMWARE en Firmware y Tracker.
- Tracker: motivo del apagado de la PMU, estado del cargador, «Last reset» con modo, tiempo y tareas del vigilante.
- Google Play: Android 16 (API 36), paquete .aab firmado en la CI, sin RainViewer y con la base de SigID opcional.

**Rotor**
- **Red local:** con la WiFi de la caja, servidor TCP (NMEA 0183, gpsd o Signal K), NMEA por UDP (el instrumento de
  viento) y OpenWeatherMap. La protección por viento ya no necesita la app; la posición de la red va a la baliza APRS.
- **Rotor en un barco o un coche** (`lan.heading`): acimut verdadero con el rumbo del barco, y vuelve a apuntar al girar.
- **Hora:** GPS de la red y NTP `ntp.roa.es` (hora oficial de España, ROA). En la Raspberry, NTPsec con NTS
  (`nts1.roa.es`) y el GPS / PPS de gpsd.

**Display**
- **Pestaña Network:** WiFi, servidor TCP (NMEA 0183, gpsd o Signal K), UDP y OpenWeatherMap; datos del barco con su
  origen y blancos AIS con distancia y CPA, en las cuatro placas.
- **Hora:** GNSS del tracker, GPS de la red y NTP `ntp.roa.es`. Settings > Link > *Clock*.

**Plotter náutico** (versión preliminar, sin probar en la placa)
- **Instrumento de viento APRS LoRa+ Wind** (nuevo, Waveshare ESP32-S3-Touch-LCD-4): veleta NASA Target 2 Wind,
  páginas Rosa, Viento, Números e Historial, modo noche, alarmas; viento al plotter por Bluetooth, NMEA 2000 y WiFi.
- **Piloto Raymarine ST2000+:** RMB y APB con waypoints de 4 letras, VWR para WindTrim y VHW; salida por Bluetooth.
- **Hora** del GPS y de `ntp.roa.es`; bienvenida y *Acerca de* con QR de donativo.

**NMEA 2000** (pasarela XIAO)
- Solo el número de versión.

Nada de esta versión se ha probado todavía en las placas nuevas (instrumento de viento) ni con redes reales.
