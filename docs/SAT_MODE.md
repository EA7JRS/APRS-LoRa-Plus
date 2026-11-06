# Modo SAT (satélites en 70 cm) y control de rotor

*Copyright (c) 2026 Manuel - EA7JRS. Estado: **fase 1**, sin probar en placa.*

**Solo en las placas que lo admiten** (con pantalla y no el ESP32 original, p. ej. T-Beam Supreme S3): en las demás no aparece en el menú, ni en la app, ni en la WebUI.

## Qué hace (fase 1)

Se elige en **SELECT MODE → Satellites 70cm** (y desde la app por BLE: modo 9). Es un modo de **solo cálculo**: no emite ni procesa paquetes de radio.

- Lista de satélites con enlace de subida y/o bajada entre 420 y 450 MHz (datos de CelesTrak y SatNOGS).
- Para el satélite elegido: **azimut, elevación, distancia, altura y velocidad radial**, próximo paso (AOS, LOS, elevación máxima) y **Doppler** de cada transmisor (lo que oyes en la bajada y lo que hay que emitir en la subida para que llegue en su frecuencia). La frecuencia de subida y de bajada de un transpondedor lineal se muestra en su centro.
- Posición de la estación (*Station position* en la WebUI; la pantalla muestra la fuente arriba a la derecha): **GPS del tracker como referencia** (`GPS`; sin fix, la del móvil `PHN`), **fusión** del GPS del tracker y del móvil (`FUS`: media ponderada por la inversa de la varianza; precisión del tracker ≈ HDOP × 5 m, la del móvil la manda la app) o **solo el móvil**. La posición del móvil la envía la app por BLE con la orden `pos` (ver `BLE_CONTROL.md`) y caduca a los 2 minutos. Si no hay ninguna: última posición conocida (`LAST`) y, si no, la fija de la WebUI (`FIX`).
- Hora: UTC del GPS. Sin hora válida no se calcula nada.

Pantalla **SATELLITE** (pulsación larga → submenú): *Next / Previous satellite*, *Change view* (ahora / próximo paso / frecuencias), *Update TLE (WiFi)*.

## Categorías: 70 cm, LoRa / LR-FHSS / TinyGS e Inmarsat

El submenú tiene *List: All / 70cm / LoRa/TinyGS / GEO/Inmarsat* para filtrar la lista:

- **70cm**: satélites con subida o bajada en 420–450 MHz.
- **LoRa/TinyGS**: transmisores SatNOGS con modo LoRa o LR-FHSS en cualquier banda (433, 868, 915 MHz, 2,4 GHz...) y los LACUNASAT. Sirve para saber cuándo pasa un satélite TinyGS o de LR-FHSS (el tracker solo *emite* LR-FHSS; la recepción de estos satélites la hace tu estación TinyGS).
- **GEO/Inmarsat**: satélites geoestacionarios (periodo ≈ 1 día sideral). **Solo seguimiento**: no hay pasos, la pantalla da su azimut/elevación fijos (o «below horizon») y sirven para apuntar una antena o un rotor. Las frecuencias que se muestran (banda L: bajada 1525–1559 MHz, subida 1626,5–1660,5 MHz, y los canales STD-C 1537,10 y 1541,45 MHz) son **nominales, para sintonizar un SDR u otro equipo**; el canal concreto depende del satélite y de la región: verifícalo. El tracker no recibe ni emite en banda L.

Las frecuencias se guardan con 64 bits (la banda L y los 2,4 GHz no caben en 32).

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

### Estación fija o en movimiento

*Rotor mounting* en la WebUI:

- **Fixed station**: el azimut que recibe el rotor está referido al norte.
- **Mobile / vehicle**: el rotor va en un vehículo, así que el azimut que se le manda es **relativo al rumbo del vehículo** (azimut al objetivo − rumbo). El rumbo verdadero sale del curso GPS cuando se va a 5 km/h o más y, parado, del magnetómetro (con la declinación); si no hay ninguno, se mantiene el último curso 30 s y después el rotor espera («No heading»). En movimiento la posición se toma del GPS (o de la fusión con el móvil) y el objetivo se recalcula cada 0,5 s; el GPS no entra en reposo mientras el rotor móvil está activo. Los pasos de satélite se recalculan si la estación se mueve más de 0,5° (≈ 55 km). No se compensa la inclinación del vehículo (cabeceo / alabeo) ni el retardo del rotor. Para que el rumbo sea fiable parado hace falta magnetómetro calibrado; sin él, el rotor móvil solo apunta bien en marcha.

### Objetivos terrestres (estaciones APRS y repetidores)

El rotor no sirve solo para satélites. En *What the rotor follows* se elige:

- **Satélite** (solo en modo SAT).
- **Estación APRS con GO TO**: el rotor sigue la estación que hayas puesto como destino de la brújula (STATIONS, LAST o GROUP → *GO TO*), con su posición actualizada en vivo cada vez que se oye.
- **Objetivo fijo**: hasta 8 en la WebUI (nombre, latitud, longitud, altitud, frecuencia) o por BLE desde la app (orden `tgt`). Sirve para un repetidor, una baliza, un nodo de la red...

Fuera del modo SAT aparece la pantalla **ROTOR** (solo con el rotor activado y una fuente terrestre) con el rumbo, la elevación, la distancia y la posición real del rotor si el transporte la devuelve (`rotctld` sí). Rumbo y distancia sobre una Tierra esférica; la elevación incluye la diferencia de altura y la curvatura con refracción estándar (radio efectivo 4/3).

**Repetidores y RepeaterBook.** RepeaterBook Connect solo envía frecuencia, desplazamiento y tono a radios compatibles (con suscripción) y no ofrece ninguna interfaz abierta para terceros; su API exige permiso y prohíbe reutilizar los datos en otros servicios. Por eso el tracker no lleva datos de RepeaterBook: la app Android, con el acceso que tenga el usuario a ese servicio, manda al tracker solo el repetidor elegido (`tgt`). La app aún no lo hace.

**Sin probar con rotor real ni en placa.** El cálculo SGP4, Doppler y pasos sí está comprobado en PC contra las referencias de Vallado y una implementación independiente (`tools/test_sat_core.cpp`).
