# Modo WSPR beacon

Se elige en **SELECT MODE → WSPR beacon** (solo con SX1262/SX1268). Como MORSE, no se guarda y hay que elegirlo en cada arranque. Tiene una única pantalla, **WSPR**, y las balizas APRS quedan en manual.

## Qué emite
Mensaje WSPR **tipo 1**: indicativo del perfil de baliza (sin SSID; máximo 6 caracteres, sin prefijo ni sufijo tipo `/P`), **locator de 4 caracteres** y potencia. Se codifica a 162 símbolos 4-FSK (código convolucional K=32, entrelazado, vector de sincronismo) a 1,4648 baudios con 1,4648 Hz entre tonos. La emisión dura 110,6 s y empieza 1 s después del minuto par.

- **Hora:** del GPS (u-blox M10). Sin hora válida no se emite. La precisión es de ±0,3 s aproximadamente (hora de la sentencia NMEA más su antigüedad), suficiente para WSPR (tolera ±1 s).
- **Locator ("tracker"):** el de la posición GPS en el momento de cada emisión, o el fijo de la WebUI (4 caracteres).
- **Ranura:** una emisión cada N minutos (2, 4, 6, 8, 10, 12, 20, 30 o 60). El desplazamiento dentro del ciclo depende del indicativo para que no coincidan todas las balizas.
- **Tonos:** el SX1262 genera una portadora y se cambia la palabra del PLL en cada símbolo. El paso del PLL (0,954 Hz) no permite tonos separados exactamente 1,4648 Hz; por eso cada símbolo se emite en **dos tramos** (paso inferior y superior vecinos, repartidos para que la media sea el tono exacto) con una sola escritura extra del PLL a mitad del símbolo (*fine tone spacing*). Se puede desactivar en la WebUI. (Hasta la 5.2.9 se alternaban los pasos cada ~1 ms; eso modulaba la portadora a 1 kHz y producía un peine de líneas cada 50 Hz en la señal, medido con un SDR.)
- **Deriva:** *Drift compensation* suma una rampa lineal (Hz/min, centrada en la mitad de la emisión) para anular la caída de frecuencia por calentamiento. En el puerto serie se registran cada ~10 s las temperaturas internas (BME, IMU, PMU, ESP32) durante la emisión y cada 5 s en Tune, para correlacionarlas con la deriva medida.
- **Tabla por potencia:** el centro y la deriva cambian con la potencia (calentamiento del PA). Con *Per-power calibration table* cada potencia (0, 3, 7, 10, 13, 17, 20 dBm) tiene su propio offset y su compensación de deriva; la pantalla WSPR y WSPR Tune usan los de la potencia elegida.
- **Pantalla:** con *Screen off while transmitting* se apaga durante la emisión (menos consumo y calor junto al TCXO) y se vuelve a encender al terminar. El botón sigue abortando.

## Frecuencia
- 70 cm: **dial 432,300 MHz USB**, frecuencia central **432,3015 MHz** (`432301500` Hz; valor fijo, no modificable desde la WebUI). 433,600 MHz **no** es una frecuencia WSPR estándar: está en la zona de canales digitales/LoRa APRS de la IARU R1.
- Cámbiala en la WebUI si usas otra banda o plan de bandas. Solo bandas de aficionado y con tu licencia.

## Calibración de frecuencia (imprescindible)
WSPR solo decodifica si la señal queda a ±100 Hz del centro, es decir a ±0,23 ppm a 432 MHz. El TCXO del SX1262 tiene una tolerancia típica de ±1 a ±2 ppm (±400 a ±900 Hz) y deriva con la temperatura: **no basta con "tener TCXO"**.
1. Submenú → **Tune carrier**: emite una portadora sin modular durante 30 s en la frecuencia central.
2. Mídela con un SDR (o un receptor calibrado) y calcula `desplazamiento = nominal − medida` en Hz.
3. Introdúcelo en la WebUI (*Frequency offset*). Repite con la placa a temperatura de trabajo.
La deriva térmica restante puede seguir sacando la señal de la ventana; los spots con ΔF alto en wsprnet.org lo delatan.

## Pantalla y submenú
Indicativo, locator, potencia; frecuencia y desplazamiento; hora UTC y satélites; cuenta atrás hasta la próxima emisión (o `TX n/162` con barra de progreso); intervalo y emisiones enviadas. LP abre el submenú: **Beacon: ON/OFF**, **Tune carrier**, **Change mode**, **Back**. El botón aborta una emisión o un Tune; al abortar la baliza queda en OFF.

## Potencia y hardware
- Potencias válidas del mensaje: 0, 3, 7, 10, 13, 17, 20 dBm. La radio emite con esa misma potencia (por defecto 10 dBm).
- La portadora es continua el 92 % del tiempo (110,6 s de cada 120 s): usa poca potencia al principio y vigila la temperatura del SX1262.
- Los armónicos del SX1262 no son aceptables sin filtro: usa un **filtro paso bajo de 70 cm** antes de la antena.

## Límites
Solo mensajes tipo 1 (no hay prefijos/sufijos ni locator de 6 caracteres). Mientras dura la emisión el resto del Tracker está en pausa. Sin sueño profundo entre emisiones. El codificador se ha verificado en PC contra una implementación de referencia (JTEncode); la emisión con la radio, el cambio de tono en transmisión y la deriva de frecuencia **no se han probado en hardware**.
