#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>

using namespace tc;

namespace {
int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
float decode(unsigned char v, int channels, int c) {
    float f = v / 255.0f;
    return channels >= 3 && c < 3 ? std::pow(f, 2.2f) : f;
}
unsigned char encode(float f, int channels, int c) {
    f = std::clamp(f, 0.0f, 1.0f);
    if (channels >= 3 && c < 3) f = std::pow(f, 1.0f / 2.2f);
    return static_cast<unsigned char>(std::lround(f * 255.0f));
}
float kernel(float d) {
    float a = std::abs(d);
    if (a <= 1.0f) return ((1.5f * a - 2.5f) * a) * a + 1.0f;
    if (a < 2.0f) return (((-0.5f * a) + 2.5f) * a - 4.0f) * a + 2.0f;
    return 0;
}
template<class Fetch>
float resample(int pos, int count, int destCount, Fetch fetch) {
    float scale = float(count) / float(destCount);
    float sum = 0;
    if (destCount < count) {
        float start = pos * scale, end = (pos + 1) * scale, total = 0;
        for (int i = int(std::floor(start)); i < std::min(int(std::ceil(end)), count); ++i) {
            float weight = std::min(end, float(i + 1)) - std::max(start, float(i));
            if (weight <= 0) continue;
            sum += fetch(i) * weight;
            total += weight;
        }
        return sum / total;
    }
    float center = (pos + 0.5f) * scale - 0.5f;
    int cf = int(std::floor(center));
    float t = center - float(cf);
    for (int k = -1; k <= 2; ++k)
        sum += fetch(std::clamp(cf + k, 0, count - 1)) * kernel(float(k) - t);
    return sum;
}
void testPixels() {
    bool identical = true;
    // Every U8 value paired with every other value, on all RGB/alpha channels.
    Pixels pairs;
    pairs.allocate(512, 512, 4);
    for (int a = 0; a < 256; ++a) for (int b = 0; b < 256; ++b)
        for (int y = 0; y < 2; ++y) for (int c = 0; c < 4; ++c) {
            size_t i = (size_t(2 * a + y) * 512 + 2 * b) * 4 + c;
            pairs.getData()[i] = static_cast<unsigned char>(a);
            pairs.getData()[i + 4] = static_cast<unsigned char>(b);
        }
    Pixels resized = pairs.clone();
    resized.resize(256,512);
    pairs.halve();
    bool resizePairsOK = true;
    for (int a = 0; a < 256; ++a) for (int b = 0; b < 256; ++b)
        for (int c = 0; c < 4; ++c) {
            float av = decode(a, 4, c), bv = decode(b, 4, c);
            auto expected = encode((av + bv + av + bv) * 0.25f, 4, c);
            for (int row=0;row<2;++row)
                resizePairsOK &= resized.getData()[(size_t(2*a+row)*256+b)*4+c] == encode((av+bv)/2.0f,4,c);
            identical &= pairs.getData()[(size_t(a) * 256 + b) * 4 + c] == expected;
        }
    check(identical, "halve: exhaustive U8 pairs equal old pow path");
    check(resizePairsOK, "resize: exhaustive U8 pairs equal one-axis old pow path");
    std::mt19937 random(1360);
    bool resizeOK = true, halveOK = true, floatOK = true;
    for (int ch = 1; ch <= 4; ++ch) {
        Pixels source;
        source.allocate(17, 13, ch);
        for (size_t i = 0; i < size_t(17 * 13 * ch); ++i) source.getData()[i] = random() & 255;
        Pixels half = source.clone(); half.halve();
        for (int y = 0; y < 6; ++y) for (int x = 0; x < 8; ++x) for (int c = 0; c < ch; ++c) {
            auto f = [&](int dx, int dy) { return decode(source.getData()[((y*2+dy)*17+x*2+dx)*ch+c],ch,c); };
            halveOK &= half.getData()[(y*8+x)*ch+c] == encode((f(0,0)+f(1,0)+f(0,1)+f(1,1))*0.25f,ch,c);
        }
        for (auto size : {std::pair{9, 7}, {31, 23}, {9, 23}, {31, 7}, {1, 1}, {17, 29}, {29, 13}}) {
            int dw = size.first, dh = size.second;
            std::vector<float> mid(size_t(dw * 13 * ch));
            for (int y = 0; y < 13; ++y) for (int x = 0; x < dw; ++x) for (int c = 0; c < ch; ++c)
                mid[(y*dw+x)*ch+c] = std::clamp(resample(x,17,dw,[&](int i) {return decode(source.getData()[(y*17+i)*ch+c],ch,c);}),0.0f,1.0f);
            Pixels actual = source.clone(); actual.resize(dw,dh);
            for (int y = 0; y < dh; ++y) for (int x = 0; x < dw; ++x) for (int c = 0; c < ch; ++c) {
                float f = resample(y,13,dh,[&](int i) {return mid[(i*dw+x)*ch+c];});
                resizeOK &= actual.getData()[(y*dw+x)*ch+c] == encode(f,ch,c);
            }
            Pixels linear; linear.allocate(17,13,ch,PixelFormat::F32);
            auto data = static_cast<float*>(linear.getDataVoid());
            for (int i = 0; i < 17*13*ch; ++i) data[i] = (float(source.getData()[i])-64.0f)/100.0f;
            for (int y = 0; y < 13; ++y) for (int x = 0; x < dw; ++x) for (int c = 0; c < ch; ++c)
                mid[(y*dw+x)*ch+c] = resample(x,17,dw,[&](int i) {return data[(y*17+i)*ch+c];});
            linear.resize(dw,dh); data=static_cast<float*>(linear.getDataVoid());
            for (int y = 0; y < dh; ++y) for (int x = 0; x < dw; ++x) for (int c = 0; c < ch; ++c) {
                float expected=resample(y,13,dh,[&](int i) {return mid[(i*dw+x)*ch+c];});
                floatOK &= std::memcmp(&expected, data+(y*dw+x)*ch+c, sizeof(float)) == 0;
            }
        }
    }
    check(halveOK,"halve: random RGB, alpha and linear masks equal old pow path");
    check(resizeOK,"resize: up/down/mixed/odd sizes equal pow with float intermediate");
    check(floatOK,"resize: F32 remains byte-identical, including values outside 0..1");
}
} // namespace

TC_CORE_TEST_MAIN() {
    testPixels();
    return failures ? 1 : 0;
}
