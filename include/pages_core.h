/* Copyright (C) 2026 Ricardo Guzman - CA2RXU
 * Copyright (C) 2026 Manuel - EA7JRS (modificaciones y ampliaciones)
 *
 * This file is part of LoRa APRS Tracker.
 *
 * LoRa APRS Tracker is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LoRa APRS Tracker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.
 */

// Logica pura (sin Arduino) de las pantallas APRS: lista de estaciones, perfil de altitud,
// tendencia barometrica, fusion de rumbo y formateo de texto. Se prueba en PC.

#ifndef PAGES_CORE_H_
#define PAGES_CORE_H_

#include <stdint.h>
#include <stddef.h>

namespace PAGES_Core {

    // ---------- Geodesia ----------
    double      distanceKm(double lat1, double lon1, double lat2, double lon2);     // haversine
    double      bearingDeg(double lat1, double lon1, double lat2, double lon2);     // 0..360
    const char* cardinal16(double deg);                                             // "N", "NNE", ...
    float       wrap360(float deg);
    float       wrap180(float deg);                                                 // -180..180
    float       dewPointC(float tempC, float humidityPct);                          // Magnus

    // ---------- Estaciones APRS escuchadas ----------
    enum Kind : uint8_t { KIND_UNKNOWN, KIND_FIXED, KIND_MOBILE, KIND_OBJECT, KIND_WX, KIND_DIGI, KIND_IGATE, KIND_MSG, KIND_STATUS, KIND_TELEMETRY };

    // type: el de APRSPacket (0 gps, 1 mensaje, 2 estado, 3 telemetria, 4 Mic-E, 5 objeto)
    Kind        classify(int type, char symbol, int speedKmh, bool hasPosition);
    const char* kindName(Kind kind);

    // Datos meteorologicos ya convertidos a unidades metricas; el valor "no disponible" es -9999
    const int16_t WX_NA = -9999;
    struct WxData {
        bool        valid;
        int16_t     tempC;
        int16_t     humidity;       // %
        int16_t     pressureHpa;
        int16_t     windDir;        // grados
        int16_t     windKmh;
        int16_t     gustKmh;
        int16_t     rainMm10;       // lluvia ultima hora, decimas de mm
    };

    // Interpreta el texto meteorologico de un paquete APRS ("220/004g005t077r000p000h50b09900")
    bool        parseWx(const char* text, WxData& out);
    // Equipo a partir del tocall; en Mic-E (type 4) se mira el sufijo del comentario y se devuelve en *rest el resto
    void        deviceName(const char* tocall, int type, const char* comment, char* out, size_t size, const char** rest);
    // Estado Mic-E a partir de los tres bits del tocall ("111" = Off Duty ... "000" = Emergency)
    const char* miceStatusName(const char* bits);
    // Copia el comentario sin el bloque de altitud "/A=001234"
    void        cleanComment(const char* in, char* out, size_t size);
    // Nombre del objeto (9 caracteres tras ';') sin espacios finales
    void        objectName(const char* payload, char* out, size_t size);

    struct Station {
        char        call[11];
        char        symbol;         // simbolo APRS (un caracter) o ' '
        char        table;          // tabla del simbolo: '/' primaria, '\\' secundaria, o letra/digito de superposicion
        uint8_t     type;           // tipo del ultimo paquete
        Kind        kind;
        bool        hasPosition;
        float       lat, lon;
        int16_t     altitude;       // m
        int16_t     speed;          // km/h
        int16_t     course;         // grados
        int16_t     rssi;
        float       snr;
        uint32_t    lastMs;
        uint16_t    packets;
        char        device[18];     // equipo (por tocall o por Mic-E); vacio = desconocido
        char        path[22];       // camino del ultimo paquete, p. ej. "WIDE1-1*"
        char        comment[42];    // comentario / estado / nombre del objeto
        char        status[12];     // estado Mic-E (Off Duty, In Service, ...)
        WxData      wx;
    };

