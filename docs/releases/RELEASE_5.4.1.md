# APRS LoRa+ 5.4.1

Tracker, app Android, rotor, Display y pasarela NMEA 2000 con el mismo número de versión.

**Tracker**
- **Luces y alertas de emergencia para la app:** eventos BLE `rf` (TX / RX al momento), `led` (mensajes, alarma de emergencia con su tipo, flash) y `eai` (baliza SOS, EAI remota, EAI de voz de la T-TWR, hombre caído); órdenes `alarm` y `eai`.
- **Dial de la app:** el evento `status` lleva la frecuencia y la modulación del modo en curso (`f`, `mod`).
- **Nombres:** Bluetooth y WiFi de configuración **APRSLoRa+tracker** (antes LoRaTracker); salida NMEA **APRSLoRa+tracker-NMEA**.
- **WebUI:** tiempo de internet (claves, descarga en el navegador, `/api/wxnet`), pronóstico con la presión y la humedad de internet, tema Sistema / Automático / Claro / Oscuro / Noche, cabecera con los colores del logo, T-TWR sin los bloques de LoRa y con los planes de su banda.
- **T-TWR Plus:** PMU como LilyGO (USB 2 A, VBUS 3,88 V, corte a 2,6 V), el modo de arranque se queda en su página, FM voice de serie.
- **Motivo del último reinicio** (CRASH / WDT / LOW VOLTAGE, con el modo, los segundos en marcha y, con el vigilante de tareas, la tarea de cada núcleo) en pantalla, registro y evento `info` (`rst`, `rstAt`, `rstTask`).
- **T-TWR Plus, reinicios por el vigilante de tareas (TASK WDT):** la recepción de audio del SA868 cede la CPU aunque no dé abasto.
- **Carga (placas con AXP2101):** el cargador de la batería se enciende siempre al arrancar; si la PMU se apaga sola, «PMU POWER OFF» con el motivo (`info.pmuOff`); estado del cargador en `sensors.chg`.
- **Instalador web:** se despliega desde main tras la release (antes no llegaba solo).

**App Android**
- **Luces TX / RX / MSG / EMG** en la banda A y **ventanas de emergencia** sobre cualquier pantalla: baliza o mensaje APRS, socorro y colisión AIS, EAI propia y hombre caído; notificación a pantalla completa y el móvil como zumbador (tono de alarma, vibración, linterna).
- **Banda A:** modo, modulación y frecuencia reales (CW, DAPNET, WSPR...); FM / NFM de la T-TWR; S-meter de la escucha CW; icono correcto de la última estación oída.
- **Tema** oscuro / claro / del sistema / automático (por el sol) y mejor contraste en las pantallas de herramientas.
- **GPS del móvil** sin cierres; diagnóstico del USB para el RTL-SDR; pronóstico del tiempo de internet; motivo del último reinicio del tracker y de los apagados de la PMU, estado del cargador.
- **Android Auto:** cada alerta de emergencia también como conversación que el coche lee en voz alta (responder por APRS; marcar como leído calla la alarma y, en el hombre caído, equivale a «estoy bien»); el remitente lleva su símbolo APRS y las páginas DAPNET la marca de DAPNET.
- **Radar meteorológico en el mapa:** radar nacional de AEMET como imagen (clave gratuita de AEMET OpenData). La base de señales de SigID (Artemis) pasa a ser opcional: se descarga solo si el usuario lo pide.

**Rotor**
- Paleta de la app de serie en la pantalla (la roja con `lcd.theme` 1), S-meter igual que el de la app y cabecera de la WebUI con los colores del logo.

**Display**
- Pronóstico y tendencia de 3 h del tiempo de internet en Sensors.

**NMEA 2000**
- Mismo número de versión que el resto (antes 5.3.5).

**Todos:** «APRS LoRa+ · © 2026 Manuel - EA7JRS» en Acerca de. **Sin probar en placa** lo nuevo de esta versión. Lista completa en `CHANGELOG.md`.
