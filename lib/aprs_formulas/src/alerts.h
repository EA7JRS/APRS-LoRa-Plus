/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Plotter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// Avisos oficiales para todo el ecosistema APRS LoRa+ (plotter, Display, tracker, app Android):
// - meteorologicos: AEMET (RSS publico de Meteoalerta, sin clave), MeteoAlarm (canal Atom de cada pais, CC BY 4.0) y
//   NWS de EE. UU. (api.weather.gov, GeoJSON, por punto);
// - terremotos: USGS (GeoJSON, todo el mundo) y EMSC (FDSN, Europa y Mediterraneo, con los datos del IGN);
// - tsunamis: tsunami.gov (Atom del PTWC, PHEB, y del NTWC, PAAQ);
// - tiempo espacial: SWPC de la NOAA (escalas G / R / S, Kp, flujo de 10,7 cm, campo del viento solar, avisos);
// - NOAA Weather Radio (SAME): ver same.h.
// Cada aviso queda con un nivel comun 0 (informacion), 1 amarillo, 2 naranja y 3 rojo, y la lista los ordena, caduca
// y dice cuales son nuevos (para la ventana emergente y el zumbador).
#ifndef PLOTTER_ALERTS_H_
#define PLOTTER_ALERTS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "geo.h"
#include "minijson.h"