    class HeardList {
    public:
        static const uint8_t CAPACITY = 20;
        void        clear();
        // Anade o actualiza una estacion. Un paquete sin posicion no borra la ultima posicion conocida.
        void        update(const char* call, char symbol, int type, bool hasPosition, float lat, float lon,
                           int altitude, int speed, int course, int rssi, float snr, uint32_t nowMs,
                           const char* tocall = nullptr, const char* path = nullptr, const char* comment = nullptr, const char* status = nullptr, char table = '/');
        // Posicion de una estacion por indicativo (0 = la mas reciente); -1 si no esta
        int         indexOf(const char* call) const;
        uint8_t     count() const { return count_; }
        uint32_t    totalPackets() const { return total_; }
        // 0 = la mas reciente. Devuelve nullptr si no existe.
        const Station* byRecent(uint8_t position) const;
    private:
        Station     items_[CAPACITY];
        uint8_t     count_ = 0;
        uint32_t    total_ = 0;
    };

    // Vista ordenada y filtrada de la lista (pantalla STATIONS)
    enum StationSort : uint8_t { SORT_RECENT, SORT_NEAREST, SORT_NAME, SORT_COUNT };
    enum StationFilter : uint8_t { FILTER_ALL, FILTER_POSITION, FILTER_MOBILE, FILTER_FIXED, FILTER_WX, FILTER_DIGI, FILTER_COUNT };
    // Rellena out[] con posiciones de byRecent(); devuelve cuantas estaciones pasan el filtro.
    // Las estaciones sin posicion (o sin posicion propia) van al final al ordenar por cercania.
    uint8_t     buildStationView(const HeardList& list, uint8_t sort, uint8_t filter, bool haveMe, double myLat, double myLon, uint8_t* out);

    // ---------- Hora local y sol ----------
    enum ZoneMode : uint8_t { ZONE_AUTO_EU, ZONE_UTC, ZONE_MANUAL, ZONE_COUNT };
    bool        euDstActive(int64_t utcEpoch);                                         // horario de verano europeo (UTC 01:00 ultimo domingo de marzo a octubre)
    // Desfase respecto a UTC en minutos; *dst = true si esta en horario de verano; manualHalfHours en pasos de 30 min
    int         zoneOffsetMinutes(uint8_t mode, int manualHalfHours, int64_t utcEpoch, bool* dst);
    const char* zoneAbbreviation(uint8_t mode, bool dst);                              // "CET", "CEST", "UTC" o "" (manual)
    int         weekdayIndex(int64_t epoch);                                           // 0 = domingo
    const char* weekdayName(int index);                                                // "SUN".."SAT"
    void        civilFromEpoch(int64_t epoch, int& year, int& month, int& day);       // fecha del instante (en la escala dada)
    // Orto y ocaso en minutos UTC desde las 00:00 UTC del dia (pueden salir fuera de 0..1439: se normalizan al mostrar).
    // Devuelve 0 si hay orto y ocaso, 1 si el sol no se pone (dia polar), -1 si no sale (noche polar).
    int         sunTimes(double latDeg, double lonDeg, int year, int month, int day, double& sunriseMin, double& sunsetMin);

    // ---------- Perfil de altitud ----------
    // Memoria fija: cuando se llena, se promedian los puntos por parejas y se duplica el intervalo.
    class AltProfile {
    public:
        static const uint8_t CAPACITY = 96;
        static const uint32_t BASE_STEP_MS = 10000;
        void        clear();
        void        add(float altitudeM, uint32_t nowMs);
        uint8_t     count() const { return count_; }
        float       at(uint8_t index) const { return values_[index]; }
        float       minimum() const;
        float       maximum() const;
        uint32_t    stepMs() const { return stepMs_; }
        uint32_t    spanSeconds() const { return count_ > 1 ? (uint32_t)(count_ - 1) * (stepMs_ / 1000) : 0; }
        float       verticalSpeedMpm() const { return vspeed_; }    // m/min, filtrada
        void        zeroVerticalSpeed() { vspeed_ = 0; }           // equipo parado: el ruido del barometro/GPS no debe verse como ascenso
        float       ascentM() const { return ascent_; }
        float       descentM() const { return descent_; }
    private:
        float       values_[CAPACITY];
        uint8_t     count_ = 0;
        uint32_t    stepMs_ = BASE_STEP_MS;
        uint32_t    lastAddMs_ = 0;
        bool        started_ = false;
        // velocidad vertical y desnivel acumulado
        float       lastAlt_ = 0;
        uint32_t    lastAltMs_ = 0;
        bool        haveLast_ = false;
        float       vspeed_ = 0;
        float       ascent_ = 0, descent_ = 0;
        float       refAlt_ = 0;        // referencia con histeresis para el desnivel
        bool        haveRef_ = false;
    };

