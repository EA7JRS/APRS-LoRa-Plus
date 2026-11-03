# Versión 5.3.0 (2026-11-03)

Incluye todo lo de 5.2.12 y 5.2.11 más lo siguiente.

## FT8 y FT4 (solo transmisión)
- Nuevos modos de arranque **FT8 beacon** y **FT4 beacon** (SELECT MODE), solo en SX1262/SX1268. Como WSPR y MORSE, no se guardan como último modo.
- Mensaje estándar «CQ INDICATIVO LOCATOR» (indicativo sin SSID, locator de 4 caracteres fijo o del GPS) o texto libre de hasta 35 caracteres.
- Ranuras desde la hora GPS: FT8 15 s (inicio a +0,5 s, 12,64 s de emisión), FT4 7,5 s (inicio a +0,3 s, 5,04 s). Una emisión cada N ciclos (2–240), repartida por indicativo para que no coincidan las balizas.
- Modulación: la trayectoria GFSK de WSJT-X (pulso gaussiano de tres símbolos; BT 2,0 en FT8 y 1,0 en FT4) se escribe en el PLL del SX126x 64 veces por símbolo en FT8 (cada 2,5 ms) y 20 en FT4 (cada 2,4 ms). Una simulación con el paso de PLL de 0,954 Hz y mantenimiento de orden cero sigue decodificando FT8 (espurios por debajo de −58 dBc a más de 60 Hz).
- La frecuencia es la de **dial**: el tono 0 sale 1500 Hz por encima, como un transceptor en USB. Valor por defecto FT8 432,174 MHz (lista de WSJT-X). **FT4 no tiene frecuencia de 70 cm por defecto**: no emite hasta que se escribe una. A la corrección global en ppm se suma un desplazamiento propio.
- La pantalla no se redibuja durante la emisión (el bus I2C pararía el PLL); el botón aborta.
- Codificador (CRC-14, LDPC 174,91, sincronismo, mapa Gray y empaquetado): `lib/ft8lib`, copia sin modificar de ft8_lib (Karlis Goba YL3JG, MIT). Nuestro código: `include/ft_beacon.h`, `src/ft_beacon.cpp`; página y temporizador en `pages_utils.cpp`.

## WebUI
- Tarjeta **FT8 / FT4 Beacon** (región IARU, frecuencia de dial, potencia, ciclos, desplazamiento, locator, texto y pantalla apagada) con un aviso del segmento del plan de bandas (IARU R1/R2/R3: balizas, repetidores, voz digital, banda de señales débiles).
- About: enlace visible a https://github.com/EA7JRS/APRS-LoRa-Plus.

## LR-FHSS activable desde la WebUI
La tarjeta «LR-FHSS Beacon» deja de ser un marcador: interruptor de activación, frecuencia central, anchura de rejilla, velocidad de código, rejilla estrecha, potencia, intervalo y locator, con el ancho ocupado y el aviso de segmento calculados en la página (la misma regla que `LRFHSS::freqCheck()`). Sigue **apagada por defecto** (`lrfhss.enabled=false`): al marcar el interruptor aparece «LR-FHSS beacon» en SELECT MODE. El firmware ya lo admitía; solo faltaba el formulario. El titular de la estación es responsable de la autorización que exijan las normas nacionales. Sin verificar en el aire.

## Estudio de cobertura
`docs/ESTUDIO_COBERTURA.md` y `.pdf`: todos los modos del proyecto (APRS LoRa en SF7 y SF9 CR4/5 BW125, FSK 9600, CW, WSPR, LR-FHSS, DAPNET, FT8, FT4) y los candidatos FST4W y D-Star/DPRS, con el plan de bandas IARU de las tres regiones. Los supuestos del modelo están indicados en el documento; los umbrales de FSK, POCSAG y D-Star son estimaciones.

## Aplazado a 5.4
- **FST4W**: el empaquetado de 50 bits y el LDPC(240,74) de WSJT-X 3.3 beta exigen una copia verificada; los submodos de −900 y −1800 s no caben en el paso del PLL.
- **D-Star / DPRS** (confirmado por el titular para 5.4): GMSK 4800 bit/s con FEC de cabecera y más de 255 bytes por trama (relleno del FIFO); requiere pruebas con un hotspot.

## Estado
**No se ha compilado ni probado en placa.** La integración se revisó a mano con `-Werror -Wall`; el resultado del CI de GitHub es la primera compilación real.
