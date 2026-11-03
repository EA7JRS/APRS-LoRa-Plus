# APRS LoRa+ 5.3.1

- **APRS FSK 9600**: FX.25 (Reed-Solomon, 16/32/64 bytes de control) e IL2P seleccionables por perfil FSK (**Packet protocol (TX)**); la recepción entiende AX.25, FX.25 e IL2P a la vez. Verificado en PC contra Dire Wolf. Ver `docs/FSK_G3RUH.md`.
- **Instalador web USB** que reconoce la placa y la versión instalada, y la actualiza con un clic (Chrome/Edge). Se publica en GitHub Pages con cada release estable.
- **Actualización desde la WebUI** (Administración > Firmware update) en placas con dos copias de firmware (T-Beam S3 Supreme, T-Beam 1W): archivo `.bin` o internet, y actualización automática opcional.
- Las demás placas se actualizan por el instalador USB. Sin probar en placa. Ver el manual, apartado 30.
