#include <TrussC.h>
#include "tcAnalyzeImage.h"
#include <array>
#include <climits>
#include <stdexcept>

namespace trussc::mcp::detail {
namespace {
using Channels = std::array<double, 4>;
struct Rect { int x, y, w, h; };
int integer(const json& j) {
    if (!j.is_number_integer()) throw std::runtime_error("coordinates and sizes must be integers");
    const auto n = j.get<int64_t>();
    if (n < INT_MIN || n > INT_MAX) throw std::runtime_error("integer out of range");
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
void save(const Pixels& p, const json& path) {
    // Reuse both the path resolver and native screenshot encoders (including
    // TIFF/GIF on macOS and TGA on Windows). The writer expects RGBA8.
    auto dest = trussc::internal::resolveScreenshotPath(
        trussc::internal::utf8ToPath(path.get<std::string>()));
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) throw std::runtime_error("failed to create image parent directory");
    Pixels u8;
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
        if (!ref.load(trussc::internal::utf8ToPath(op.at("path").get<std::string>()))) throw std::runtime_error("failed to load reference image");
        if (ref.getWidth()!=p.getWidth() || ref.getHeight()!=p.getHeight()) throw std::runtime_error("reference dimensions differ");
        double threshold=number(op.at("threshold"));
        if (threshold<0) throw std::runtime_error("threshold must be nonnegative");
        Pixels diff;
        bool write=op.contains("save")&&!op.at("save").is_null();
        if (write) diff.allocate(p.getWidth(),p.getHeight(),4);
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
json analyzeImage(const Pixels& p, const json& args, const char* colorSpace) {
    json result{{"width",p.getWidth()},{"height",p.getHeight()},
                {"format",p.isFloat()?"float":"RGBA8"},{"colorSpace",colorSpace}};
    try {
        if (!p.isAllocated()||p.getWidth()<=0||p.getHeight()<=0) throw std::runtime_error("image is empty");
        const auto& ops=args.at("ops");
        if (!ops.is_array()) throw std::runtime_error("ops must be an array");
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
