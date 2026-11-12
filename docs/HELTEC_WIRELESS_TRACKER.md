# Heltec Wireless Tracker en APRS LoRa+

Placa `heltec_wireless_tracker` (instalador web y `heltec_wireless_tracker.zip` de cada release). Especificaciones del fabricante: <https://heltec.org/project/wireless-tracker/#Specifications>.

## Especificaciones y cómo las usa el firmware

| Parte | Fabricante | En el firmware |
|---|---|---|
| MCU | ESP32-S3FN8 (8 MB de flash, sin PSRAM) | `board = esp32-s3-devkitc-1` (8 MB, sin PSRAM) |
| LoRa | SX1262 con TCXO, 21 ± 1 dBm, −134 dBm | `HAS_SX1262`, `HAS_TCXO`; SCK 9, MISO 11, MOSI 10, CS 8, RST 12, DIO1 14, BUSY 13 |
| Bandas LoRa | versiones de 470–510, 863–870 y 902–928 MHz | la que elijas al comprarla: el SX1262 llega de 150 a 960 MHz, pero el filtro y la adaptación de la placa son de esa banda |
| GNSS | UC6580 (Unicore), L1 + L5, GPS, GLONASS, BeiDou, Galileo, NavIC, QZSS | UART 115200: RX del ESP32 = 33, TX = 34; RESET 35, PPS 36. Frases `$GN…`; el gráfico del cielo (`sky`) reconoce también NavIC (`GI`, sistema 6 de GSA) |
| Pantalla | TFT 0,96″ 160 × 80 (ST7735) | TFT_eSPI: MOSI 42, SCLK 41, CS 38, DC 40, RST 39, luz 21 |
| Alimentación de GNSS y pantalla | `Vext` (GPIO 3 a nivel alto) | `VEXT_CTRL 3`: al apagar el GPS (ahorro) también se apaga la pantalla |
| Batería | Li-ion 3,7 V con carga (SH1.25-2) | divisor 390 k + 100 k en GPIO 1; `ADC_CTRL 2` a nivel alto para medir (V1.1) |
| Botones | USER (GPIO 0) y RST | `BUTTON_PIN 0` (también despierta del reposo) |
| I2C libre | — | SDA 7, SCL 6 (sensores: BME280, etc.) |
| Bluetooth / WiFi | BLE 5, WiFi b/g/n | BLE KISS y servicio de control (app y Display), WebUI y punto de acceso |

**Ojo con la banda:** Heltec no vende versión de 433 MHz. El APRS LoRa de España y casi toda Europa va en **433,775 MHz**, fuera de las tres versiones: la de 470–510 MHz es la más cercana pero perderá alcance, y la de 863–870 MHz solo sirve para la red de 868 MHz (869,525 MHz, usada en algunos países), que no está entre los perfiles LoRa de serie. En 902–928 MHz (América) el perfil de 915 MHz sí está.

No tiene PMU (AXP), así que no aparecen «PMU POWER OFF» ni el estado del cargador; la tensión de la batería se lee por el ADC.

## Versiones

- **V1.1:** el GNSS se enciende con GPIO 3 a nivel alto (lo que hace el firmware) y la medida de batería necesita `ADC_CTRL` (GPIO 2) alto.
- **V1.0:** cambia el control de alimentación del GNSS; si el GPS no arranca en una V1.0, mira el registro de cambios del hardware de Heltec.

## Antena del GNSS

De serie una resistencia lleva la señal a la antena de la placa. Para usar el conector IPEX hay que girarla 90° y soldarla en la posición marcada (con una de 0 Ω quedan las dos a la vez, con peor señal).

## Sin probar

Las pantallas TFT nuevas de APRS LoRa+ solo se comprobaron compilando; falta verlas en la placa.
