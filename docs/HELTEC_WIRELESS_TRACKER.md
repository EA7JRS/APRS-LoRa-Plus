# Heltec Wireless Tracker en APRS LoRa+

Placa `heltec_wireless_tracker` (instalador web y `heltec_wireless_tracker.zip` de cada release). Fuentes: [especificaciones](https://heltec.org/project/wireless-tracker/#Specifications), [hoja de datos V1.1](https://resource.heltec.cn/download/Wireless_Tracker/Wireless%20Tracker1.1.pdf), [esquema HTIT-Tracker V0.5](https://resource.heltec.cn/download/Wireless_Tracker/Wireless_Tacker1.1/HTIT-Tracker_V0.5.pdf) y [wiki](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wireless-tracker/).

## Especificaciones y cómo las usa el firmware

| Parte | Fabricante | En el firmware |
|---|---|---|
| MCU | ESP32-S3FN8 (8 MB de flash, sin PSRAM) | `board = esp32-s3-devkitc-1` (8 MB, sin PSRAM) |
| LoRa | SX1262 con TCXO, 21 ± 1 dBm, −134 dBm | `HAS_SX1262`, `HAS_TCXO`; SCK 9, MISO 11, MOSI 10, CS 8, RST 12, DIO1 14, BUSY 13 |
| Bandas LoRa | dos modelos: **LF** (EU433 433,175–434,665 MHz y CN470 470–510 MHz) y **HF** (863–928 MHz: EU868, IN865, US915, AU915, KR920, AS923) | el LF es el de **70 cm**: sirve para el APRS LoRa de 433,775 MHz y los demás perfiles de serie de 433–434 MHz; el HF, para 868 / 915 MHz |
| GNSS | UC6580 (Unicore), 96 canales, hasta 10 Hz, GPS L1+L5, Galileo E1+E5a, BeiDou B2a, GLONASS G1, QZSS, SBAS, NavIC L5 (con firmware específico); 1,5 m RMS | UART 115200: RX del ESP32 = 33, TX = 34; RESET 35, PPS 36. Frases `$GN…`; el gráfico del cielo (`sky`) reconoce también NavIC (`GI`, sistema 6 de GSA) |
| Pantalla | TFT 0,96″ 160 × 80 (ST7735) | TFT_eSPI: MOSI 42, SCLK 41, CS 38, DC 40, RST 39, luz 21 |
| Alimentación de GNSS y pantalla | `Vext` (GPIO 3 a nivel alto) | `VEXT_CTRL 3`: al apagar el GPS (ahorro) también se apaga la pantalla |
| Batería | Li-ion 3,7 V con carga (SH1.25-2) | divisor 390 k + 100 k en GPIO 1 (VBAT = lectura × 4,9, como la hoja de datos); `ADC_CTRL 2` a nivel alto para medir (V1.1) |
| Botones | USER (GPIO 0) y RST | `BUTTON_PIN 0` (también despierta del reposo) |
| LED | LED de usuario en GPIO 18 | LED de mensajes por defecto (*Notifications*, apagado hasta que lo actives). Los demás avisos, por defecto en pines libres de J3: LED de emisión 17, linterna 16, zumbador 4 (tono) y 5 (alimentación) |
| I2C libre | — | SDA 7, SCL 6 (sensores: BME280, etc.) |
| Bluetooth / WiFi | BLE 5, WiFi b/g/n | BLE KISS y servicio de control (app y Display), WebUI y punto de acceso |

**Banda:** la web de Heltec vende el modelo LF como «470–510 MHz», pero su hoja de datos (V1.1) lo da también para **EU433 (433,175–434,665 MHz)**, que es el de 70 cm y el que hay que usar en España (433,775 MHz). El perfil de 434,855 MHz queda un poco por encima de ese margen; el de 439,9125 MHz, bastante fuera (perderá potencia y sensibilidad). Potencia: 21 ± 1 dBm en todas las bandas.

## Lo que hace el firmware con esta placa

- **GNSS UC6580:** al encenderlo (al arrancar y al volver del modo eco) se configura sin bloquear el bucle: `$CFGSYS,h35155` (GPS L1+L5, BeiDou B1I+B2a, GLONASS, Galileo E1+E5a, SBAS y QZSS; reinicia el receptor), frases GSA y GSV activadas para el gráfico del cielo de la app y sin GLL ni los `$TXT` de avisos. No se guarda en el receptor: se repite cada vez que vuelve a tener alimentación. Las órdenes son las que usa Meshtastic para este módulo.
- **Reset del GNSS (GPIO 35, activo a nivel bajo):** si el receptor encendido pasa 30 s sin mandar nada, se reinicia por su pin de reset (como mucho cada 2 min) y se vuelve a configurar. Con el GNSS apagado el pin queda en alta impedancia para no alimentarlo por ahí.
- **PPS (GPIO 36):** el flanco del pulso por segundo marca el comienzo exacto del segundo UTC (≈1 ms) para las ranuras de WSPR, FT8 y demás. Solo se usa si llega a 1 Hz; sin fix (sin PPS) se usa, como antes, la hora de la frase NMEA y su antigüedad. Lo mismo vale para las demás placas con `GPS_PPS` (T-TWR Plus, T-Beam 1W).
- **Potencia:** hasta 22 dBm (el máximo del SX1262), en el perfil LoRa de la WebUI.
- **Apagado (reposo profundo):** se retienen `Vext` apagado (GNSS y pantalla), el divisor de batería desconectado y la luz de la pantalla apagada, para acercarse a los 15 µA de la hoja de datos; al arrancar se sueltan.

No tiene PMU (AXP), así que no aparecen «PMU POWER OFF» ni el estado del cargador; la tensión de la batería se lee por el ADC.

## Versiones

- **V1.1** (esquema HTIT-Tracker V0.5): el GNSS se enciende con GPIO 3 a nivel alto (lo que hace el firmware) y la medida de batería necesita `ADC_CTRL` (GPIO 2) alto.
- **V1.0:** cambia el control de alimentación del GNSS; si el GPS no arranca en una V1.0, mira el registro de cambios del hardware de Heltec.

## Antena del GNSS

De serie una resistencia lleva la señal a la antena de la placa. Para usar el conector IPEX hay que girarla 90° y soldarla en la posición marcada (con una de 0 Ω quedan las dos a la vez, con peor señal).

## Sin probar

Todo lo anterior y las pantallas TFT nuevas de APRS LoRa+ solo se comprobaron compilando; falta verlo en la placa (sobre todo las órdenes del UC6580, el nivel del reset y el consumo en reposo).
