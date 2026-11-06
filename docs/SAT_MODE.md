# Modo SAT (satélites en 70 cm) y control de rotor

*Copyright (c) 2026 Manuel - EA7JRS. Estado: **fase 1**, sin probar en placa.*

## Qué hace (fase 1)

Se elige en **SELECT MODE → Satellites 70cm** (y desde la app por BLE: modo 9). Es un modo de **solo cálculo**: no emite ni procesa paquetes de radio.

- Lista de satélites con enlace de subida y/o bajada entre 420 y 450 MHz (datos de CelesTrak y SatNOGS).
- Para el satélite elegido: **azimut, elevación, distancia, altura y velocidad radial**, próximo paso (AOS, LOS, elevación máxima) y **Doppler** de cada transmisor (lo que oyes en la bajada y lo que hay que emitir en la subida para que llegue en su frecuencia). La frecuencia de subida y de bajada de un transpondedor lineal se muestra en su centro.
- Posición de la estación: GPS; sin fix se usa la última posición conocida y, si no hay, la posición fija de la WebUI (*Satellites 70 cm*).
- Hora: UTC del GPS. Sin hora válida no se calcula nada.

Pantalla **SATELLITE** (pulsación larga → submenú): *Next / Previous satellite*, *Change view* (ahora / próximo paso / frecuencias), *Update TLE (WiFi)*.

## Datos orbitales

El tracker descarga por WiFi (la red de *Firmware update* de la WebUI) el archivo `sats.txt` y lo guarda en la memoria flash. Lo genera cada día un workflow de GitHub (`.github/workflows/satdata.yml`, `tools/make_sat_data.py`) y lo publica en la rama `satdata` del repositorio. La dirección se puede cambiar en la WebUI (*Data file address*). Los TLE envejecen: la precisión baja pasados unos días; la pantalla muestra su edad.

Formato (texto, un bloque por satélite): `S|norad|nombre`, las dos líneas del TLE y `T|subidaMin|subidaMax|bajadaMin|bajadaMax|modo|baudios|inversor|descripción` por transmisor (Hz; 0 = sin dato).

## Radio

En esta fase la radio no se usa. Lo que sí admite el SX1262 en 70 cm: CW por portadora (A1A) y FSK/GMSK de 1,2 a 9,6 kbaud y LoRa; **no** BPSK, SSB ni AFSK. Las fases siguientes (recepción de CW y telemetría con Doppler, emisión con precompensación y PTT, integración con GPredict) no están hechas.

## Control de rotor

Se configura en la WebUI (*Antenna rotor*). El tracker apunta el rotor al satélite elegido desde `pre-position` segundos antes del AOS hasta el LOS, y opcionalmente lo lleva a una posición de reposo. Solo envía una orden si el objetivo cambia más que el *Step* (0,1–10°) o cada 10 s.

| Transporte | Qué habla | Notas |
|---|---|---|
| **WiFi / TCP** | cliente de `rotctld` de Hamlib (puerto 4533): `P az el` y `p` | el PC o la Raspberry con `rotctld` debe ser alcanzable desde la WiFi de *Firmware update* (p. ej. el hotspot del móvil) |
| **Bluetooth SPP** | Yaesu GS-232 (`Waaa eee`) | solo placas con Bluetooth clásico (ESP32 original) y con el Bluetooth del tracker apagado |
| **BLE → app** | evento `rot` (ver `BLE_CONTROL.md`) | la app Android debe retransmitirlo al rotor; la app aún no lo implementa |

Limitaciones: acimut de 0 a 359° (rotores de 450° sin tratamiento especial), elevación 0–90°, sin corrección de refracción. Ejemplo para probar sin rotor: `rotctld -m 1 -t 4533` (rotor «dummy» de Hamlib) y mirar `rotctl -m 2 -r IP:4533 p`.

**Sin probar con rotor real ni en placa.** El cálculo SGP4, Doppler y pasos sí está comprobado en PC contra las referencias de Vallado y una implementación independiente (`tools/test_sat_core.cpp`).
