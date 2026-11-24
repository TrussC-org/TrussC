// JSONL v1 validation, scaling, ring storage, and scene timing.
// Shared by the recorder and player; existing recordings stay compatible.
#pragma once

#include <TrussC.h>
#include <fstream>
#include <optional>
#include <climits>
#include <deque>
#include <map>
#include <chrono>

namespace tcx::inputreplay {

enum class ScaleMode { None, Stretch, Fit };

namespace detail {
using Json = tc::Json;
struct Size { double width = 0, height = 0; };
inline uint64_t clockMs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline Size windowSize(tc::Window& window) {
    return {double(window.getWidth()), double(window.getHeight())};
}


inline tc::Vec2 scalePosition(tc::Vec2 p, Size recorded, Size current, ScaleMode mode) {
    if (mode == ScaleMode::None || recorded.width <= 0 || recorded.height <= 0 ||
        current.width <= 0 || current.height <= 0) return p;
    const double sx = current.width / recorded.width;
    const double sy = current.height / recorded.height;
    if (mode == ScaleMode::Stretch) return {float(p.x * sx), float(p.y * sy)};
    // Off-canvas coordinates stay off-canvas rather than moving into a letterbox.
    if (p.x < 0 || p.y < 0 || p.x > recorded.width || p.y > recorded.height) return p;
    const double factor = (std::min)(sx, sy);
    return {float(p.x * factor + (current.width - recorded.width * factor) / 2),
            float(p.y * factor + (current.height - recorded.height * factor) / 2)};
}

inline uint64_t unsignedValue(const Json& j, const char* key, uint64_t limit = UINT64_MAX) {
    const auto& v = j.at(key);
    if (!v.is_number_integer() || (v.is_number_integer() && !v.is_number_unsigned() && v.get<int64_t>() < 0))
        throw std::runtime_error(std::string(key) + " must be a nonnegative integer");
    const auto result = v.get<uint64_t>();
    if (result > limit) throw std::runtime_error(std::string(key) + " exceeds limit");
    return result;
}
inline double number(const Json& j, const char* key) {
    const auto& v = j.at(key);
    if (!v.is_number()) throw std::runtime_error(std::string(key) + " must be a number");
    const auto result = v.get<double>();
    // Bound (injection representation): coordinates/deltas must fit the float API.
    if (!std::isfinite(result) || std::abs(result) > double((std::numeric_limits<float>::max)()))
        throw std::runtime_error(std::string(key) + " must be a finite float");
    return result;
}
inline int signedInt(const Json& j, const char* key) {
    const auto& v = j.at(key);
    if (!v.is_number_integer() || (v.is_number_unsigned() && v.get<uint64_t>() > INT_MAX) ||
        (!v.is_number_unsigned() && (v.get<int64_t>() < INT_MIN || v.get<int64_t>() > INT_MAX)))
        throw std::runtime_error(std::string(key) + " must fit int");
    return v.get<int>();
}
inline bool validKey(int key) {
    return key == SAPP_KEYCODE_SPACE || key == SAPP_KEYCODE_APOSTROPHE ||
        (key >= SAPP_KEYCODE_COMMA && key <= SAPP_KEYCODE_9) ||
        key == SAPP_KEYCODE_SEMICOLON || key == SAPP_KEYCODE_EQUAL ||
        (key >= SAPP_KEYCODE_A && key <= SAPP_KEYCODE_RIGHT_BRACKET) || key == SAPP_KEYCODE_GRAVE_ACCENT ||
        (key >= SAPP_KEYCODE_WORLD_1 && key <= SAPP_KEYCODE_WORLD_2) ||
        (key >= SAPP_KEYCODE_ESCAPE && key <= SAPP_KEYCODE_END) ||
        (key >= SAPP_KEYCODE_CAPS_LOCK && key <= SAPP_KEYCODE_PAUSE) ||
        (key >= SAPP_KEYCODE_F1 && key <= SAPP_KEYCODE_F25) ||
        (key >= SAPP_KEYCODE_KP_0 && key <= SAPP_KEYCODE_KP_EQUAL) ||
        (key >= SAPP_KEYCODE_LEFT_SHIFT && key <= SAPP_KEYCODE_MENU);
}
inline bool boolean(const Json& j, const char* key) {
    const auto& v = j.at(key);
    if (!v.is_boolean()) throw std::runtime_error(std::string(key) + " must be a boolean");
    return v.get<bool>();
}
inline std::string string(const Json& j, const char* key) {
    const auto& v = j.at(key);
    if (!v.is_string()) throw std::runtime_error(std::string(key) + " must be a string");
    return v.get<std::string>();
}

struct Record {
    std::string type;
    uint64_t ms = 0, frame = 0;
    Json data;
};
struct Session {
    Json header;
    Size size; // First window, retained for single-window callers.
    std::map<std::string, Size> windows;
    std::vector<Record> records;
};

inline bool hasModifiers(const std::string& type) {
    return type == "keyPressed" || type == "keyReleased" || type == "charInput" ||
        type == "mouseMoved" || type == "mousePressed" || type == "mouseReleased" || type == "mouseScrolled";
}

inline tc::InputEvent input(const Record& r, Size recorded, Size current, ScaleMode mode) {
    const auto& j = r.data;
    const auto& type = r.type;
    auto position = [&] {
        return scalePosition({float(number(j, "x")), float(number(j, "y"))}, recorded, current, mode);
    };
    tc::InputModifiers mods;
    if (hasModifiers(type)) {
        const auto bits = unsignedValue(j, "mods", 15);
        mods = {bool(bits & 1), bool(bits & 2), bool(bits & 4), bool(bits & 8)};
    }
    if (type == "keyPressed") return tc::InputEvent::keyPressed(int(unsignedValue(j, "key", SAPP_KEYCODE_MENU)), mods, boolean(j, "repeat"));
    if (type == "keyReleased") return tc::InputEvent::keyReleased(int(unsignedValue(j, "key", SAPP_KEYCODE_MENU)), mods);
    if (type == "charInput") return tc::InputEvent::charInput(uint32_t(unsignedValue(j, "codepoint", 0x10ffff)), mods, boolean(j, "repeat"));
    if (type == "mouseMoved") return tc::InputEvent::mouseMoved(position(), mods);
    if (type == "mousePressed") return tc::InputEvent::mousePressed(position(), tc::MouseButton(unsignedValue(j, "button", 2)), mods);
    if (type == "mouseReleased") return tc::InputEvent::mouseReleased(position(), tc::MouseButton(unsignedValue(j, "button", 2)), mods);
    if (type == "mouseScrolled") return tc::InputEvent::mouseScrolled({float(number(j, "scrollX")), float(number(j, "scrollY"))}, mods);
    if (type == "filesDropped") return tc::InputEvent::filesDropped(j.at("files").get<std::vector<std::string>>(), position());
    tc::TouchEventArgs touches;
    touches.numTouches = int(j.at("touches").size());
    touches.cancelled = boolean(j, "canceled");
    for (int i = 0; i < touches.numTouches; ++i) {
        const auto& t = j.at("touches").at(i);
        auto& out = touches.touches[i];
        out.id = signedInt(t, "id");
        const auto p = scalePosition({float(number(t, "x")), float(number(t, "y"))}, recorded, current, mode);
        out.x = p.x;
        out.y = p.y;
        out.pressure = float(number(t, "pressure"));
        out.changed = boolean(t, "changed");
    }
    if (type == "touchPressed") return tc::InputEvent::touchPressed(touches);
    if (type == "touchMoved") return tc::InputEvent::touchMoved(touches);
    return tc::InputEvent::touchReleased(touches);
}

inline void validateHeader(const Json& j, Session& out) {
    if (unsignedValue(j, "version") != 1) throw std::runtime_error("unknown version");
    string(j, "app");
    string(j, "trussc");
    string(j, "started");
    if (number(j, "fps") < 0) throw std::runtime_error("invalid fps");
    const auto& windows = j.at("windows");
    if (!windows.is_array() || windows.empty()) throw std::runtime_error("expected windows");
    for (const auto& w : windows) {
        const auto key = string(w, "key");
        const Size size{number(w, "width"), number(w, "height")};
        if (key.empty() || !out.windows.emplace(key, size).second) throw std::runtime_error("duplicate or empty window key");
        if (size.width <= 0 || size.height <= 0 || number(w, "dpi") <= 0) throw std::runtime_error("invalid window dimensions/DPI");
        boolean(w, "pixelPerfect");
    }
    out.size = out.windows.at(string(windows.at(0), "key"));
    out.header = j;
}

inline Record validateRecord(Json j) {
    Record r{string(j, "type"), unsignedValue(j, "ms"), unsignedValue(j, "frame"), {}};
    if (r.type == "mark") { string(j, "name"); }
    else if (r.type != "end") {
        if (string(j, "window").empty()) throw std::runtime_error("empty window key");
        boolean(j, "injected");
        string(j, "tag");
        const auto& t = r.type;
        if (hasModifiers(t)) unsignedValue(j, "mods", 15);
        if (t == "keyPressed" || t == "keyReleased") {
            if (!validKey(int(unsignedValue(j, "key", SAPP_KEYCODE_MENU)))) throw std::runtime_error("invalid key");
            if (boolean(j, "repeat") && t == "keyReleased") throw std::runtime_error("key release cannot repeat");
        } else if (t == "charInput") {
            const auto cp = unsignedValue(j, "codepoint", 0x10ffff);
            if ((cp >= 0xd800 && cp <= 0xdfff)) throw std::runtime_error("invalid Unicode scalar");
            boolean(j, "repeat");
        } else if (t == "mouseMoved" || t == "mousePressed" || t == "mouseReleased" || t == "mouseScrolled" || t == "filesDropped") {
            number(j, "x");
            number(j, "y");
            if (t == "mousePressed" || t == "mouseReleased") unsignedValue(j, "button", 2);
            if (t == "mouseScrolled") { number(j, "scrollX"); number(j, "scrollY"); }
            if (t == "filesDropped") {
                if (!j.at("files").is_array()) throw std::runtime_error("files must be an array");
                for (const auto& path : j.at("files")) if (!path.is_string()) throw std::runtime_error("path must be a string");
            }
        } else if (t == "touchPressed" || t == "touchMoved" || t == "touchReleased") {
            if (boolean(j, "canceled") && t != "touchReleased") throw std::runtime_error("only touch release can be canceled");
            const auto& touches = j.at("touches");
            // Bound (platform array): injection accepts at most MAX_TOUCHES entries.
            if (!touches.is_array() || touches.size() > tc::TouchEventArgs::MAX_TOUCHES) throw std::runtime_error("invalid touch count");
            std::vector<int> ids;
            for (const auto& touch : touches) {
                const auto id = signedInt(touch, "id");
                if (std::find(ids.begin(), ids.end(), id) != ids.end()) throw std::runtime_error("duplicate touch ID");
                ids.push_back(id);
                number(touch, "x");
                number(touch, "y");
                const double pressure = number(touch, "pressure");
                if (pressure < 0 || pressure > 1) throw std::runtime_error("invalid pressure");
                boolean(touch, "changed");
            }
        } else throw std::runtime_error("unknown event kind");
    }
    r.data = std::move(j);
    return r;
}

// Transactional: the caller receives nothing until the complete stream is valid.
inline Session load(std::istream& stream) {
    Session result;
    size_t line = 0;
    bool ended = false;
    std::string text;
    try {
        while (std::getline(stream, text)) {
            ++line;
            if (ended) throw std::runtime_error("record after end");
            auto j = Json::parse(text,
                [](int depth, Json::parse_event_t event, Json&) {
                    // Bound (recursive JSON copies/destruction on small stacks): allow at most 16 nested containers.
                    if (depth >= 16 && (event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start))
                        throw std::runtime_error("JSON nesting limit exceeded");
                    return true;
                });
            if (!j.is_object()) throw std::runtime_error("record must be an object");
            if (line == 1) {
                if (string(j, "type") != "header") throw std::runtime_error("missing header");
                validateHeader(j, result);
            } else {
                auto r = validateRecord(std::move(j));
                if (r.type != "mark" && r.type != "end" && !result.windows.count(string(r.data, "window")))
                    throw std::runtime_error("unknown window key");
                if (!result.records.empty() && (r.ms < result.records.back().ms || r.frame < result.records.back().frame))
                    throw std::runtime_error("nonmonotonic time/frame");
                ended = r.type == "end";
                result.records.push_back(std::move(r));
            }
        }
        if (stream.bad() || (stream.fail() && !stream.eof())) throw std::runtime_error("read failed");
        if (!ended) throw std::runtime_error("missing end record");
    } catch (const Json::parse_error&) {
        // Parser diagnostics can quote sensitive recording content; report only its line.
        throw std::runtime_error("line " + std::to_string((std::max)(size_t(1), line)) + ": malformed JSON");
    } catch (const std::exception& e) {
        throw std::runtime_error("line " + std::to_string((std::max)(size_t(1), line)) + ": " + e.what());
    }
    return result;
}

// Store complete records within a duration and an optional explicit capacity.
// Original offsets can span a long-running installation; export rebases them.
class Ring {
public:
    struct Entry { Json data; };
    std::deque<Entry> records;
    Json header;
    uint64_t duration = 0;
    size_t capacity = 0, inputCount = 0;
    void start(Json value, uint64_t milliseconds, size_t count = (std::numeric_limits<size_t>::max)()) {
        if (!milliseconds || !count) throw std::runtime_error("ring duration/capacity must be positive");
        Session metadata;
        validateHeader(value, metadata);
        *this = {};
        header = std::move(value);
        duration = milliseconds;
        capacity = count;
    }
    void trim(uint64_t now) {
        while (!records.empty() && (records.size() > capacity ||
            (now > duration && unsignedValue(records.front().data, "ms") < now - duration))) {
            if (records.front().data.at("type") != "mark") --inputCount;
            records.pop_front();
        }
    }
    void push(Json value) {
        validateRecord(value);
        const auto now = unsignedValue(value, "ms");
        const bool isInput = value.at("type") != "mark";
        records.push_back({std::move(value)});
        if (isInput) ++inputCount;
        trim(now);
    }
    void save(std::ostream& stream, uint64_t now, uint64_t frame) {
        trim(now);
        const uint64_t baseMs = records.empty() ? now : unsignedValue(records.front().data, "ms");
        const uint64_t baseFrame = records.empty() ? frame : unsignedValue(records.front().data, "frame");
        stream << header.dump() << '\n';
        for (const auto& entry : records) {
            auto value = entry.data;
            value["ms"] = unsignedValue(value, "ms") - baseMs;
            value["frame"] = unsignedValue(value, "frame") - baseFrame;
            stream << value.dump() << '\n';
        }
        stream << Json({{"type", "end"}, {"ms", now - baseMs}, {"frame", frame - baseFrame}}).dump() << '\n';
        if (!stream) throw std::runtime_error("ring write failed");
    }
};

// A clock supplied by the caller makes scene synchronization independently testable.
class Timeline {
public:
    Session session;
    size_t index = 0;
    bool active = false;
    void start(Session value, uint64_t now) {
        session = std::move(value);
        index = 0;
        baseMs_ = 0;
        startNow_ = now;
        active = true;
        findMark();
    }
    bool mark(const std::string& name, uint64_t now) {
        if (!active || nextMark_ == session.records.size() ||
            session.records[nextMark_].data.at("name") != name) return false;
        baseMs_ = session.records[nextMark_].ms;
        startNow_ = now;
        index = nextMark_ + 1;
        findMark();
        return true;
    }
    std::optional<Record> next(uint64_t now) {
        if (!active || index >= session.records.size() || index == nextMark_) return {};
        const auto& r = session.records[index];
        if (now < startNow_ || now - startNow_ < r.ms - baseMs_) return {};
        ++index; // Advance before synchronous injection can call the app's mark().
        if (r.type == "end") active = false;
        return r;
    }
private:
    size_t nextMark_ = 0;
    uint64_t startNow_ = 0, baseMs_ = 0;
    void findMark() {
        nextMark_ = index;
        while (nextMark_ < session.records.size() && session.records[nextMark_].type != "mark") ++nextMark_;
    }
};
} // namespace detail
} // namespace tcx::inputreplay
