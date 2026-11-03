# ft8lib (subconjunto)

Copia **sin modificar** de los ficheros de `ft8/` de [ft8_lib](https://github.com/kgoba/ft8_lib) de Karlis Goba (YL3JG), licencia MIT (ver `LICENSE`).
Solo se incluye lo necesario para **codificar**: `constants`, `crc`, `encode`, `message` y `text`. No incluye el decodificador (`decode`, `ldpc`).
Se compila con `-w` (véase `library.json`) para que los avisos del código original no rompan `-Werror` del proyecto.
Lo usa `src/ft_beacon.cpp` (modos FT8 y FT4 de APRS LoRa+).
