// Minimal baseline JPEG encoder to a memory buffer.
// Ported from jo_jpeg.cpp v1.52 by Jon Olick (https://www.jonolick.com), released to the PUBLIC DOMAIN.
// Adapted here to write into a caller-provided buffer (no stdio) for the SSDV mode. (EA7JRS)
#ifndef JO_JPEG_H_
#define JO_JPEG_H_

#include <stdint.h>
#include <string.h>
#include <math.h>

namespace jojpeg {

static inline int s_jo_zig(int i);   // zigzag (definida mas abajo)

struct Sink {
    uint8_t* buf; size_t cap; size_t len; bool ovf;
    void byte(uint8_t b) { if (len < cap) buf[len++] = b; else ovf = true; }
};

static void joBits(Sink& s, int& bitBuf, int& bitCnt, const unsigned short* bs) {
    bitCnt += bs[1];
    bitBuf |= bs[0] << (24 - bitCnt);
    while (bitCnt >= 8) {
        uint8_t c = (bitBuf >> 16) & 0xFF;
        s.byte(c);
        if (c == 0xFF) s.byte(0);
        bitBuf <<= 8;
        bitCnt -= 8;
    }
}

static void joDCT(float& d0, float& d1, float& d2, float& d3, float& d4, float& d5, float& d6, float& d7) {
    float tmp0 = d0 + d7, tmp7 = d0 - d7, tmp1 = d1 + d6, tmp6 = d1 - d6;
    float tmp2 = d2 + d5, tmp5 = d2 - d5, tmp3 = d3 + d4, tmp4 = d3 - d4;
    float tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3, tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
    d0 = tmp10 + tmp11; d4 = tmp10 - tmp11;
    float z1 = (tmp12 + tmp13) * 0.707106781f;
    d2 = tmp13 + z1; d6 = tmp13 - z1;
    tmp10 = tmp4 + tmp5; tmp11 = tmp5 + tmp6; tmp12 = tmp6 + tmp7;
    float z5 = (tmp10 - tmp12) * 0.382683433f;
    float z2 = tmp10 * 0.541196100f + z5, z4 = tmp12 * 1.306562965f + z5, z3 = tmp11 * 0.707106781f;
    float z11 = tmp7 + z3, z13 = tmp7 - z3;
    d5 = z13 + z2; d3 = z13 - z2; d1 = z11 + z4; d7 = z11 - z4;
}

static int joProcessDU(Sink& s, int& bitBuf, int& bitCnt, float* CDU, int du_stride, const float* fdtbl, int DC,
                       const unsigned short HTDC[256][2], const unsigned short HTAC[256][2]) {
    const unsigned short EOB[2] = { HTAC[0x00][0], HTAC[0x00][1] };
    const unsigned short M16zeroes[2] = { HTAC[0xF0][0], HTAC[0xF0][1] };
    for (int dataOff = 0; dataOff < 8; ++dataOff)
        joDCT(CDU[dataOff], CDU[dataOff + du_stride], CDU[dataOff + du_stride*2], CDU[dataOff + du_stride*3],
              CDU[dataOff + du_stride*4], CDU[dataOff + du_stride*5], CDU[dataOff + du_stride*6], CDU[dataOff + du_stride*7]);
    for (int dataOff = 0; dataOff < 8*du_stride; dataOff += du_stride)
        joDCT(CDU[dataOff], CDU[dataOff+1], CDU[dataOff+2], CDU[dataOff+3], CDU[dataOff+4], CDU[dataOff+5], CDU[dataOff+6], CDU[dataOff+7]);
    int DU[64];
    static const int s_jo_ZigZag[] = { 0,1,5,6,14,15,27,28,2,4,7,13,16,26,29,42,3,8,12,17,25,30,41,43,9,11,18,24,31,40,44,53,10,19,23,32,39,45,52,54,20,22,33,38,46,51,55,60,21,34,37,47,50,56,59,61,35,36,48,49,57,58,62,63 };
    for (int y = 0, j = 0; y < 8; ++y) for (int x = 0; x < 8; ++x, ++j) {
        float v = CDU[y*du_stride + x] * fdtbl[j];
        DU[s_jo_ZigZag[j]] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    }
    int diff = DU[0] - DC;
    if (diff == 0) joBits(s, bitBuf, bitCnt, HTDC[0]);
    else {
        unsigned short bits[2];
        int t = diff < 0 ? -diff : diff; int nbits = 0; int tt = t; while (tt) { nbits++; tt >>= 1; }
        if (diff < 0) diff--;
        bits[1] = nbits; bits[0] = diff & ((1 << nbits) - 1);
        joBits(s, bitBuf, bitCnt, HTDC[nbits]);
        joBits(s, bitBuf, bitCnt, bits);
    }
    int end0pos = 63; for (; end0pos > 0 && DU[end0pos] == 0; --end0pos) {}
    if (end0pos == 0) { joBits(s, bitBuf, bitCnt, EOB); return DU[0]; }
    for (int i = 1; i <= end0pos; ++i) {
        int startpos = i;
        for (; DU[i] == 0 && i <= end0pos; ++i) {}
        int nrzeroes = i - startpos;
        if (nrzeroes >= 16) { int lng = nrzeroes >> 4; for (int nrmarker = 1; nrmarker <= lng; ++nrmarker) joBits(s, bitBuf, bitCnt, M16zeroes); nrzeroes &= 15; }
        unsigned short bits[2];
        int v = DU[i]; int t = v < 0 ? -v : v; int nbits = 0; int tt = t; while (tt) { nbits++; tt >>= 1; }
        if (v < 0) v--;
        bits[1] = nbits; bits[0] = v & ((1 << nbits) - 1);
        joBits(s, bitBuf, bitCnt, HTAC[(nrzeroes << 4) + nbits]);
        joBits(s, bitBuf, bitCnt, bits);
    }
    if (end0pos != 63) joBits(s, bitBuf, bitCnt, EOB);
    return DU[0];
}

// Codifica RGB (comp=3) o gris (comp=1) a JPEG baseline en out (cap bytes). Devuelve bytes escritos, 0 si no cabe.
// quality 1..100. w y h cualesquiera (SSDV exige multiplos de 16; el que llama lo garantiza).
static size_t encode(uint8_t* out, size_t cap, const void* data, int width, int height, int comp, int quality) {
    static const unsigned char std_dc_luminance_nrcodes[] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
    static const unsigned char std_dc_luminance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static const unsigned char std_ac_luminance_nrcodes[] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
    static const unsigned char std_ac_luminance_values[] = {
        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
        0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
        0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
        0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
        0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
        0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
        0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa };
    static const unsigned char std_dc_chrominance_nrcodes[] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
    static const unsigned char std_dc_chrominance_values[] = {0,1,2,3,4,5,6,7,8,9,10,11};
    static const unsigned char std_ac_chrominance_nrcodes[] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
    static const unsigned char std_ac_chrominance_values[] = {
        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
        0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
        0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
        0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
        0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
        0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa };
    static const int YQT[] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
    static const int UVQT[] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
    static const float aasf[] = {1.0f*2.828427125f,1.387039845f*2.828427125f,1.306562965f*2.828427125f,1.175875602f*2.828427125f,1.0f*2.828427125f,0.785694958f*2.828427125f,0.541196100f*2.828427125f,0.275899379f*2.828427125f};

    if (!data || !out || width <= 0 || height <= 0 || (comp != 1 && comp != 3)) return 0;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

    // Codigos Huffman canonicos generados de las tablas estandar (nrcodes[1..16] + values) en vez de literales
    unsigned short YDC_HT[256][2] = {{0,0}}, UVDC_HT[256][2] = {{0,0}}, YAC_HT[256][2] = {{0,0}}, UVAC_HT[256][2] = {{0,0}};
    struct GenHT { static void gen(unsigned short HT[256][2], const unsigned char* nrcodes, const unsigned char* values) {
        int code = 0, k = 0;
        for (int len = 1; len <= 16; ++len) { for (int i = 0; i < nrcodes[len]; ++i) { HT[values[k]][0] = (unsigned short)code; HT[values[k]][1] = (unsigned short)len; ++code; ++k; } code <<= 1; }
    } };
    GenHT::gen(YDC_HT,  std_dc_luminance_nrcodes,   std_dc_luminance_values);
    GenHT::gen(UVDC_HT, std_dc_chrominance_nrcodes, std_dc_chrominance_values);
    GenHT::gen(YAC_HT,  std_ac_luminance_nrcodes,   std_ac_luminance_values);
    GenHT::gen(UVAC_HT, std_ac_chrominance_nrcodes, std_ac_chrominance_values);

    unsigned char YTable[64], UVTable[64];
    for (int i = 0; i < 64; ++i) {
        int yti = (YQT[i]*quality+50)/100; YTable[s_jo_zig(i)] = (unsigned char)(yti < 1 ? 1 : yti > 255 ? 255 : yti);
        int uvti = (UVQT[i]*quality+50)/100; UVTable[s_jo_zig(i)] = (unsigned char)(uvti < 1 ? 1 : uvti > 255 ? 255 : uvti);
    }
    float fdtbl_Y[64], fdtbl_UV[64];
    for (int row = 0, k = 0; row < 8; ++row) for (int col = 0; col < 8; ++col, ++k) {
        fdtbl_Y[k]  = 1 / (YTable [s_jo_zig(k)] * aasf[row] * aasf[col]);
        fdtbl_UV[k] = 1 / (UVTable[s_jo_zig(k)] * aasf[row] * aasf[col]);
    }

    Sink s { out, cap, 0, false };
    const unsigned char head0[] = { 0xFF,0xD8,0xFF,0xE0,0,0x10,'J','F','I','F',0,1,1,0,0,1,0,1,0,0,0xFF,0xDB,0,0x84,0 };
    for (size_t i = 0; i < sizeof(head0); ++i) s.byte(head0[i]);
    for (int i = 0; i < 64; ++i) s.byte(YTable[i]);
    s.byte(1); for (int i = 0; i < 64; ++i) s.byte(UVTable[i]);
    const unsigned char head1[] = { 0xFF,0xC0,0,0x11,8,(unsigned char)(height>>8),(unsigned char)(height&0xFF),(unsigned char)(width>>8),(unsigned char)(width&0xFF),
        3,1,0x11,0,2,0x11,1,3,0x11,1,0xFF,0xC4,0x01,0xA2,0 };
    for (size_t i = 0; i < sizeof(head1); ++i) s.byte(head1[i]);
    // DC/AC Huffman tables
    for (int i = 0; i < 16; ++i) s.byte(std_dc_luminance_nrcodes[i+1]);
    for (int i = 0; i <= 11; ++i) s.byte(std_dc_luminance_values[i]);
    s.byte(0x10); for (int i = 0; i < 16; ++i) s.byte(std_ac_luminance_nrcodes[i+1]);
    for (int i = 0; i <= 161; ++i) s.byte(std_ac_luminance_values[i]);
    s.byte(1); for (int i = 0; i < 16; ++i) s.byte(std_dc_chrominance_nrcodes[i+1]);
    for (int i = 0; i <= 11; ++i) s.byte(std_dc_chrominance_values[i]);
    s.byte(0x11); for (int i = 0; i < 16; ++i) s.byte(std_ac_chrominance_nrcodes[i+1]);
    for (int i = 0; i <= 161; ++i) s.byte(std_ac_chrominance_values[i]);
    const unsigned char head2[] = { 0xFF,0xDA,0,0x0C,3,1,0,2,0x11,3,0x11,0,0x3F,0 };
    for (size_t i = 0; i < sizeof(head2); ++i) s.byte(head2[i]);

    int bitBuf = 0, bitCnt = 0;
    int DCY = 0, DCU = 0, DCV = 0;
    const uint8_t* img = (const uint8_t*)data;
    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            float YDU[64], UDU[64], VDU[64];
            for (int row = y, pos = 0; row < y + 8; ++row) {
                int clampRow = row < height ? row : height - 1;
                for (int col = x; col < x + 8; ++col, ++pos) {
                    int clampCol = col < width ? col : width - 1;
                    int p = (clampRow * width + clampCol) * comp;
                    float r, g, b;
                    if (comp == 1) { r = g = b = img[p]; }
                    else { r = img[p]; g = img[p+1]; b = img[p+2]; }
                    YDU[pos] =  0.29900f*r + 0.58700f*g + 0.11400f*b - 128;
                    UDU[pos] = -0.16874f*r - 0.33126f*g + 0.50000f*b;
                    VDU[pos] =  0.50000f*r - 0.41869f*g - 0.08131f*b;
                }
            }
            DCY = joProcessDU(s, bitBuf, bitCnt, YDU, 8, fdtbl_Y, DCY, YDC_HT, YAC_HT);
            DCU = joProcessDU(s, bitBuf, bitCnt, UDU, 8, fdtbl_UV, DCU, UVDC_HT, UVAC_HT);
            DCV = joProcessDU(s, bitBuf, bitCnt, VDU, 8, fdtbl_UV, DCV, UVDC_HT, UVAC_HT);
        }
    }
    static const unsigned short fillBits[] = { 0x7F, 7 };
    joBits(s, bitBuf, bitCnt, fillBits);
    s.byte(0xFF); s.byte(0xD9);
    return s.ovf ? 0 : s.len;
}

// zigzag para las tablas de cuantificacion
static inline int s_jo_zig(int i) {
    static const int z[] = {0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
    return z[i];
}

}  // namespace jojpeg

#endif
