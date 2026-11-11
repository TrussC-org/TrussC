#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <thread>

using namespace tc;

namespace {
#include "reference.h"
int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
void testFFT() {
    bool realWindowOK = true, complexWindowOK = true, forwardOK = true, inverseOK = true;
    std::mt19937 random(1360);
    for (int n : {2, 16, 2048, 512, 2048}) {
        std::vector<std::complex<float>> input(n);
        for (auto& v : input) v = {float(int(random()%2001)-1000)/1000, float(int(random()%2001)-1000)/1000};
        for (auto type : {WindowType::Rect,WindowType::Hanning,WindowType::Hamming,WindowType::Blackman}) {
            std::vector<float> realSignal(n), realExpected(n);
            for (int i=0;i<n;++i) {
                realSignal[i]=input[i].real();
                realExpected[i]=realSignal[i]*windowFunction(type,i,n);
            }
            applyWindow(realSignal,type);
            realWindowOK &= std::memcmp(realSignal.data(),realExpected.data(),n*sizeof(float))==0;
            auto expected=input, actual=input;
            for (int i=0;i<n;++i) expected[i]*=windowFunction(type,i,n);
            applyWindow(actual,type);
            complexWindowOK &= std::memcmp(actual.data(),expected.data(),n*sizeof(actual[0]))==0;
            referenceFFT(expected); fft(actual);
            // The old loop can contract a complex multiply and butterfly add
            // into FMA differently from the cached loop. Allow a few float
            // rounding steps at the bin's scale, retaining the absolute floor
            // for bins near zero (where relative error is ill-conditioned).
            for (int i=0;i<n;++i) {
                float tolerance = 1e-5f + 4 * std::numeric_limits<float>::epsilon() * std::abs(expected[i]);
                bool matches = std::abs(actual[i]-expected[i]) <= tolerance;
                if (!matches && forwardOK) {
                    std::printf("FFT mismatch: n=%d window=%d bin=%d error=%g tolerance=%g\n",
                                n, static_cast<int>(type), i, std::abs(actual[i]-expected[i]), tolerance);
                }
                forwardOK &= matches;
            }
            ifft(actual);
            for (int i=0;i<n;++i) inverseOK &= std::abs(actual[i]-input[i]*windowFunction(type,i,n)) < 1e-4f;
        }
    }
    check(realWindowOK,"Window: byte-identical real samples across cache size changes");
    check(complexWindowOK,"Window: byte-identical complex samples across cache size changes");
    check(forwardOK,"FFT: old path within absolute and relative float tolerance");
    check(inverseOK,"FFT: inverse across cache size changes");
    bool threadOK[2] = {false,false};
    std::thread a([&] { std::vector<std::complex<float>> v(2048,{1,0}); fft(v); threadOK[0]=v[0].real()==2048; });
    std::thread b([&] { std::vector<std::complex<float>> v(512,{1,0}); fft(v); threadOK[1]=v[0].real()==512; });
    a.join(); b.join(); check(threadOK[0]&&threadOK[1],"FFT: independent thread caches");
}
void testBezier() {
    std::mt19937 random(1360);
    bool ok = true;
    for (int n : {2,3,4,6,16,64}) for (int trial=0;trial<10;++trial) {
        std::vector<Vec3> pts(n),expected,actual;
        for(auto& p:pts) p={float(random()%800),float(random()%600),float(random()%40)};
        float tol = trial%2 ? 0.1f : 2.0f;
        expected.push_back(pts.front()); referenceBezier(pts,tol*tol,0,expected);
        tessellateBezierN(pts,tol,actual);
        ok &= actual.size()==expected.size() && std::memcmp(actual.data(),expected.data(),actual.size()*sizeof(Vec3))==0;
    }
    check(ok,"Bezier: byte-identical vertices at orders 1..63");
}
void testBase64() {
    std::mt19937 random(1360);
    bool ok=true;
    const std::string alphabet=internal::base64_chars;
    for (size_t len=0;len<=4096;++len) {
        std::vector<unsigned char> input(len),decoded;
        for(auto& b:input) b=random()&255;
        std::string encoded=toBase64(input);
        ok &= encoded==referenceBase64(input.data(),len);
        unsigned int bits=0; int count=0;
        for(char c:encoded) {
            if(c=='=') break;
            bits=(bits<<6)|static_cast<unsigned int>(alphabet.find(c)); count+=6;
            if(count>=8) {count-=8;decoded.push_back(static_cast<unsigned char>(bits>>count));}
        }
        ok &= decoded==input;
        ok &= fromBase64(encoded)==input;
    }
    check(ok,"Base64: every size 0..4096 matches old encoder and round-trips");
}
} // namespace

TC_CORE_TEST_MAIN() {
    testFFT(); testBezier(); testBase64();
    return failures ? 1 : 0;
}
