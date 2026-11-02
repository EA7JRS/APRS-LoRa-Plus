/* Copyright (C) 2026 Manuel - EA7JRS
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

#include "nmea_core.h"
#include "nmea2000.h"

namespace NMEA_Core {

    Engine::Engine() : sink_(nullptr), context_(nullptr), ais_(224), lastGnss_(0), lastZda_(0), lastEnvironment_(0),
                       startedGnss_(false), startedZda_(false), startedEnvironment_(false), sid_(0) {}

    void Engine::configure(const Options& options) {
        options_ = options;
        ais_     = NMEA0183::AisTable(options.mid);
    }

    void Engine::setSink(SinkFn sink, void* context) {
        sink_    = sink;
        context_ = context;
    }

    int Engine::stationCount() const {
        int count = 0;
        for (int i = 0; i < NMEA0183::AisTable::MAX_STATIONS; i++) {
            if (ais_.at(i).used) count++;
        }
        return count;
    }

    void Engine::emit(const char* text, size_t length) {
        if (sink_ && length > 0) sink_(text, length, context_);
    }

    void Engine::emitPgn(uint32_t nowMs, const NMEA0183::Gnss& own, uint32_t pgn, const uint8_t* data, size_t length) {
        char line[300];
        if (options_.seaSmart) {
            size_t n = NMEA2000::seaSmart(line, sizeof(line), pgn, nowMs, options_.n2kSource, data, length);
            emit(line, n);
        }
        if (options_.actisense) {
            int hh = own.hasTime ? own.hour : (int) ((nowMs / 3600000UL) % 24);
            int mm = own.hasTime ? own.minute : (int) ((nowMs / 60000UL) % 60);
            int ss = own.hasTime ? own.second : (int) ((nowMs / 1000UL) % 60);
            int ms = own.hasTime ? own.centis * 10 : (int) (nowMs % 1000UL);
            size_t n = NMEA2000::actisense(line, sizeof(line), hh, mm, ss, ms, options_.n2kSource, 0xFF, 6, pgn, data, length);
            emit(line, n);
        }
    }

    void Engine::emitOwnGnss(uint32_t nowMs, const NMEA0183::Gnss& g) {
        char line[200];
        emit(line, NMEA0183::rmc(g, line, sizeof(line)));
        emit(line, NMEA0183::gga(g, line, sizeof(line)));
        emit(line, NMEA0183::vtg(g, line, sizeof(line)));
        if (g.valid) {
            emit(line, NMEA0183::gll(g, line, sizeof(line)));
        }
        if (g.hasTime && g.hasDate && (!startedZda_ || (uint32_t) (nowMs - lastZda_) >= options_.zdaPeriodMs)) {
            emit(line, NMEA0183::zda(g, line, sizeof(line)));
            lastZda_ = nowMs;
            startedZda_ = true;
        }
        if (g.valid && (options_.seaSmart || options_.actisense)) {
            uint8_t data[64];
            sid_ = (uint8_t) ((sid_ + 1) % 253);
            size_t n = NMEA2000::pgn129025(data, sizeof(data), g.lat, g.lon);
            emitPgn(nowMs, g, NMEA2000::PGN_POSITION_RAPID, data, n);
            n = NMEA2000::pgn129026(data, sizeof(data), sid_, g.cogDegrees, g.sogKnots);
            emitPgn(nowMs, g, NMEA2000::PGN_COG_SOG_RAPID, data, n);
            if (g.hasTime && g.hasDate) {
                double seconds = g.hour * 3600.0 + g.minute * 60.0 + g.second + g.centis / 100.0;
                n = NMEA2000::pgn129029(data, sizeof(data), sid_, g.year, g.month, g.day, seconds, g.lat, g.lon, g.altitudeM, g.hasAltitude, g.satellites, g.hdop);
                emitPgn(nowMs, g, NMEA2000::PGN_GNSS_POSITION, data, n);
            }
        }
    }

    void Engine::emitEnvironment(uint32_t nowMs, const NMEA0183::Gnss& g, const NMEA0183::Environment& e) {
        char line[200];
        emit(line, NMEA0183::mda(e, line, sizeof(line)));
        emit(line, NMEA0183::xdr(e, line, sizeof(line)));
        if (options_.seaSmart || options_.actisense) {
            uint8_t data[16];
            size_t n = NMEA2000::pgn130311(data, sizeof(data), sid_, e.hasTemperature, e.temperatureC, e.hasHumidity, e.humidityPct, e.hasPressure, e.pressureHpa);
            emitPgn(nowMs, g, NMEA2000::PGN_ENVIRONMENT, data, n);
            if (e.hasTemperature) {
                n = NMEA2000::pgn130312(data, sizeof(data), sid_, 0, 1, e.temperatureC);
                emitPgn(nowMs, g, NMEA2000::PGN_TEMPERATURE, data, n);
            }
            if (e.hasHumidity) {
                n = NMEA2000::pgn130313(data, sizeof(data), sid_, 0, 1, e.humidityPct);
                emitPgn(nowMs, g, NMEA2000::PGN_HUMIDITY, data, n);
            }
            if (e.hasPressure) {
                n = NMEA2000::pgn130314(data, sizeof(data), sid_, 0, 0, e.pressureHpa);
                emitPgn(nowMs, g, NMEA2000::PGN_PRESSURE, data, n);
            }
        }
    }

    void Engine::emitAisPosition(uint32_t nowMs, int index, const NMEA0183::Gnss& own) {
        const NMEA0183::AisStation& s = ais_.at(index);
        char line[200];
        int second = own.hasTime ? own.second : -1;
        // Clase A: Mensaje Tipo 1 (emparejado con Mensaje Tipo 5 en emitAisStatic)
        if (s.hasInfo && s.info.aisClass == AIS_Vessel::CLASS_A) {
            uint8_t status = 15;
            bool moving = s.sogKnots >= 0.3;
            if (s.sogKnots >= 0) {
                if (!moving)                        status = 1;     // fondeado
                else if (s.info.shipType == 36)     status = 8;     // navegando a vela
                else                                status = 0;     // navegando a motor
            }
            double heading = (moving && s.cogDeg >= 0) ? s.cogDeg : -1.0;
            emit(line, NMEA0183::aisType1(s.mmsi, status, s.lat, s.lon, s.sogKnots, s.cogDeg, second, line, sizeof(line), heading));
        } else {
            // Clase B ("SO" o "CS") y estaciones APRS generales: Mensaje Tipo 18 (emparejado con Tipo 24 A/B)
            bool carrierSense = !(s.hasInfo && s.info.aisClass == AIS_Vessel::CLASS_B_SO);
            emit(line, NMEA0183::aisType18(s.mmsi, s.lat, s.lon, s.sogKnots, s.cogDeg, second, line, sizeof(line), carrierSense));
        }
        ais_.markEmitted(index, nowMs);
    }

    void Engine::emitAisStatic(uint32_t nowMs, int index) {
        const NMEA0183::AisStation& s = ais_.at(index);
        char line[260];
        if (s.hasInfo) {                                                    // datos reales: nombre, tipo, dimensiones e indicativo sin SSID
            char callsign[12];
            AIS_Vessel::baseCallsign(s.callsign, callsign, sizeof(callsign));
            uint16_t toBow, toStern;
            uint8_t  toPort, toStarboard;
            AIS_Vessel::splitDimensions(s.info.length, s.info.beam, toBow, toStern, toPort, toStarboard);
            const char* name = s.info.name[0] ? s.info.name : callsign;
            if (s.info.aisClass == AIS_Vessel::CLASS_A) {
                vdmSequence_ = (uint8_t) ((vdmSequence_ + 1) % 10);
                emit(line, NMEA0183::aisType5(s.mmsi, callsign, name, s.info.shipType, toBow, toStern, toPort, toStarboard, s.info.draughtDm, vdmSequence_, line, sizeof(line)));
            } else {
                emit(line, NMEA0183::aisType24A(s.mmsi, name, line, sizeof(line)));
                emit(line, NMEA0183::aisType24B(s.mmsi, s.info.shipType, callsign, toBow, toStern, toPort, toStarboard, line, sizeof(line)));
            }
            ais_.markStatic(index, nowMs);
            return;
        }
        emit(line, NMEA0183::aisType24A(s.mmsi, s.callsign, line, sizeof(line)));
        emit(line, NMEA0183::aisType24B(s.mmsi, 0, s.callsign, line, sizeof(line)));
        ais_.markStatic(index, nowMs);
    }

    void Engine::update(uint32_t nowMs, const NMEA0183::Gnss& own, const NMEA0183::Environment& environment) {
        // Emitir siempre a 1 Hz (con fix envia 'A' y coordenadas; sin fix envia 'V' y satelites=0 para que el software del PC detecte el receptor)
        if (options_.gnss && (!startedGnss_ || (uint32_t) (nowMs - lastGnss_) >= options_.gnssPeriodMs)) {
            lastGnss_    = nowMs;
            startedGnss_ = true;
            emitOwnGnss(nowMs, own);
        }
        bool hasEnvironment = environment.hasTemperature || environment.hasHumidity || environment.hasPressure;
        if (options_.environment && hasEnvironment && (!startedEnvironment_ || (uint32_t) (nowMs - lastEnvironment_) >= options_.environmentPeriodMs)) {
            lastEnvironment_    = nowMs;
            startedEnvironment_ = true;
            emitEnvironment(nowMs, own, environment);
        }
        if (options_.ais) {
            for (int burst = 0; burst < 4; burst++) {                  // como mucho 4 repeticiones por llamada
                int index = ais_.nextRefresh(nowMs, options_.aisRefreshMs, options_.aisTtlMs);
                if (index < 0) break;
                emitAisPosition(nowMs, index, own);
                if (ais_.staticDue(index, nowMs, options_.aisStaticPeriodMs)) emitAisStatic(nowMs, index);
            }
        }
    }

    void Engine::onAprsStation(uint32_t nowMs, const char* callsign, double lat, double lon, double sogKnots, double cogDeg, const NMEA0183::Gnss& own,
                               const AIS_Vessel::Info* info) {
        if (!options_.ais || callsign == nullptr || callsign[0] == 0) return;
        if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 || (lat == 0.0 && lon == 0.0)) return;
        bool isNew = false;
        int index = ais_.track(callsign, lat, lon, sogKnots, cogDeg, nowMs, isNew);
        if (info != nullptr) ais_.setInfo(index, *info);
        emitAisPosition(nowMs, index, own);
        if (isNew || ais_.staticDue(index, nowMs, options_.aisStaticPeriodMs)) emitAisStatic(nowMs, index);
    }

}