    // ---------- Tendencia barometrica ----------
    enum Trend : uint8_t { TREND_UNKNOWN, TREND_FALLING_FAST, TREND_FALLING, TREND_FALLING_SLOW, TREND_STEADY, TREND_RISING_SLOW, TREND_RISING, TREND_RISING_FAST };

    class PressureTrend {
    public:
        static const uint8_t CAPACITY = 19;                          // 3 h a 10 min (+1)
        static const uint32_t STEP_MS = 10UL * 60UL * 1000UL;
        void        clear();
        void        add(float pressureHpa, uint32_t nowMs);
        // Cambio en 3 h (se extrapola linealmente si hay entre 1 h y 3 h de historia)
        bool        delta3h(float& deltaHpa) const;
        Trend       trend() const;
        static Trend classifyDelta(float deltaHpa3h);
        static const char* trendName(Trend trend);
        static const char* trendShort(Trend trend);      // para pantallas estrechas
    private:
        float       values_[CAPACITY];
        uint8_t     count_ = 0;
        uint32_t    lastMs_ = 0;
        bool        started_ = false;
    };


    // ---------- Pronostico meteorologico local (reglas simples con presion, tendencia 3 h, humedad y T-Td) ----------
    // pressureHpa: ideal a nivel del mar; humidityPct < 0 = sin humedad; dewPointC solo se usa con humedad.
    // Devuelve un texto de como maximo 16 caracteres ("Rain likely", "Fair, stable"...).
    // deltaHpa3h: cambio de presion en 3 h (NAN si no se conoce); activa la matriz de decision (caida/subida fuerte + presion + T-Td).
    void        forecastText(float pressureHpa, bool hasTrend, Trend trend, float tempC, float humidityPct, float dewPointC, char* out, size_t size, float deltaHpa3h = NAN);
    float       cloudBaseM(float tempC, float dewPointC);                             // Hennig: (T - Td) * 125 m
    const char* convectionClass(float tempC, float dewPointC);                        // "Saturated" / "Cumulus" / "Moderate" / "Dry, stable"
    float       frostMinC(float temp18C, float dew18C, float groundK = 2.0f);          // T18 - 0,55 (T18 - Td18) - K

    // ---------- Alarmas de emergencia ----------
    enum AlarmKind : uint8_t { ALARM_NONE = 0, ALARM_EMERGENCY = 1, ALARM_MOB = 2 };
    // Busca palabras completas (sin distinguir mayusculas): SOS, EMERGENCY, MAYDAY -> emergencia; MOB, MAN OVERBOARD -> hombre al agua
    AlarmKind   alarmFromText(const char* text);
    const char* alarmTitle(AlarmKind kind);                                          // "EMERGENCY" / "MAN OVERBOARD"


    // ---------- Gesto de emergencia con el boton: 5 pulsaciones cortas o SOS en morse (... --- ...) ----------
    // Cada pulsacion se clasifica por su duracion: punto < 300 ms, raya 300..700 ms (mas larga = no es del gesto,
    // para no chocar con la pulsacion larga normal). Entre pulsaciones no pueden pasar mas de 1200 ms.
    class SosGesture {
    public:
        static const uint32_t DOT_MAX_MS = 300;
        static const uint32_t DASH_MAX_MS = 700;
        static const uint32_t GAP_MAX_MS = 1200;
        void        clear() { count_ = 0; seq_[0] = '\0'; }
        // pressMs: duracion de la pulsacion; gapMs: tiempo desde que se solto la anterior. true = gesto reconocido
        bool        onPress(uint32_t pressMs, uint32_t gapMs);
    private:
        char        seq_[12];
        uint8_t     count_ = 0;
    };

    // ---------- Registro de mensajes (recibidos y enviados) ----------
    enum MsgFlags : uint8_t { MSG_READ = 1, MSG_ACK_REQ = 2, MSG_ACKED = 4, MSG_FAILED = 8 };