namespace plotter {

enum class AlertSrc : uint8_t { Aemet, MeteoAlarm, Nws, Tsunami, Swpc, Same, Usgs, Emsc, Other };
const char* alertSrcName(AlertSrc s);           // "AEMET", "MeteoAlarm", "NWS", "TSUNAMI", "SWPC", "SAME", "USGS", "EMSC"
const char* alertSrcCode(AlertSrc s);           // corto, para el protocolo: "aemet", "ma", "nws", "tsu", "swpc", "same"
AlertSrc alertSrcFromCode(const std::string& code);
const char* alertLevelName(uint8_t level);      // "INFO", "YELLOW", "ORANGE", "RED"

struct Alert {
    std::string id;                 // unico por aviso y zona
    AlertSrc src = AlertSrc::Other;
    uint8_t level = 0;              // 0 informacion, 1 amarillo, 2 naranja, 3 rojo
    std::string event;              // "Rain", "Tornado Warning", "Tsunami Warning", "G3 Geomagnetic storm"
    std::string area;               // zona, condado o region
    std::string text;               // detalle corto
    std::string instruction;        // que hacer (si lo trae)
    int64_t sent = 0, onset = 0, expires = 0;   // segundos Unix (0 = no se sabe)
    double lat = 1000, lon = 1000;  // posicion del aviso (epicentro de un tsunami); 1000 = sin posicion
    bool hasPos() const { return lat < 999 && lon < 999; }
    bool activeAt(int64_t now) const { return expires == 0 || now < expires; }
};

// ---- Utilidades de XML (canales Atom / RSS / CAP) ----
// Texto de la primera etiqueta "name" (con prefijo, "cap:event") dentro de "xml", sin espacios sobrantes, con las
// entidades y CDATA resueltas y sin etiquetas HTML dentro. "" si no esta.
std::string xmlText(const std::string& xml, const char* name, size_t from = 0);
// Bloques <tag ...>...</tag> de un documento (entradas de un Atom, items de un RSS)
std::vector<std::string> xmlBlocks(const std::string& doc, const char* tag);
// Separa bloques <tag>...</tag> de un documento que llega a trozos (descargas en los ESP32 sin guardarlo entero).
// Cada bloque completo (hasta maxBlock bytes; los mas largos se descartan) se pasa a "onBlock".
class XmlBlockSplitter {
public:
    XmlBlockSplitter(const char* tag, size_t maxBlock = 8192);
    template <typename F> void feed(const char* data, size_t len, F onBlock) {
        for (size_t i = 0; i < len; i++) if (push(data[i])) { onBlock(block_); block_.clear(); }
    }
private:
    bool push(char c);
    std::string open_, close_, block_, tail_;
    size_t max_;
    bool inside_ = false, overflow_ = false;
};

// Fecha ISO 8601 / RFC 3339 ("2026-11-04T23:00:00+00:00", "...Z", "...-06:00", sin segundos) a segundos Unix; 0 si no vale
int64_t parseIsoTime(const std::string& s);
int64_t unixFromUtc(int y, int mo, int d, int h, int mi, int s);

// Nivel comun desde la gravedad CAP (Extreme 3, Severe 2, Moderate 1, Minor / Unknown 0) y desde un color en el texto
// ("Yellow", "Orange", "Red", "amarillo", "naranja", "rojo"; -1 si no hay)
uint8_t alertLevelFromSeverity(const std::string& severity);
int alertLevelFromColour(const std::string& text);

// ---- Fuentes ----
// MeteoAlarm: una <entry> del canal Atom de un pais (una por aviso y zona). El nivel sale del color del titulo
// ("Orange Thunderstorm Warning issued for ..."), que no siempre coincide con cap:severity.
constexpr const char* kMeteoAlarmFeed = "https://feeds.meteoalarm.org/feeds/meteoalarm-legacy-atom-";   // + "spain", "germany"...
bool meteoAlarmParseEntry(const std::string& entry, Alert& a);
// Nombre del canal de MeteoAlarm para un pais (ISO 3166 alfa-2, "ES" -> "spain"); nullptr si no esta en MeteoAlarm
const char* meteoAlarmFeedName(const char* iso2);

// AEMET: un <item> del RSS de Meteoalerta ("Aviso. Nivel amarillo. Lluvias. Costa granadina"; las horas van en la
// descripcion con su desfase, "de 18:00 04-11-2026 CET (UTC+1) a 07:59 05-11-2026 CET (UTC+1)"). El primer item
// (el .tar.gz con todos los CAP) no es un aviso: false.
constexpr const char* kAemetRssPeninsula = "https://www.aemet.es/documentos_d/eltiempo/prediccion/avisos/rss/CAP_AFAE_RSS.xml";
constexpr const char* kAemetRssCanarias = "https://www.aemet.es/documentos_d/eltiempo/prediccion/avisos/rss/CAP_AFAC65_RSS.xml";
bool aemetParseRssItem(const std::string& item, Alert& a);
// Fenomeno de Meteoalerta en ingles ("Lluvias" -> "Rain"); el mismo texto si no lo conoce
std::string aemetPhenomenonEn(const std::string& es);

// CAP 1.2 (un aviso de AEMET del .tar.gz, un CAP de MeteoAlarm o de tsunami.gov): el <info> en "lang" (prefijo,
// "en"; si no esta, el primero) y, por cada <area>, su poligono. Un Alert por area.
bool capParse(const std::string& xml, std::vector<Alert>& out, std::vector<std::vector<LatLon>>* polygons = nullptr,
              const char* lang = "en");
// Poligono CAP ("lat,lon lat,lon ...") y punto dentro de un poligono
std::vector<LatLon> capPolygon(const std::string& text);
bool pointInPolygon(LatLon p, const std::vector<LatLon>& poly);

// NWS: respuesta GeoJSON de https://api.weather.gov/alerts/active?point=<lat>,<lon> (pide User-Agent con contacto)
constexpr const char* kNwsActive = "https://api.weather.gov/alerts/active?point=";
bool nwsParse(const std::string& json, std::vector<Alert>& out);

// tsunami.gov: una <entry> de PHEBAtom.xml (PTWC) o PAAQAtom.xml (NTWC). Nivel por la categoria: Warning 3,
// Advisory 2, Watch 1, Information / Threat message / Cancellation 0. Caduca a las 24 h del boletin.
constexpr const char* kTsunamiPtwc = "https://www.tsunami.gov/events/xml/PHEBAtom.xml";
constexpr const char* kTsunamiNtwc = "https://www.tsunami.gov/events/xml/PAAQAtom.xml";
bool tsunamiParseEntry(const std::string& entry, Alert& a);

// ---- Terremotos ----
// USGS: https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_day.geojson (M4,5+ de las ultimas 24 h)
// EMSC: FDSN de seismicportal.eu cerca de una posicion (emscUrl). Los dos dan el mismo terremoto: dedupe con
// quakeSame. El nivel depende de la magnitud y de la distancia (quakeLevel), y lo pone quakeSetLevels.
constexpr const char* kUsgs45Day = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_day.geojson";
std::string emscUrl(double lat, double lon, int64_t sinceUnix, double minMag = 3.5, double radiusDeg = 15);
bool quakeParseUsgs(const std::string& json, std::vector<Alert>& out);
bool quakeParseEmsc(const std::string& json, std::vector<Alert>& out);
// magnitud (en el campo "text" queda la profundidad); distancia en km. 3 rojo: M6 a < 300 km o M7 a < 1000 km;
// 2 naranja: M5 a < 300, M6 a < 1000, M7,5 a < 3000; 1 amarillo: M4 a < 100, M5 a < 600, M6 a < 2000; si no, 0.
uint8_t quakeLevel(double mag, double distKm);
double quakeMag(const Alert& a);                                // de "M5.0 Earthquake"
bool quakeSame(const Alert& a, const Alert& b);                 // mismo terremoto (< 60 km, < 90 s, < 0,6 de magnitud)
// Nivel y texto ("Depth 10 km · 245 km NE") de cada terremoto desde la posicion propia; quita los duplicados
void quakeSetLevels(std::vector<Alert>& quakes, LatLon here, bool haveHere);

// ---- Tiempo espacial (SWPC de la NOAA) ----
constexpr const char* kSwpcScales = "https://services.swpc.noaa.gov/products/noaa-scales.json";
constexpr const char* kSwpcKp = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json";
constexpr const char* kSwpcFlux = "https://services.swpc.noaa.gov/products/summary/10cm-flux.json";
constexpr const char* kSwpcMag = "https://services.swpc.noaa.gov/products/summary/solar-wind-mag-field.json";
constexpr const char* kSwpcWind = "https://services.swpc.noaa.gov/products/summary/solar-wind-speed.json";
constexpr const char* kSwpcAlerts = "https://services.swpc.noaa.gov/products/alerts.json";
constexpr const char* kSwpcXray = "https://services.swpc.noaa.gov/json/goes/primary/xrays-6-hour.json";

struct SpaceWx {
    int g = -1, r = -1, s = -1;     // escalas de hoy (0..5; -1 = sin dato)
    int gMax = -1;                  // mayor G prevista en los 3 dias siguientes
    float kp[8] = {};               // ultimos 8 valores de 3 h (24 h), el ultimo el mas reciente
    int kpCount = 0;
    int64_t kpTime = 0;             // hora del ultimo Kp
    float sfi = -1;                 // flujo de 10,7 cm (sfu)
    float bz = 1000, bt = -1;       // campo del viento solar (nT)
    float windKms = -1;             // velocidad del viento solar
    std::string xray;               // clase actual de rayos X ("M2.4")
    int64_t updated = 0;
    float kpNow() const { return kpCount ? kp[kpCount - 1] : -1; }
    bool valid() const { return g >= 0 || kpCount > 0 || sfi > 0; }
};
bool swpcParseScales(const std::string& json, SpaceWx& w);
bool swpcParseKp(const std::string& json, SpaceWx& w);          // los dos formatos (filas o objetos)
bool swpcParseSummary(const std::string& json, SpaceWx& w);     // 10cm-flux (Flux), mag-field (Bz, Bt), speed (WindSpeed)
bool swpcParseXray(const std::string& json, SpaceWx& w);        // ultimo dato de 0,1-0,8 nm
std::string xrayClass(double wattsM2);                          // 2.4e-5 -> "M2.4"
int kpToG(float kp);                                            // escala G de un Kp (5 -> G1 ... 9 -> G5)
// Avisos del SWPC (ALERT / WARNING / WATCH de las ultimas 48 h; los cancelados se quitan). Nivel por la escala:
// 1-2 amarillo, 3 naranja, 4-5 rojo; sin escala, informacion.
bool swpcParseAlerts(const std::string& json, int64_t now, std::vector<Alert>& out);
// Escalas de hoy como avisos (G, R, S >= 1)
void swpcScaleAlerts(const SpaceWx& w, int64_t now, std::vector<Alert>& out);
const char* swpcScaleName(char band);                           // 'G' "Geomagnetic storm", 'R' "Radio blackout", 'S' "Radiation storm"

// ---- Lista de avisos en vigor ----
class AlertList {
public:
    static constexpr size_t kMax = 24;
    // Anade o actualiza (por id). true si es nuevo o ha subido de nivel: hay que avisar.
    bool upsert(const Alert& a);
    // Pone la ultima descarga de una fuente: anade y actualiza los que trae y quita los de esa fuente que ya no vienen.
    // Devuelve los id de los nuevos o que han subido de nivel.
    std::vector<std::string> replaceSource(AlertSrc src, const std::vector<Alert>& fresh);
    void expire(int64_t now);
    bool remove(const std::string& id);
    void clear() { items_.clear(); }
    size_t size() const { return items_.size(); }
    const Alert& operator[](size_t i) const { return items_[i]; }
    const Alert* find(const std::string& id) const;
    int maxLevel() const;                       // -1 si no hay
    // Orden: mas grave primero, luego el que empieza antes
    void sort();
private:
    std::vector<Alert> items_;
};

// Filtro por zona para fuentes sin poligono (MeteoAlarm, RSS de AEMET): lista de textos separados por comas
// ("Huelva, Cadiz"); vale si alguno esta en el area (sin distinguir mayusculas ni acentos). Vacio = todas.
bool alertZoneMatch(const std::string& area, const std::string& zones);
// Texto en ASCII (sin acentos) para pantallas con fuente de 7 bits
std::string asciiFold(const std::string& utf8);

}  // namespace plotter
#endif
