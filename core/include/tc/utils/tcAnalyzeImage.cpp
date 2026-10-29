#include <TrussC.h>
#include "tcAnalyzeImage.h"
#include "../../impl/tcMcpImageAdmission.h"
#include "../../impl/tcImageDecode.h"
#include <array>
#include <climits>
#include <stdexcept>
#include <string_view>

namespace trussc::mcp::internal {
namespace {
// MCP retains its existing header policy independently of Pixels loading.
LoadResult admitMcpImageHeader(const unsigned char* bytes, size_t length) {
    // PIC info also checks a pixel product before returning dimensions.
    // Validate its fixed header and bounded packet table before admission.
    if (length >= 108 && !std::memcmp(bytes, "\x53\x80\xf6\x34", 4) &&
        !std::memcmp(bytes + 88, "PICT", 4)) {
        // Bound (abi): PIC metadata must fit stb's ten packet descriptors. Overflow returns DecodeFailed.
        for (size_t packet = 0; packet < 10; ++packet) {
            const size_t at = 104 + packet * 4;
            if (at + 4 > length || bytes[at] > 1 || bytes[at + 1] != 8 ||
                bytes[at + 2] > 2 || !(bytes[at + 3] & 0xf0) || (bytes[at + 3] & 0x0f))
                return LoadResult::fail(LoadError::DecodeFailed, "invalid PIC header");
            if (!bytes[at]) return trussc::internal::admitImageDimensions((unsigned(bytes[92]) << 8) | bytes[93],
                (unsigned(bytes[94]) << 8) | bytes[95], 4, 1);
        }
        return LoadResult::fail(LoadError::DecodeFailed, "invalid PIC packets");
    }
    // stb narrows HDR's original strtol fields to int. Validate the text
    // before either its info or float decoder can accept a wrapped canvas.
    if ((length >= 11 && !std::memcmp(bytes, "#?RADIANCE\n", 11)) ||
        (length >= 7 && !std::memcmp(bytes, "#?RGBE\n", 7))) {
        size_t at = 0;
        auto line = [&](std::string_view& value) {
            const size_t start = at;
            while (at < length && bytes[at] != '\n') {
                // stb's C-string tokens end at NUL, so it must not select a different header/resolution.
                if (bytes[at] == 0) return false;
                ++at;
            }
            if (at == length) return false;
            value = std::string_view(reinterpret_cast<const char*>(bytes + start), at - start);
            ++at;
            return true;
        };
        auto invalid = [] { return LoadResult::fail(LoadError::DecodeFailed, "invalid HDR resolution"); };
        std::string_view text;
        bool format = false;
        if (!line(text)) return invalid();
        for (;;) {
            if (!line(text)) return invalid();
            if (text.empty()) break;
            if (text == "FORMAT=32-bit_rle_rgbe") format = true;
        }
        // Bound (abi): HDR resolution must fit stb's 1024-byte token. Overflow returns DecodeFailed.
        if (!text.empty() || !format || !line(text) || text.size() >= 1024 || text.substr(0, 3) != "-Y ")
            return invalid();
        text.remove_prefix(3);
        auto spaces = [&] {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                   text.front() == '\v' || text.front() == '\f')) text.remove_prefix(1);
        };
        auto number = [&](int& value) {
            spaces();
            if (!text.empty() && text.front() == '+') text.remove_prefix(1);
            value = 0;
            bool digits = false;
            while (!text.empty() && text.front() >= '0' && text.front() <= '9') {
                const int digit = text.front() - '0';
                // Bound (abi): original HDR fields must fit positive int before narrowing. Overflow returns DecodeFailed.
                if (value > (INT32_MAX - digit) / 10) return false;
                value = value * 10 + digit;
                digits = true;
                text.remove_prefix(1);
            }
            return digits && value > 0;
        };
        int width = 0, height = 0;
        if (!number(height)) return invalid();
        // Match stb's supported -Y / +X orientation and separator.
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        if (text.substr(0, 3) != "+X ") return invalid();
        text.remove_prefix(3);
        if (!number(width)) return invalid();
        spaces();
        if (!text.empty()) return invalid();
        return trussc::internal::admitImageDimensions(width, height, 4, sizeof(float));
    }
    // PNG's info parser applies its own axis/product limits inside IHDR.
    // Walk supported prefix chunks with checked spans before inspecting IHDR.
    static constexpr unsigned char signature[]{137,80,78,71,13,10,26,10};
    if (length < 8 || std::memcmp(bytes, signature, 8)) return LoadResult::success();
    auto be32 = [&](size_t at) {
        return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
               (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
    };
    size_t chunk = 8;
    for (;;) {
        if (length - chunk < 12 || be32(chunk) > length - chunk - 12)
            return LoadResult::fail(LoadError::DecodeFailed, "invalid PNG chunk span");
        const size_t size = be32(chunk);
        if (!std::memcmp(bytes + chunk + 4, "IHDR", 4)) {
            if (size != 13) return LoadResult::fail(LoadError::DecodeFailed, "invalid PNG IHDR size");
            break;
        }
        if (std::memcmp(bytes + chunk + 4, "CgBI", 4))
            return LoadResult::fail(LoadError::DecodeFailed, "invalid PNG prefix chunk");
        uint32_t crc = 0xffffffffu;
        for (size_t i = chunk + 4; i < chunk + 8 + size; ++i) {
            crc ^= bytes[i];
            for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        if (~crc != be32(chunk + 8 + size))
            return LoadResult::fail(LoadError::DecodeFailed, "invalid PNG prefix CRC");
        chunk += size + 12;
    }
    const auto width = be32(chunk + 8), height = be32(chunk + 12);
    const unsigned depth = bytes[chunk + 16], color = bytes[chunk + 17];
    const bool validDepth = (color == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
        (color == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
        ((color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16));
    uint32_t crc = 0xffffffffu;
    for (size_t i = chunk + 4; i < chunk + 21; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    if (!width || !height || width > INT32_MAX || height > INT32_MAX || !validDepth ||
        bytes[chunk + 18] || bytes[chunk + 19] || bytes[chunk + 20] > 1 || ~crc != be32(chunk + 21))
        return LoadResult::fail(LoadError::DecodeFailed, "invalid PNG IHDR");
    return trussc::internal::admitImageDimensions(width, height, 4, depth == 16 ? 2 : 1);
}
using Channels = std::array<double, 4>;
struct Rect { int x, y, w, h; };
int integer(const json& j) {
    if (!j.is_number_integer()) throw std::runtime_error("coordinates and sizes must be integers");
    const auto n = j.get<int64_t>();
    // Bound (abi): external MCP coordinates and sizes must fit int indices. Overflow throws an error.
    if (n < INT_MIN || n > INT_MAX) {
        admissionRejected(AdmissionStore::Operations, INT_MAX, static_cast<size_t>(n));
        throw std::runtime_error("integer out of range");
    }
    return static_cast<int>(n);
}
double number(const json& j) {
    if (!j.is_number()) throw std::runtime_error("expected a number");
    double n = j.get<double>();
    if (!std::isfinite(n)) throw std::runtime_error("expected a finite number");
    return n;
}
Rect rectangle(const Pixels& p, const json& op) {
    Rect r{0, 0, p.getWidth(), p.getHeight()};
    if (op.contains("rect")) {
        const auto& a = op.at("rect");
        if (!a.is_array() || a.size() != 4) throw std::runtime_error("rect must be [x,y,w,h]");
        r = {integer(a[0]), integer(a[1]), integer(a[2]), integer(a[3])};
    }
    if (r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0 ||
        int64_t(r.x) + r.w > p.getWidth() || int64_t(r.y) + r.h > p.getHeight())
        throw std::runtime_error("rect must be nonempty and inside the image");
    return r;
}
bool inside(int x, int y, Rect r) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
Channels color(const Pixels& p, int x, int y) {
    const int ch = p.getChannels();
    const size_t i = trussc::internal::pixelOffset(x, y, p.getWidth(), ch);
    auto at = [&](int c) -> double {
        double v = p.isFloat() ? double(p.getDataF32()[i + c]) : double(p.getData()[i + c]) / 255.0;
        if (!std::isfinite(v)) throw std::runtime_error("image contains non-finite values");
        return v;
    };
    // CPU two-channel pixels are gray + alpha. Float Fbo RG is expanded on capture.
    return ch <= 2 ? Channels{at(0), at(0), at(0), ch == 2 ? at(1) : 1.0}
                   : Channels{at(0), at(1), at(2), ch == 4 ? at(3) : 1.0};
}
std::vector<double> inputColor(const json& j) {
    if (!j.is_array() || (j.size() != 3 && j.size() != 4))
        throw std::runtime_error("color bounds must have 3 or 4 channels");
    std::vector<double> v;
    for (const auto& n : j) v.push_back(number(n));
    return v;
}
struct Matches {
    uint64_t count = 0;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = 0, y1 = 0;
    long double sx = 0, sy = 0;
    void add(int x, int y) {
        ++count; x0 = std::min(x0, x); y0 = std::min(y0, y);
        x1 = std::max(x1, x); y1 = std::max(y1, y); sx += x; sy += y;
    }
    json bbox() const { return count ? json::array({x0,y0,x1-x0+1,y1-y0+1}) : json(nullptr); }
};
Channels mean(const Pixels& p, Rect r) {
    Channels sum{};
    for (int y = r.y; y < r.y+r.h; ++y)
        for (int x = r.x; x < r.x+r.w; ++x) {
            auto c = color(p,x,y);
            for (int k = 0; k < 4; ++k) sum[k] += c[k];
        }
    for (auto& c : sum) c /= double(r.w) * r.h;
    return sum;
}
void reservePixels(const Pixels& p, size_t bytesPerPixel = 8) {
    const auto limit = getAdmissionLimits().workingBytes;
    // Bound (security): analysis image copies and encoders need credits before allocation. Overflow returns an error and is counted.
    if (p.getWidth() <= 0 || p.getHeight() <= 0 || size_t(p.getWidth()) > limit / size_t(p.getHeight()) / bytesPerPixel) {
        const uint64_t area = uint64_t(std::max(0, p.getWidth())) * std::max(0, p.getHeight());
        admissionRejected(AdmissionStore::Working, limit, area > SIZE_MAX / bytesPerPixel ? SIZE_MAX : size_t(area) * bytesPerPixel);
        throw AdmissionError();
    }
    reserveWork(size_t(p.getWidth()) * p.getHeight() * bytesPerPixel);
}
void save(const Pixels& p, const json& path) {
    // Reuse both the path resolver and native screenshot encoders (including
    // TIFF/GIF on macOS and TGA on Windows). The writer expects RGBA8.
    auto dest = trussc::internal::resolveScreenshotPath(
        trussc::internal::utf8ToPath(path.get<std::string>()));
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) throw std::runtime_error("failed to create image parent directory");
    Pixels u8;
    reservePixels(p, 64);
    u8.allocate(p.getWidth(), p.getHeight(), 4);
    for (int y = 0; y < p.getHeight(); ++y) for (int x = 0; x < p.getWidth(); ++x) {
        auto c = color(p, x, y);
        auto i = trussc::internal::pixelOffset(x, y, p.getWidth(), 4);
        for (int k = 0; k < 4; ++k)
            u8.getData()[i+k] = static_cast<unsigned char>(std::lround(std::clamp(c[k], 0.0, 1.0)*255));
    }
    if (!trussc::internal::saveScreenshotPixels(u8, dest)) throw std::runtime_error("failed to save image");
}
json operation(const Pixels& p, const json& op) {
    const auto name = op.at("op").get<std::string>();
    const Rect r = rectangle(p,op);
    json out;
    if (name == "pixel") {
        int x = integer(op.at("x")), y = integer(op.at("y"));
        if (!inside(x,y,r)) throw std::runtime_error("pixel is outside rect");
        out["color"] = color(p,x,y);
    } else if (name == "stats") {
        Channels lo, hi;
        lo.fill(std::numeric_limits<double>::infinity()); hi.fill(-std::numeric_limits<double>::infinity());
        for (int y=r.y;y<r.y+r.h;++y) for (int x=r.x;x<r.x+r.w;++x) {
            auto c=color(p,x,y);
            for (int k=0;k<4;++k) { lo[k]=std::min(lo[k],c[k]); hi[k]=std::max(hi[k],c[k]); }
        }
        out={{"mean",mean(p,r)},{"min",lo},{"max",hi}};
    } else if (name == "histogram") {
        int bins=integer(op.at("bins"));
        if (bins<=0) throw std::runtime_error("bins must be positive");
        std::array<std::vector<uint64_t>,4> hist;
        for (auto& h:hist) h.resize(bins);
        for (int y=r.y;y<r.y+r.h;++y) for (int x=r.x;x<r.x+r.w;++x) {
            auto c=color(p,x,y);
            for (int k=0;k<4;++k) {
                int b=static_cast<int>(std::min(std::floor(std::clamp(c[k],0.0,1.0)*bins),double(bins-1)));
                ++hist[k][b];
            }
        }
        out["histogram"]=hist;
    } else if (name == "count") {
        std::vector<double> lo,hi;
        if (op.contains("color")) {
            if (op.contains("min") || op.contains("max")) throw std::runtime_error("choose color+tolerance or min+max");
            lo=hi=inputColor(op.at("color")); double t=number(op.at("tolerance"));
            if (t<0) throw std::runtime_error("tolerance must be nonnegative");
            for (size_t k=0;k<lo.size();++k) {lo[k]-=t;hi[k]+=t;}
        } else { lo=inputColor(op.at("min")); hi=inputColor(op.at("max")); }
        if (lo.size()!=hi.size()) throw std::runtime_error("bounds must have the same channel count");
        for (size_t k=0;k<lo.size();++k) if (lo[k]>hi[k]) throw std::runtime_error("min exceeds max");
        Matches m;
        for (int y=r.y;y<r.y+r.h;++y) for (int x=r.x;x<r.x+r.w;++x) {
            auto c=color(p,x,y); bool match=true;
            for (size_t k=0;k<lo.size();++k) match &= c[k]>=lo[k] && c[k]<=hi[k];
            if (match) m.add(x,y);
        }
        out={{"count",m.count},{"bbox",m.bbox()},{"centroid",m.count ? json::array({double(m.sx/m.count),double(m.sy/m.count)}) : json(nullptr)}};
    } else if (name == "grid") {
        int cols=integer(op.at("cols")),rows=integer(op.at("rows"));
        if (cols<=0 || rows<=0 || cols>r.w || rows>r.h) throw std::runtime_error("grid cells must be nonempty");
        json cells=json::array();
        for (int y=0;y<rows;++y) {
            json row=json::array();
            int y0=r.y+int(int64_t(y)*r.h/rows),y1=r.y+int(int64_t(y+1)*r.h/rows);
            for (int x=0;x<cols;++x) {
                int x0=r.x+int(int64_t(x)*r.w/cols),x1=r.x+int(int64_t(x+1)*r.w/cols);
                row.push_back(mean(p,{x0,y0,x1-x0,y1-y0}));
            }
            cells.push_back(std::move(row));
        }
        out["colors"]=std::move(cells);
    } else if (name == "line") {
        int x=integer(op.at("x0")),y=integer(op.at("y0")),x1=integer(op.at("x1")),y1=integer(op.at("y1"));
        Rect all{0,0,p.getWidth(),p.getHeight()};
        if (!inside(x,y,all)||!inside(x1,y1,all)) throw std::runtime_error("line endpoints must be inside image");
        int64_t dx=std::abs(int64_t(x1)-x),dy=-std::abs(int64_t(y1)-y),err=dx+dy;
        int sx=x<x1?1:-1,sy=y<y1?1:-1;
        json colors=json::array();
        for (;;) {
            if (inside(x,y,r)) colors.push_back(color(p,x,y));
            if (x==x1 && y==y1) break;
            int64_t e=2*err;
            if (e>=dy) {err+=dy;x+=sx;}
            if (e<=dx) {err+=dx;y+=sy;}
        }
        out["colors"]=std::move(colors);
    } else if (name == "diff") {
        Pixels ref;
        if (!loadMcpImage(ref, trussc::internal::utf8ToPath(op.at("path").get<std::string>()))) throw std::runtime_error("failed to load reference image");
        if (ref.getWidth()!=p.getWidth() || ref.getHeight()!=p.getHeight()) throw std::runtime_error("reference dimensions differ");
        double threshold=number(op.at("threshold"));
        if (threshold<0) throw std::runtime_error("threshold must be nonnegative");
        Pixels diff;
        bool write=op.contains("save")&&!op.at("save").is_null();
        if (write) { reservePixels(p); diff.allocate(p.getWidth(),p.getHeight(),4); }
        Matches m; double largest=0;
        for (int y=r.y;y<r.y+r.h;++y) for (int x=r.x;x<r.x+r.w;++x) {
            auto a=color(p,x,y),b=color(ref,x,y); double d=0;
            for (int k=0;k<4;++k) {
                double v=std::abs(a[k]-b[k]); d=std::max(d,v);
                if (write) diff.getData()[trussc::internal::pixelOffset(x,y,p.getWidth(),4)+k]=static_cast<unsigned char>(std::lround(std::clamp(v,0.0,1.0)*255));
            }
            if (write) diff.getData()[trussc::internal::pixelOffset(x,y,p.getWidth(),4)+3]=255;
            largest=std::max(largest,d); if(d>threshold)m.add(x,y);
        }
        if(write) save(diff,op.at("save"));
        out={{"count",m.count},{"bbox",m.bbox()},{"maxDifference",largest}};
    } else throw std::runtime_error("unknown image op: "+name);
    out["op"]=name;
    return out;
}
}
size_t reserveImageDecode(int width, int height, size_t sourceBytesPerPixel) {
    const auto limit = getAdmissionLimits().workingBytes;
    checkAdmission(sourceBytesPerPixel, limit > 4 ? (limit - 4) / 4 : 0, AdmissionStore::Working);
    const size_t bytesPerPixel = std::max<size_t>(64, sourceBytesPerPixel * 4 + 4);
    // Include codec block padding, including narrow images with padded planes.
    const uint64_t paddedWidth = (uint64_t(width) + 31) & ~uint64_t(31);
    const uint64_t paddedHeight = (uint64_t(height) + 31) & ~uint64_t(31);
    const uint64_t workspacePixels = paddedWidth * paddedHeight;
    // Bound (security): decoded channels, retained RGBA and codec workspace
    // must fit the budget before either stb or a platform decoder allocates.
    if (workspacePixels > limit / bytesPerPixel) {
        admissionRejected(AdmissionStore::Working, limit,
            workspacePixels > SIZE_MAX / bytesPerPixel ? SIZE_MAX : size_t(workspacePixels) * bytesPerPixel);
        throw AdmissionError();
    }
    const size_t bytes = size_t(workspacePixels) * bytesPerPixel;
    reserveWork(bytes);
    return bytes;
}
bool loadMcpImage(Pixels& pixels, const fs::path& source) try {
    WorkScope workScope;
    trussc::internal::ImageDecodeAllocationScope failure;
    reserveWork(1024 * 1024); // Fixed metadata, palette and codec-table allowance.
    const auto path = trussc::internal::resolveLoadPath(source);
    std::error_code ec;
    const auto length = fs::file_size(path, ec);
    if (ec || !length) return false;
    const auto imageSize = trussc::internal::admitImageEncodedSize(length > SIZE_MAX ? SIZE_MAX : size_t(length));
    if (!imageSize) {
        return false;
    }
    const auto limits = getAdmissionLimits();
    // Bound (abi): stb's immutable input view uses an int byte length. Overflow rejects the image and is counted.
    checkAdmission(length > SIZE_MAX ? SIZE_MAX : size_t(length), size_t(INT_MAX), AdmissionStore::Working);
    // Bound (security): encoded copies and decoder buffers share working credits. Overflow rejects the image before reading and is counted.
    if (length > limits.workingBytes / 4) {
        admissionRejected(AdmissionStore::Working, limits.workingBytes, length > SIZE_MAX / 4 ? SIZE_MAX : size_t(length) * 4);
        throw AdmissionError();
    }
    reserveWork(size_t(length) * 4);
    LoadResult entry;
    auto closeFile = [](FILE* handle) { std::fclose(handle); };
    std::unique_ptr<FILE, decltype(closeFile)> file(
        trussc::internal::openLoadFile(path, "MCP", entry), closeFile);
    if (!file) return false;
    std::unique_ptr<unsigned char[]> encoded(trussc::internal::imageDecodeAllocationAllowed(size_t(length))
        ? new (std::nothrow) unsigned char[size_t(length)] : nullptr);
    if (!encoded) { trussc::internal::imageDecodeOutOfMemory(); return false; }
    if (std::fread(encoded.get(), 1, size_t(length), file.get()) != size_t(length) ||
        std::fgetc(file.get()) != EOF || std::ferror(file.get())) return false;
    if (!admitMcpImageHeader(encoded.get(), size_t(length))) return false;
    int width = 0, height = 0, channels = 0;
    size_t denied = 0;
    bool metadata = false;
    {
        ImageAllocationScope allocation(1024 * 1024);
        metadata = stbi_info_from_memory(encoded.get(), int(size_t(length)), &width, &height, &channels) != 0;
        if (allocation.exceeded()) denied = allocation.needed();
    }
    if (denied) { admissionRejected(AdmissionStore::Working, 1024 * 1024, denied); throw AdmissionError(); }
    if (failure.failed()) {
        trussc::internal::imageDecodeOutOfMemory();
        return false;
    }
    auto platformDecode = [&] {
#ifdef __APPLE__
        return loadMcpPlatformImage(pixels, encoded.get(), size_t(length));
#else
        // Pixels::loadPlatform is a stub here (including Windows and Android).
        return false;
#endif
    };
    if (!metadata || width <= 0 || height <= 0) return platformDecode();
    const auto imageDimensions = trussc::internal::admitImageDimensions(width, height, 4,
        stbi_is_hdr_from_memory(encoded.get(), int(size_t(length))) ? sizeof(float) : 2);
    if (!imageDimensions) {
        return false;
    }
    const uint64_t count = uint64_t(width) * height;
    const size_t workspace = reserveImageDecode(width, height);
    const size_t decoderAllowance = 1024 * 1024 + size_t(length) * 3 + workspace - size_t(count) * 4;
    bool loaded = false;
    {
        ImageAllocationScope allocation(decoderAllowance);
        int decodedWidth = 0, decodedHeight = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
            stbi_load_from_memory(encoded.get(), int(size_t(length)), &decodedWidth, &decodedHeight, &channels, 4),
            &stbi_image_free);
        if (decoded && decodedWidth == width && decodedHeight == height && !allocation.exceeded()) {
            if (!trussc::internal::ImageLoadAccess::allocate(pixels, width, height)) {
                trussc::internal::imageDecodeOutOfMemory();
                return false;
            }
            std::memcpy(pixels.getData(), decoded.get(), size_t(count) * 4);
            loaded = true;
        }
        if (allocation.exceeded()) denied = allocation.needed();
    }
    if (denied) { admissionRejected(AdmissionStore::Working, decoderAllowance, denied); throw AdmissionError(); }
    if (failure.failed()) {
        trussc::internal::imageDecodeOutOfMemory();
        return false;
    }
    return loaded || platformDecode();
} catch (const std::bad_alloc&) {
    pixels.clear();
    trussc::internal::imageDecodeOutOfMemory();
    return false;
}
void validateAnalyzeArguments(const json& args) {
    const auto& ops = args.at("ops");
    if (!ops.is_array()) throw std::runtime_error("ops must be an array");
    const auto limits = getAdmissionLimits();
    checkAdmission(ops.size(), limits.operations, AdmissionStore::Operations);
    for (const auto& op : ops) {
        const auto name = op.at("op").get<std::string>();
        if (name == "histogram") {
            const int bins = integer(op.at("bins"));
            if (bins <= 0) throw std::runtime_error("bins must be positive");
            checkAdmission(size_t(bins), limits.histogramBins, AdmissionStore::Histogram);
        } else if (name == "grid") {
            const int cols = integer(op.at("cols")), rows = integer(op.at("rows"));
            if (cols <= 0 || rows <= 0) throw std::runtime_error("grid cells must be nonempty");
            // Bound (security): check the grid product without multiplying dimensions first. Overflow rejects the operation and is counted.
            if (size_t(cols) > limits.gridCells / size_t(rows)) {
                admissionRejected(AdmissionStore::Grid, limits.gridCells, uint64_t(cols) * rows);
                throw AdmissionError();
            }
        }
    }
}
json analyzeImage(const Pixels& p, const json& args, const char* colorSpace) {
    WorkScope workScope;
    json result{{"width",p.getWidth()},{"height",p.getHeight()},
                {"format",p.isFloat()?"float":"RGBA8"},{"colorSpace",colorSpace}};
    try {
        if (!p.isAllocated()||p.getWidth()<=0||p.getHeight()<=0) throw std::runtime_error("image is empty");
        validateAnalyzeArguments(args);
        const auto& ops=args.at("ops");
        // Reserve worst-case JSON nodes, vector capacity, scalar formatting and
        // escaped envelopes for every operation before any operation runs.
        for (const auto& op : ops) {
            uint64_t scalars = 32;
            const auto name = op.at("op").get<std::string>();
            if (name == "histogram") scalars += uint64_t(integer(op.at("bins"))) * 4;
            if (name == "grid") scalars += uint64_t(integer(op.at("cols"))) * uint64_t(integer(op.at("rows"))) * 4;
            if (name == "line") scalars += uint64_t(std::max(p.getWidth(), p.getHeight())) * 4;
            const auto limit = getAdmissionLimits().replyBytes;
            if (scalars > limit / 32) {
                admissionRejected(AdmissionStore::Reply, limit, scalars > SIZE_MAX / 32 ? SIZE_MAX : size_t(scalars) * 32);
                throw AdmissionError();
            }
            // Bound (abi): scratch estimates must fit size_t before multiplication. Overflow returns an error and is counted.
            if (scalars > getAdmissionLimits().workingBytes / 64) {
                admissionRejected(AdmissionStore::Working, getAdmissionLimits().workingBytes, scalars > SIZE_MAX / 64 ? SIZE_MAX : size_t(scalars) * 64);
                throw AdmissionError();
            }
            reserveReply(size_t(scalars) * 32);
            reserveWork(size_t(scalars) * 64);
        }
        json results=json::array();
        for(const auto& op:ops) results.push_back(operation(p,op));
        if(args.contains("save")&&!args.at("save").is_null()) save(p,args.at("save"));
        result["results"]=std::move(results);
    } catch (const std::exception& e) {
        result["status"]="error"; result["message"]=e.what();
    }
    return result;
}
}