    struct MsgEntry {
        char        dir;            // 'R' recibido, 'T' enviado
        uint8_t     flags;
        uint32_t    epoch;          // UTC en segundos; 0 = sin hora
        uint16_t    ackId;          // numero de confirmacion pedida (0 = sin confirmacion)
        char        call[11];
        char        text[68];
    };

    enum MsgState : uint8_t { MSTATE_UNREAD, MSTATE_READ, MSTATE_SENT, MSTATE_WAITING, MSTATE_ACKED, MSTATE_FAILED };

    class MessageLog {
    public:
        static const uint8_t CAPACITY = 60;
        void        clear() { count_ = 0; }
        uint8_t     count() const { return count_; }
        uint8_t     unread() const;
        const MsgEntry* byRecent(uint8_t position) const;       // 0 = el mas reciente
        void        addReceived(const char* call, const char* text, uint32_t epoch);
        void        addSent(const char* call, const char* text, uint32_t epoch, uint16_t ackId);
        void        append(const MsgEntry& entry);               // conserva el orden (al cargar del fichero)
        bool        markAcked(const char* call, uint16_t ackId);
        bool        markFailed(const char* call, uint16_t ackId);
        bool        markRead(uint8_t position);                  // true si cambio
        bool        markAllRead();
        bool        removeRecent(uint8_t position);
        static MsgState stateOf(const MsgEntry& entry);
        // Linea del fichero: dir|epoch|flags|ackId|call|texto
        static void format(const MsgEntry& entry, char* out, size_t size);
        static bool parse(const char* line, MsgEntry& entry);
    private:
        MsgEntry    items_[CAPACITY];       // 0 = el mas antiguo
        uint8_t     count_ = 0;
        void        push(const MsgEntry& entry);
        static void copy(char* destination, size_t size, const char* source);
    };

    // Siguiente linea de texto de como mucho `width` columnas, cortando en espacios. Devuelve false si no queda texto.
    bool        wrapNext(const char* text, size_t& position, size_t width, char* out, size_t outSize);
    // "21:05" si es del mismo dia UTC que `nowEpoch`, "21/10" si no, "--:--" sin hora
    void        formatMessageTime(uint32_t epoch, uint32_t nowEpoch, char* out, size_t size);
    void        formatMessageDateTime(uint32_t epoch, char* out, size_t size);     // "21/10/2026 21:05"

    // ---------- Calidad del aire (IAQ estimado con el BME680) ----------
    // No es el IAQ de Bosch BSEC (que usa una libreria propietaria): se estima con la resistencia del
    // sensor de gas frente a su maximo reciente (linea base adaptativa) y la humedad. 0 = aire excelente,
    // 500 = muy contaminado. Necesita unos minutos de calentamiento y su precision mejora con las horas.
    class IaqEstimator {
    public:
        static const uint32_t WARMUP_MS  = 5UL * 60UL * 1000UL;
        static const uint8_t  WARMUP_MIN_SAMPLES = 10;
        void        clear();
        void        add(float gasKohm, float humidityPct, uint32_t nowMs);
        bool        ready() const;
        uint32_t    warmupRemainingSec(uint32_t nowMs) const;
        float       index() const { return index_; }            // 0..500
        const char* label() const;                              // Great, Good, Fair, Poor, Bad, Awful
        static const char* labelFor(float index);
    private:
        float       baseline_ = 0;
        uint32_t    firstMs_ = 0, lastMs_ = 0;
        uint16_t    samples_ = 0;
        float       index_ = 0;
    };

    // ---------- Fusion de rumbo: magnetometro + giroscopo + rumbo GPS ----------
    enum HeadingSource : uint8_t { HSRC_NONE, HSRC_MAG, HSRC_GPS, HSRC_GYRO };

    class HeadingFusion {
    public:
        void        reset();
        // gyroDps: velocidad de giro en el eje vertical (si gyroValid). magHeading y gpsCourse en grados.
        float       update(float dtSec, bool gyroValid, float gyroDps, bool magValid, float magHeading,
                           bool gpsValid, float gpsCourse, float speedKmh);
        bool        valid() const { return valid_; }
        float       heading() const { return heading_; }
        HeadingSource source() const { return source_; }
        int         gyroSign() const { return gyroSign_; }
        void        setDefaultGyroSign(int sign) { gyroSign_ = sign >= 0 ? 1 : -1; votes_ = 0; }
    private:
        bool        valid_ = false;
        float       heading_ = 0;
        float       lastMeasured_ = 0;
        bool        haveLastMeasured_ = false;
        float       sinceMeasureSec_ = 0;
        HeadingSource source_ = HSRC_NONE;
        int         gyroSign_ = -1;         // con eje Z hacia arriba, giro horario = dps negativos
        int         votes_ = 0;
    };


