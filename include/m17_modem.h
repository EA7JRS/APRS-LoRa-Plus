/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ (LoRa APRS Tracker fork).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * M17 (4FSK 4800 simbolos/s, RRC alfa 0,5) por el audio plano de la radio FM SA868 de la LilyGO T-TWR: modo paquete (emision y
 * recepcion) y escucha de las emisiones de voz (indicativos y META del LSF / LICH: posicion GNSS). Las tramas (LSF, paquete,
 * corriente), el codigo convolucional, el Viterbi, el Golay, el intercalado y el aleatorizado son de libm17 (lib/libm17, M17
 * Foundation, GPL-2.0-or-later). Aqui: el filtro RRC a 48 kHz (10 muestras por simbolo), la busqueda de las palabras de sincronia
 * con normalizacion de nivel y el armado de los paquetes. Codigo C++ puro: se prueba en el PC con tools/afsk/test_m17.cpp
 * (frente a m17-packet-encode / m17-packet-decode de M17_Implementations).
 */

#ifndef M17_MODEM_H_
#define M17_MODEM_H_

#include <stddef.h>
#include <stdint.h>

namespace M17M {

    constexpr uint32_t FS           = 48000;    // muestras por segundo de la banda base
    constexpr int      SPS          = 10;       // muestras por simbolo
    constexpr size_t   MAX_PACKET   = 823;      // bytes de un paquete (identificador de protocolo incluido, CRC aparte)
    constexpr uint8_t  PROTO_RAW    = 0x00, PROTO_AX25 = 0x01, PROTO_APRS = 0x02, PROTO_SMS = 0x05;

    // Simbolos (+-1, +-3) de una emision en modo paquete: preambulo, LSF, tramas de paquete (CRC anadido aqui) y EOT.
    // data[0] es el identificador de protocolo (0x05 SMS, 0x02 APRS...). Devuelve el numero de simbolos o 0 si no vale
    size_t packetSymbols(const char* src, const char* dst, uint8_t can, const uint8_t* data, size_t len, float* syms, size_t maxSyms);
    constexpr size_t maxPacketSymbols() { return 192 * (2 + 34 + 1); }     // preambulo + LSF + 33 tramas + EOT, con margen

    // Banda base: impulsos de los simbolos -> RRC (los coeficientes de libm17) a 48 kHz. amplitude 0..1: nivel del simbolo +-3
    class BasebandTx {
    public:
        void   start(const float* syms, size_t n, float amplitude);
        size_t fill(int16_t* out, size_t maxSamples);
        bool   done() const { return pos >= total; }
    private:
        const float* syms = nullptr;
        size_t n = 0, pos = 0, total = 0;
        float  amp = 0;
    };

    struct Lsf {
        char     src[10], dst[10];
        uint16_t type;
        uint8_t  meta[14];
        bool     stream;                        // true: de una emision de voz / datos (corriente), false: de un paquete
        bool     fromLich;                      // armado con los trozos del LICH (no se oyo el LSF del principio)
        bool     hasPosition;                   // META con posicion GNSS valida
        float    lat, lon, altitude, speed;     // grados, m, km/h
        uint16_t bearing;
        uint8_t  stationType;                   // 0 fija, 1 movil, 2 portatil
        uint8_t  gnssValid;                     // bits de validez de libm17 (M17_GNSS_ALT_VALID, M17_GNSS_SPD_BEARING_VALID...)
    };

    // ---- APRS sobre M17 (el modo M17 del tracker) -----------------------------------------------------------------------
    // Todo lo que emite el tracker en M17 va en paquetes de protocolo APRS (0x02) con el texto TNC2 entero. Lo recibido se pasa a
    // APRS en TNC2 para el resto del firmware; lo que no es un paquete APRS de verdad (SMS, emisiones de voz) lleva NOGATE en la
    // ruta para que ningun iGate lo suba a APRS-IS.
    constexpr const char* TOCALL = "APZM17";

    size_t tnc2ToPacket(const char* tnc2, uint8_t* out, size_t outMax);       // 0x02 + texto
    size_t smsPacket(const char* text, uint8_t* out, size_t outMax);          // 0x05 + texto + 0
    // Paquete recibido -> TNC2. 0x02: el texto tal cual (sin cabecera "SRC>DST:" se pone la del LSF); 0x05 SMS: mensaje APRS
    // "SRC>APZM17,NOGATE::DST      :texto" (67 caracteres como mucho). 0 si no se convierte (otros protocolos, datos no validos)
    size_t packetToTnc2(const Lsf& lsf, const uint8_t* data, size_t len, char* out, size_t outMax);
    // LSF de una emision de voz / datos -> posicion APRS (GNSS del META) o estado "M17 voice to DST". 0 si no vale
    size_t lsfToTnc2(const Lsf& lsf, char* out, size_t outMax);
    // Indicativo M17 -> direccion APRS (A-Z, 0-9 y '-': '/' y '.' pasan a '-'; 9 caracteres). false si no es un indicativo
    bool   aprsCall(const char* m17, char* out, size_t outMax);

    typedef void (*LsfCallback)(const Lsf& lsf, void* ctx);
    typedef void (*PacketCallback)(const Lsf& lsf, const uint8_t* data, size_t len, void* ctx);   // data[0] = protocolo, sin CRC

    class Rx {
    public:
        bool begin();
        void reset();
        void setCallbacks(LsfCallback onLsf, PacketCallback onPacket, void* ctx) { lsfCb = onLsf; pktCb = onPacket; cbCtx = ctx; }
        void feed(float x);                     // una muestra de audio a 48 kHz (escala +-1)
        bool dataCarrier() const { return busy > 0; }
        uint32_t lsfs = 0, packets = 0, crcErrors = 0;
    private:
        static constexpr int TAPS = 81;         // RRC de 8 simbolos (libm17 rrc_taps_10)
        static constexpr int HIST = 16 * SPS;   // filtrado: lo justo para comparar la palabra de sincronia (8 simbolos)
        float    xin[TAPS * 2]; int xpos = 0;
        float    y[HIST]; int ypos = 0;
        float    dc = 0, rmsAvg = 0;
        // busqueda de la sincronia: mejor distancia en una ventana de +-SPS/2 muestras
        int      candType = -1, candWait = 0; float candDist = 0, candGain = 1;
        int      sinceFrame = -1, lockType = -1, lockAt = 0; float lockDist = 1e9f, lockGain = 1;     // enganchado tras una trama
        // carga util en curso
        int      plType = -1, plCount = 0, plPhase = 0; float plGain = 1;
        float    pld[184];
        int      busy = 0;
        // paquete en curso
        uint8_t  pkt[33 * 25 + 2]; int lastFn = -1; bool haveLsf = false;
        Lsf      cur;
        // LSF de la corriente por el LICH
        uint8_t  lichBuf[30]; uint8_t lichMask = 0;
        LsfCallback lsfCb = nullptr; PacketCallback pktCb = nullptr; void* cbCtx = nullptr;
        float    sync[4][8];                    // LSF, paquete, corriente, EOT
        float    tail(int k) const;             // salida filtrada de hace k muestras
        void     frameDone();
        void     startPayload(int type, float gain, float dist, int lag);
        bool     fillLsf(const void* lsfRaw, bool stream, bool fromLich);
    };
}

#endif
