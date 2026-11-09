// Frozen pre-stage-0 algorithms for output comparisons.
void referenceFFT(std::vector<std::complex<float>>& data) {
    int n = static_cast<int>(data.size());
    if (n <= 1) return;

    // Error if not power of 2
    if (!isPowerOfTwo(n)) {
        logError() << "FFT: size must be power of 2 (got " << n << ")";
        return;
    }

    int bits = internal::getBits(n);

    // Bit reverse permutation
    for (int i = 0; i < n; i++) {
        int j = internal::bitReverse(i, bits);
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    // Butterfly operation
    for (int len = 2; len <= n; len *= 2) {
        float angle = -TAU / len;
        std::complex<float> wn(std::cos(angle), std::sin(angle));

        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; j++) {
                std::complex<float> u = data[i + j];
                std::complex<float> t = w * data[i + j + len / 2];
                data[i + j] = u + t;
                data[i + j + len / 2] = u - t;
                w *= wn;
            }
        }
    }
}

template <class Out>
void referenceBezier(std::vector<Vec3> pts, float tolSq, int depth, Out& out) {
    int n = (int)pts.size();
    if (n < 2) return;

    // Flatness: max perpendicular distance of any interior control point
    // to the chord pts[0]--pts[n-1].
    float maxD = 0.0f;
    for (int i = 1; i < n - 1; i++) {
        float d = internal::pointLineDistSq(pts[i], pts.front(), pts.back());
        if (d > maxD) maxD = d;
    }
    if (depth >= kBezierMaxDepth || maxD <= tolSq) {
        out.push_back(pts.back());
        return;
    }
    // Split at t = 0.5 via de Casteljau: build the left/right control
    // polygons in two extra buffers.
    std::vector<Vec3> left, right;
    left.reserve(n);
    right.reserve(n);
    left.push_back(pts.front());
    right.push_back(pts.back());
    for (int level = 1; level < n; level++) {
        for (int i = 0; i < n - level; i++) {
            pts[i] = internal::lerp3(pts[i], pts[i + 1], 0.5f);
        }
        left.push_back(pts.front());
        right.push_back(pts[n - level - 1]);
    }
    // `right` was built end-first; reverse to get the right half's
    // control polygon in the conventional order.
    std::reverse(right.begin(), right.end());
    referenceBezier(std::move(left),  tolSq, depth + 1, out);
    referenceBezier(std::move(right), tolSq, depth + 1, out);
}

inline std::string referenceBase64(const unsigned char* bytes, size_t len) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(bytes++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                ret += internal::base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += internal::base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';
    }

    return ret;
}