    // ---------- Radio LoRa: tiempo en el aire (formula de Semtech AN1200.13) ----------
    // cr4 = denominador del code rate (5..8 para 4/5..4/8). Cabecera explicita y CRC activos, preambulo de 8 simbolos como RadioLib.
    double      loraTimeOnAirMs(int sf, long bandwidthHz, int cr4, int payloadBytes, int preambleSymbols = 8);
    // S-meter para VHF/UHF (>30 MHz), IARU Region 1 R.1: S9 = -93 dBm, 6 dB por punto; por encima, S9+dB
    void        sMeterText(int rssiDbm, char* out, size_t size);                      // "S7", "S9", "S9+12", "S0"
    double      loraBitrate(int sf, long bandwidthHz, int cr4);                       // bits/s utiles, calculado (no medido)

    // Tiempo en el aire acumulado y de la ultima hora (60 cubos de un minuto)
    class AirtimeTracker {
    public:
        void     clear();
        void     addTx(uint32_t nowMs, uint32_t airMs);
        void     addRx(uint32_t nowMs, uint32_t airMs);
        uint32_t txTotalMs() const { return txTotal_; }
        uint32_t rxTotalMs() const { return rxTotal_; }
        uint32_t txLastHourMs(uint32_t nowMs) const { return sum_(txBucket_, nowMs); }
        uint32_t rxLastHourMs(uint32_t nowMs) const { return sum_(rxBucket_, nowMs); }
        float    txDutyPercent(uint32_t nowMs) const;                                 // % de la ultima hora (3600 s)
    private:
        struct Bucket { uint32_t minute; uint32_t ms; bool used; };
        void     add_(Bucket* buckets, uint32_t nowMs, uint32_t airMs);
        uint32_t sum_(const Bucket* buckets, uint32_t nowMs) const;
        Bucket   txBucket_[60];
        Bucket   rxBucket_[60];
        uint32_t txTotal_ = 0;
        uint32_t rxTotal_ = 0;
    };

    // ---------- Formato de texto ----------
    void        formatAge(uint32_t ageMs, char* out, size_t size);                  // "12s", "5m", "2h", "3d"
    // ---------- Unidades y coordenadas ----------
    enum UnitSystem : uint8_t { UNITS_METRIC, UNITS_IMPERIAL, UNITS_NAUTICAL, UNITS_COUNT };
    enum CoordFormat : uint8_t { COORD_DD, COORD_DDM, COORD_DMS, COORD_COUNT };
    const char* unitSystemName(uint8_t units);                                        // "metric", "imperial", "nautical"
    const char* coordFormatName(uint8_t format);                                      // "DD", "DDM", "DMS"
    void        formatDistanceUnits(double km, uint8_t units, char* out, size_t size);  // 850m, 12.3km | 0.53mi, 450ft | 0.30nm
    void        formatSpeedUnits(double kmh, uint8_t units, char* out, size_t size);    // 46km/h | 29mph | 25kn
    void        formatAltitudeUnits(double meters, uint8_t units, char* out, size_t size);   // 41m | 135ft (nautico: m)
    double      altitudeValue(double meters, uint8_t units);
    const char* altitudeUnit(uint8_t units);
    double      speedValue(double kmh, uint8_t units);
    const char* speedUnit(uint8_t units);
    // Una coordenada con el hemisferio al final: 37.38912N | 37 23.347N | 37 23'20.8"N
    void        formatCoordinate(double degrees, bool isLatitude, uint8_t format, char* out, size_t size, int ddDecimals = 5);
    void        formatDistance(double km, char* out, size_t size);                  // "850m", "12.3km", "123km"
    void        formatUptime(uint32_t seconds, char* out, size_t size);             // "1d02h", "3h25m", "12m05s"
    const char* hdopQuality(float hdop);                                            // texto corto

}

#endif
