# Receptor AIS

*Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later. **Sin probar con barcos reales**: el núcleo está probado en el PC (`tools/ais/test_ais.cpp`, en la CI).*

Barcos del VHF marino (AIS, ITU-R M.1371: GMSK a 9600 baudios en **161,975 MHz** canal A / 87B y **162,025 MHz** canal B / 88B). Tres fuentes, que se juntan en la misma lista:

| Fuente | Placas | Cómo |
|---|---|---|
| **SA868 de la T-TWR** | T-TWR con módulo de VHF | SELECT MODE → *AIS receiver* (modo 12, solo recepción). Audio plano, canal de 25 kHz, silenciador abierto. Un canal cada vez: A, B o alternando cada *Seconds per channel* (cada barco alterna los dos canales, así que con uno solo se oyen todos, la mitad de los mensajes). |
| **Módulo AIS externo** | todas | Receptor con salida NMEA (dAISy, Quark-elec QK-A02x...) en el UART 2: su TX al GPIO de *Module TX → GPIO (RX)*, 38400 baudios. Si la salida NMEA por UART está activa, comparte el puerto (TX al plotter, RX del módulo). Funciona en cualquier modo. |
| **App Android** | todas | El RTL-SDR del móvil decodifica los dos canales y manda las frases con la orden BLE `aisin`. |

## Demodulación (T-TWR)

Compensación del acoplo de alterna de la entrada de audio de la placa (≈ 320 Hz, `ais.hpfHz`; el mismo que en DAPNET), paso bajo, CAG, umbral, PLL del reloj de bit, NRZI, HDLC (banderas, bits de relleno) y FCS CRC-16 X.25. En el PC, con señal GMSK BT 0,4 generada a 48 kHz con el acoplo de 320 Hz: 20/20 tramas hasta σ = 0,05 y 19/20 con σ = 0,10 (sin la compensación, 0–3/20).

## Mensajes

1, 2, 3 (posición clase A), 4 (estación base), 5 (nombre, indicativo, IMO, tipo, eslora / manga, calado, destino, ETA), 9 (aeronave SAR), 14 (seguridad: AIS-SART, MOB, EPIRB), 18 y 19 (clase B), 21 (ayuda a la navegación) y 24 (datos estáticos clase B). Comprobados con los vectores de gpsd / pyais.

## Página AIS

Lista por distancia (nombre o MMSI, `!` = peligro, millas y rumbo verdadero) y detalle (nombre, MMSI, clase, indicativo o tipo, distancia y rumbo, velocidad / rumbo / proa / estado, CPA y TCPA o destino o dimensiones, antigüedad). Pulsación larga: *View*, *Next / Previous vessel* (en la T-TWR, el encoder), **GO TO** (el barco pasa a la lista de estaciones con su MMSI y la brújula, y el rotor con la fuente 1, lo siguen con su posición al día), *Channel* (T-TWR), *Collision alarm*, *Clear list*. En el modo AIS se ven también GPS, COMPASS, CLOCK y TELEMETRY.

## Alarmas

- **Riesgo de colisión**: CPA por debajo de *CPA below* (0,5 millas) en los próximos *within* minutos (15), con el movimiento propio del GPS; o dentro de la **zona de guardia** (0 = apagada). Cada 5 s se mira el peor; suena como las alarmas de emergencia (*COLLISION RISK*, 10 min sin repetir para el mismo barco).
- **Socorro**: MMSI 970 (AIS-SART), 972 (MOB), 974 (EPIRB-AIS), estado 14 o mensaje 14 con «SART» / «MOB» / «EPIRB» (salvo «TEST»): *AIS DISTRESS* con la posición para GO TO.

## NMEA y app

Con *!AIVDM to the NMEA output and the app* cada mensaje sale como frases `!AIVDM` por la salida NMEA de la placa (USB, TCP, UART: OpenCPN, Navionics, plotter) y a la app por BLE (canal NMEA, si está suscrita). La app recibe además el evento `ais` de cada barco y pide la lista con `aislist` (ver `BLE_CONTROL.md`).

## Sin verificar

- Que el SA868 (NiceRF y OpenRTX) sintonice 162 MHz con audio plano y que el ADC dé suficiente nivel; el umbral del demodulador con ruido real.
- El orden de los bits en el aire se ha tomado de la norma (octetos con el bit de menor peso primero, como HDLC): comprobado de punta a punta con nuestro generador y con frases NMEA reales, no con una grabación de radio.
