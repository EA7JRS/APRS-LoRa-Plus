# APRS LoRa+ 5.3.5

- **Modo SAT** (*Satellites 70cm*): lista de satélites (70 cm, LoRa / LR-FHSS / TinyGS, Inmarsat), azimut, elevación, distancia, próximo paso y Doppler; recepción con Doppler (CW, FSK / GMSK, LoRa) y emisión con precompensación (apagada por defecto, solo a petición y con indicativo real). El archivo de satélites lo sube la app Android por BLE. Ver `docs/SAT_MODE.md`.
- **Rotor**: rotctld (TCP), GS-232 (Bluetooth SPP) y BLE; sigue un satélite, GO TO, un objetivo fijo, **la última estación APRS oída** (con estimación en movimiento) o **la última baliza CW con locator**. El servidor rotctld del tracker atiende a GPredict.
- **La actualización por internet pasa a la app Android** (internet del móvil + BLE): el tracker ya no lleva cliente HTTPS. La WebUI conserva la subida de un archivo `.bin`.
- **Menos flash**: configuración por tabla de campos y WebUI más ligera. T-Beam Supreme: 91,1 % → 85,2 % de la partición de aplicación.
- **App Android**: escucha CW, A-GPS para el u-blox M10, rotor y envío del archivo de satélites.
- **GPS u-blox M10** a 115200 baudios; **LED verde Rx** activo de fábrica para balizas y CW; **logo nuevo** en la pantalla de bienvenida; **instalador web** con Tracker, Rotor y Gateway.
- **Sin probar en placa**: todo lo nuevo de esta versión. Lista completa en `CHANGELOG.md`.
