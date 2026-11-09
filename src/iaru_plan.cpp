/*
 * APRS LoRa+ - Regiones IARU: deteccion por posicion y planes de banda.
 * Copyright (C) 2026 Manuel - EA7JRS   (GPL-3.0-or-later)
 */
#include "iaru_plan.h"
#include "iaru_region_data.inc"      // GENERADO por tools/gen_iaru_region.py

namespace IARU_Plan {

    namespace {

        bool inside(const float (*poly)[2], int n, float x, float y) {
            bool c = false;
            for (int i = 0, j = n - 1; i < n; j = i++) {
                const float xi = poly[i][0], yi = poly[i][1], xj = poly[j][0], yj = poly[j][1];
                if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) c = !c;
            }
            return c;
        }

        // Canales (Hz) por tipo y region (copia de IARU_CHANNELS / _FSK / _CW / _CWB / _VHF de data_embed/script.js)
        const long LORA_1[] = {433625000, 433650000, 433675000, 433700000, 433725000, 433750000, 433775000,
                               434450000, 434475000, 434500000, 434525000, 434550000, 434575000, 434855000,
                               438025000, 438050000, 438075000, 438100000, 438125000, 438150000, 438175000, 439912500};
        const long LORA_2[] = {433775000};
        const long LORA_3[] = {433775000, 439100000, 439125000, 439150000};
        const long FSK_1[]  = {432500000, 433625000, 433650000, 433675000, 433700000, 433725000, 433750000, 433775000,
                               434450000, 434475000, 434500000, 434525000, 434550000, 434575000, 434855000,
                               438025000, 438050000, 438075000, 438100000, 438125000, 438150000, 438175000};
        const long CW_1[]   = {432025000, 432050000, 432075000, 432100000};
        const long CW_2[]   = {432025000, 432050000, 432100000, 902100000, 903100000};
        const long CW_3[]   = {432050000, 432100000};
        const long CWB_1[]  = {432410000, 432430000, 432450000, 432470000, 432025000, 432050000, 432075000, 432100000};
        const long CWB_2[]  = {432320000, 432350000, 432380000, 903050000, 432025000, 432050000, 432100000, 902100000, 903100000};
        const long CWB_3[]  = {432050000, 432100000};
        // APRS AFSK 1200 en 2 m (T-TWR, radio SA868), por pais: sigidwiki.com/wiki/APRS, VK2KFJ (Australia / Nueva Zelanda) y la
        // tabla japonesa de frecuencias APRS (144,660 MHz a 1200 baudios)
        const long VHF_1[]  = {144800000};
        const long VHF_2[]  = {144390000, 144930000, 145570000};
        const long VHF_3[]  = {144390000, 145175000, 144575000, 144640000, 144660000};
        // APRS AFSK 1200 en 70 cm (T-TWR de UHF): 432,500 (plan de 70 cm de la IARU R1, "APRS frequency"), 439,100 (plan de 70 cm del WIA,
        // Australia) y 431,040 (tabla japonesa de frecuencias APRS). La Region 2 no tiene una frecuencia comun: la frecuencia es propia
        const long UHF_1[]  = {432500000};
        const long UHF_3[]  = {439100000, 431040000};

        #define ARR(a) a, (int)(sizeof(a) / sizeof(a[0]))
        struct List { const long* hz; int n; };
        List listFor(Kind k, int region) {
            if (region < 1 || region > 3) region = 1;
            switch (k) {
                case FSK:  return region == 1 ? List{ARR(FSK_1)}  : (region == 2 ? List{ARR(LORA_2)} : List{ARR(LORA_3)});
                case CW:   return region == 1 ? List{ARR(CW_1)}   : (region == 2 ? List{ARR(CW_2)}   : List{ARR(CW_3)});
                case CWB:  return region == 1 ? List{ARR(CWB_1)}  : (region == 2 ? List{ARR(CWB_2)}  : List{ARR(CWB_3)});
                case VHF:  return region == 1 ? List{ARR(VHF_1)}  : (region == 2 ? List{ARR(VHF_2)}  : List{ARR(VHF_3)});
                case UHF:  return region == 1 ? List{ARR(UHF_1)}  : (region == 2 ? List{nullptr, 0}  : List{ARR(UHF_3)});
                default:   return region == 1 ? List{ARR(LORA_1)} : (region == 2 ? List{ARR(LORA_2)} : List{ARR(LORA_3)});
            }
        }
    }

    int regionAt(float lat, float lon) {
        while (lon > 180.0f)  lon -= 360.0f;
        while (lon < -180.0f) lon += 360.0f;
        const float x2 = lon > 165.0f ? lon - 360.0f : lon;
        if (inside(IARU_R2, IARU_R2_N, x2, lat)) return 2;
        const float x1 = (lon < -168.9f && lat >= 65.0f) ? lon + 360.0f : lon;
        return inside(IARU_R1, IARU_R1_N, x1, lat) ? 1 : 3;
    }

    bool allowed(int region, long hz) {
        if (region == 2) return (hz >= 144000000L && hz <= 148000000L) || (hz >= 420000000L && hz <= 450000000L) || (hz >= 902000000L && hz <= 928000000L);
        if (region == 3) return (hz >= 144000000L && hz <= 148000000L) || (hz >= 430000000L && hz <= 440000000L);
        return (hz >= 144000000L && hz <= 146000000L) || (hz >= 430000000L && hz <= 440000000L);    // region 1
    }

    int channelCount(Kind kind, int region)            { return listFor(kind, region).n; }
    long channelHz(Kind kind, int region, int index)   { const List l = listFor(kind, region); return (index >= 0 && index < l.n) ? l.hz[index] : 0; }

    long defaultHz(Kind kind, int region) {
        switch (kind) {
            case CW:  return 432050000L;                          // centro de actividad de telegrafia (existe en las tres regiones)
            case CWB: return region == 1 ? 432410000L : (region == 2 ? 432320000L : 432050000L);
            case VHF: return region == 1 ? 144800000L : 144390000L;   // R3: 144,390 (Indonesia, Malasia, Tailandia); Australia, Nueva Zelanda, China y Japon en la lista
            case UHF: return region == 1 ? 432500000L : (region == 3 ? 439100000L : 0L);
            default:  return 433775000L;                          // LoRa APRS (existe en las tres regiones, tambien en la lista FSK)
        }
    }

    bool isPreset(Kind kind, int region, long hz) {
        const List l = listFor(kind, region);
        for (int i = 0; i < l.n; i++) if (l.hz[i] == hz) return true;
        return false;
    }

    long mapFrequency(Kind kind, int fromRegion, int toRegion, long hz) {
        if (hz <= 0 || fromRegion == toRegion) return hz;
        const long def = defaultHz(kind, toRegion);
        if (def > 0 && isPreset(kind, fromRegion, hz)) return isPreset(kind, toRegion, hz) ? hz : def;
        if (allowed(toRegion, hz) || def == 0) return hz;           // Custom (o region sin lista): se respeta si cabe; sin lista no hay otra
        return def;
    }

}
