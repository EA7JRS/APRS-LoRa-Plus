# APRS LoRa+ 5.3.6

- **Nueva placa: LilyGO T-TWR Plus** (Rev2.0 y Rev2.1, radio FM SA868 en VHF o UHF): APRS AFSK 1200, **M17** (paquetes APRS y SMS), **FM de voz** (VFO, 32 memorias, tonos, escaneo, ARTS, EAI), DAPNET, CW A1A / MCW, calibración y GPS L76K. Ver `docs/T_TWR.md`.
- **Límites de emisión de la T-TWR**: recepción 136–174 / 400–480 MHz; emisión solo en las bandas de aficionado de la región IARU según el GPS (Europa 144–146 y 430–440 MHz, América 144–148 y 430–450 MHz), fijo en el firmware. Desbloqueo solo con un código firmado para cada equipo.
- **Satélites por grupos y favoritos** (`sats_all.txt`: todas las bandas, grupos de CelesTrak, CTCSS de los repetidores); en la T-TWR el VFO sigue al satélite con Doppler y tono, y APRS por satélite (ISS 145,825 MHz).
- **Receptor AIS** (modo 12): SA868 de la T-TWR, módulo externo por UART en cualquier placa o el RTL-SDR de la app; lista, detalle, GO TO, alarmas de colisión y de socorro, reenvío NMEA. Ver `docs/AIS.md`.
- **Tiempo de internet** (la app con OpenWeatherMap / IQAir): página TELEMETRY, **QNH del altímetro** sin GPS, datos para APRS LoRa+ Display y **parada por viento** de la caja del rotor. Nunca en las balizas.
- **Radio comercial por I2C** (RDA5807M, Si4703, Si4735) con RDS en cualquier placa con pantalla.
- **IL2P a 9600 compatible con Dire Wolf y la NinoTNC.**
- **Cielo GNSS** para la app (evento `sky`).
- **WebUI e instalador web con el icono de la app**; pantallas de arranque de Display y Rotor con la bienvenida de la app.
- **Sin probar en placa**: todo lo nuevo de esta versión. Lista completa en `CHANGELOG.md`.
