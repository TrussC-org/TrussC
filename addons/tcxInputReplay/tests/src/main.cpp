#include <tcxInputReplay.h>
#include <cstdio>
#include <sstream>

namespace {
using namespace tcx::inputreplay;
using namespace tcx::inputreplay::detail;
int failures = 0, checks = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", name); }
}
Json header() {
    return {{"type", "header"}, {"version", 1}, {"app", "test"}, {"trussc", "test"}, {"started", "0"}, {"fps", 60},
        {"windows", Json::array({{{"key", "main"}, {"width", 100}, {"height", 50}, {"dpi", 1}, {"pixelPerfect", false}}})}};
}
Json end(uint64_t ms = 0, uint64_t frame = 0) { return {{"type", "end"}, {"ms", ms}, {"frame", frame}}; }
Json move() {
    return {{"type", "mouseMoved"}, {"ms", 10}, {"frame", 1}, {"window", "main"},
        {"x", 10}, {"y", 20}, {"mods", 15}, {"injected", true}, {"tag", "script"}};
}
std::string encode(const std::vector<Json>& records) {
    std::string result;
    for (const auto& j : records) result += j.dump() + '\n';
    return result;
}
bool accepts(const std::string& value, const std::string& expected = "") {
    std::istringstream stream(value);
    try { load(stream); return expected.empty(); }
    catch (const std::exception& e) { return !expected.empty() && std::string(e.what()).find(expected) != std::string::npos; }
}
void validation() {
    const auto h = header(), m = move(), e = end(10, 1);
    check(accepts(encode({h, m, e})), "valid JSONL");
    check(accepts(encode({h, e}).substr(0, encode({h, e}).size() - 1)), "no final newline required");
    check(accepts("", "line 1"), "empty stream");
    check(accepts(encode({m, e}), "missing header"), "header required");
    check(accepts(encode({h, m}), "missing end"), "truncated stream");
    check(accepts(encode({h, e, m}), "record after end"), "trailing record");
    check(accepts(encode({h}) + "{bad\n", "line 2"), "malformed JSON line number");
    check(accepts(encode({h}) + "[]\n", "object"), "object required");
    {
        std::istringstream privateData(encode({h}) + "{\"private-note\":unclosed\n");
        try { load(privateData); check(false, "malformed private input rejected"); }
        catch (const std::exception& ex) {
            check(std::string(ex.what()) == "line 2: malformed JSON", "parser diagnostics omit recording content");
        }
    }
    auto bad = h;
    bad["version"] = 2;
    check(accepts(encode({bad, e}), "unknown version"), "version rejected");
    for (const auto* field : {"version", "app", "trussc", "started", "fps", "windows"}) {
        bad = h;
        bad.erase(field);
        check(accepts(encode({bad, e}), "line 1"), "header field required");
    }
    for (const auto* field : {"key", "width", "height", "dpi", "pixelPerfect"}) {
        bad = h;
        bad["windows"][0].erase(field);
        check(accepts(encode({bad, e}), "line 1"), "window field required");
    }
    for (const auto* field : {"type", "ms", "frame", "window", "x", "y", "mods", "injected", "tag"}) {
        bad = m;
        bad.erase(field);
        check(accepts(encode({h, bad, e}), "line 2"), "event field required");
    }
    for (const auto* field : {"ms", "frame", "x", "mods", "injected", "tag"}) {
        bad = m;
        bad[field] = Json::array();
        check(accepts(encode({h, bad, e}), "line 2"), "invalid field type");
    }
    bad = m;
    bad["type"] = "newEvent";
    check(accepts(encode({h, bad, e}), "unknown event"), "unknown event kind");
    bad = m;
    bad["window"] = "other";
    check(accepts(encode({h, bad, e}), "unknown window"), "unknown window");
    bad = h;
    bad["windows"][0]["width"] = 0;
    check(accepts(encode({bad, e}), "dimensions"), "zero dimensions");
    bad = h;
    bad["windows"].push_back(h["windows"][0]);
    check(accepts(encode({bad, e}), "duplicate"), "duplicate window key");
    check(accepts(encode({h, m, end(9, 1)}), "nonmonotonic"), "time ordering");
    check(accepts(encode({h, m, end(10, 0)}), "nonmonotonic"), "frame ordering");
    check(accepts(encode({h, end(UINT64_MAX, UINT64_MAX)})), "time and frame at bound");
    check(accepts(encode({h, end(604800001)})), "time beyond seven days");
    bad = m;
    bad["frame"] = -1;
    check(accepts(encode({h, bad, e}), "nonnegative"), "negative frame");
    bad["frame"] = 1.5;
    check(accepts(encode({h, bad, e}), "integer"), "fractional frame");
    bad = m;
    bad["mods"] = 16;
    check(accepts(encode({h, bad, e}), "mods exceeds"), "modifier domain");
    bad = m;
    bad["x"] = 1e100;
    check(accepts(encode({h, bad, e}), "finite float"), "float overflow");
    check(accepts(encode({h}) + "{\"type\":\"end\",\"ms\":1e999,\"frame\":0}\n", "line 2"), "nonfinite input");
    auto key = m;
    key["type"] = "keyPressed";
    key["key"] = SAPP_KEYCODE_A;
    key["repeat"] = true;
    check(accepts(encode({h, key, e})), "key repeat");
    for (int value : {-1, 0, 100, 349}) {
        key["key"] = value;
        check(accepts(encode({h, key, e}), "line 2"), "invalid key domain");
    }
    key["type"] = "keyReleased";
    key["key"] = SAPP_KEYCODE_A;
    check(accepts(encode({h, key, e}), "cannot repeat"), "key release repeat rejected");
    key["type"] = "charInput";
    for (int cp : {0, 0x61, 0x10ffff}) {
        key["codepoint"] = cp;
        check(accepts(encode({h, key, e})), "Unicode scalar");
    }
    for (int cp : {-1, 0xd800, 0xdfff, 0x110000}) {
        key["codepoint"] = cp;
        check(accepts(encode({h, key, e}), "line 2"), "invalid Unicode");
    }
    bad = m;
    bad["type"] = "mousePressed";
    bad["button"] = 3;
    check(accepts(encode({h, bad, e}), "button exceeds"), "button domain");
    bad["type"] = "filesDropped";
    bad["files"] = {"one", 2};
    check(accepts(encode({h, bad, e}), "path"), "drop paths type");
    bad = m;
    bad["type"] = "touchReleased";
    bad["canceled"] = true;
    Json point = {{"id", -42}, {"x", 10}, {"y", 5}, {"pressure", 1}, {"changed", true}};
    bad["touches"] = Json::array({point});
    check(accepts(encode({h, bad, e})), "signed touch ID and cancellation");
    bad["type"] = "touchMoved";
    check(accepts(encode({h, bad, e}), "canceled"), "touch cancellation phase rejected");
    bad["type"] = "touchReleased";
    bad["touches"].push_back(point);
    check(accepts(encode({h, bad, e}), "duplicate"), "duplicate touch IDs");
    bad["touches"] = Json::array();
    for (int i = 0; i < tc::TouchEventArgs::MAX_TOUCHES; ++i) {
        point["id"] = i;
        bad["touches"].push_back(point);
    }
    check(accepts(encode({h, bad, e})), "touch count at bound");
    point["id"] = 33;
    bad["touches"].push_back(point);
    check(accepts(encode({h, bad, e}), "touch count"), "touch count over bound");
    for (const auto& type : {"keyPressed", "keyReleased", "charInput", "mousePressed", "mouseReleased", "mouseScrolled", "filesDropped", "touchPressed", "touchMoved", "touchReleased", "mark", "end"}) {
        auto record = m;
        record["type"] = type;
        record["key"] = SAPP_KEYCODE_A;
        record["repeat"] = false;
        record["codepoint"] = 0x61;
        record["button"] = 0;
        record["scrollX"] = 1;
        record["scrollY"] = -1;
        record["files"] = Json::array({"a"});
        record["touches"] = Json::array({point});
        record["canceled"] = false;
        record["name"] = "scene";
        std::vector<const char*> required;
        const std::string t = type;
        if (t == "keyPressed" || t == "keyReleased") required = {"key", "repeat", "mods"};
        else if (t == "charInput") required = {"codepoint", "repeat", "mods"};
        else if (t == "mousePressed" || t == "mouseReleased") required = {"button", "x", "y", "mods"};
        else if (t == "mouseScrolled") required = {"scrollX", "scrollY", "x", "y", "mods"};
        else if (t == "filesDropped") required = {"files", "x", "y"};
        else if (t.starts_with("touch")) required = {"touches", "canceled"};
        else if (t == "mark") required = {"name"};
        else required = {"ms", "frame"};
        for (const auto* field : required) {
            auto missing = record;
            missing.erase(field);
            check(accepts(encode({h, missing, e}), "line 2"), "required payload field");
        }
    }
    check(accepts(encode({h}) + "{\"type\":\"end\",\"ms\":0,\"frame\":18446744073709551616}\n", "integer"), "uint64 overflow rejected");
    // Round trip preserves all data, including paths, provenance, Unicode and touch metadata.
    Json drop = m;
    drop["type"] = "filesDropped";
    drop["files"] = {"日本語/one.txt", "a\\b\"c"};
    auto text = encode({h, m, drop, e});
    std::istringstream stream(text);
    auto session = load(stream);
    check(session.header == h && session.records[0].data == m && session.records[1].data == drop, "JSON round trip");
    drop["mods"] = Json::array(); // Not part of a drop payload: extensions must not affect its decoder.
    std::istringstream extraFields(encode({h, drop, e}));
    try {
        const auto loaded = load(extraFields);
        input(loaded.records[0], loaded.size, loaded.size, ScaleMode::None);
        check(true, "validated drop ignores irrelevant modifier extension");
    } catch (...) { check(false, "validated drop ignores irrelevant modifier extension"); }
    auto padded = h.dump();
    padded.insert(padded.size() - 1, 65537 - padded.size(), ' ');
    check(accepts(padded + "\n" + e.dump() + "\n"), "line beyond old bound");
    for (size_t arrays : {size_t(15), size_t(16)}) {
        const auto extension = ",\"extra\":" + std::string(arrays, '[') + "0" + std::string(arrays, ']') + "}";
        const auto nestedHeader = h.dump().substr(0, h.dump().size() - 1) + extension;
        const auto nestedEvent = m.dump().substr(0, m.dump().size() - 1) + extension;
        check(accepts(nestedHeader + "\n" + e.dump() + "\n", arrays == 15 ? "" : "line 1: JSON nesting limit"), "header nesting at/over bound");
        check(accepts(encode({h}) + nestedEvent + "\n" + e.dump() + "\n", arrays == 15 ? "" : "line 2: JSON nesting limit"), "event nesting at/over bound");
    }
    check(accepts(encode({h}) + std::string(32000, '[') + "0" + std::string(32000, ']') + "\n", "line 2: JSON nesting limit"), "deep input rejected during parsing");
}

// Generate repeated records without holding a second complete copy.
class RepeatedStream : public std::streambuf {
    std::string first_, repeated_, last_, chunk_;
    size_t count_, emitted_ = 0;
public:
    RepeatedStream(std::string first, std::string repeated, size_t count, std::string last)
        : first_(std::move(first)), repeated_(std::move(repeated)), last_(std::move(last)), count_(count) {}
    int_type underflow() override {
        if (gptr() && gptr() < egptr()) return traits_type::to_int_type(*gptr());
        if (emitted_ == 0) chunk_ = first_;
        else if (emitted_ <= count_) chunk_ = repeated_;
        else if (emitted_ == count_ + 1) chunk_ = last_;
        else return traits_type::eof();
        ++emitted_;
        setg(chunk_.data(), chunk_.data(), chunk_.data() + chunk_.size());
        return underflow();
    }
};
void largeRecording() {
    RepeatedStream buf(header().dump() + '\n',
        "{\"type\":\"mark\",\"ms\":0,\"frame\":0,\"name\":\"\"}\n", 1000000, end().dump() + '\n');
    std::istream stream(&buf);
    check(load(stream).records.size() == 1000001, "more than old record limit loads");
}
void scaling() {
    const Size recorded{100, 50}, current{300, 100};
    check(scalePosition({20, 10}, recorded, current, ScaleMode::None) == tc::Vec2(20, 10), "None");
    check(scalePosition({20, 10}, recorded, current, ScaleMode::Stretch) == tc::Vec2(60, 20), "Stretch");
    check(scalePosition({20, 10}, recorded, current, ScaleMode::Fit) == tc::Vec2(90, 20), "Fit centered");
    check(scalePosition({0, 0}, recorded, current, ScaleMode::Fit) == tc::Vec2(50, 0), "Fit left edge");
    check(scalePosition({100, 50}, recorded, current, ScaleMode::Fit) == tc::Vec2(250, 100), "Fit right edge");
    check(scalePosition({-5, 25}, recorded, current, ScaleMode::Fit) == tc::Vec2(-5, 25), "outside left passes unchanged");
    check(scalePosition({101, 51}, recorded, current, ScaleMode::Fit) == tc::Vec2(101, 51), "outside right passes unchanged");
    check(scalePosition({20, 10}, recorded, {100, 200}, ScaleMode::Fit) == tc::Vec2(20, 85), "Fit top letterbox");
    check(scalePosition({40, 20}, {200, 100}, {600, 200}, ScaleMode::Fit) == tc::Vec2(180, 40), "framebuffer pixelPerfect math");
    check(scalePosition({20, 10}, recorded, {200, 200}, ScaleMode::Stretch) == tc::Vec2(40, 40), "resize uses current factors");
}
void timing() {
    Json m = {{"type", "mark"}, {"name", "scene"}, {"ms", 100}, {"frame", 2}};
    auto event = move();
    event["ms"] = 120;
    event["frame"] = 3;
    std::istringstream stream(encode({header(), move(), m, event, end(130, 4)}));
    const auto s = load(stream);
    Timeline t;
    t.start(s, 1000);
    check(!t.next(1009), "not due");
    check(t.next(1010)->type == "mouseMoved", "due at offset");
    check(!t.next(999999), "wait indefinitely at mark");
    check(!t.mark("other", 999999), "unmatched mark ignored");
    check(t.mark("scene", 999999), "late mark matches");
    check(!t.next(1000018), "clock restarts at late mark");
    check(t.next(1000019)->ms == 120, "offset from mark");
    check(t.next(1000029)->type == "end" && !t.active, "natural end");
    t.start(s, 1000);
    check(t.mark("scene", 1001), "early mark matches");
    check(!t.next(1020), "early mark rebases");
    check(t.next(1021)->ms == 120, "early mark drops previous input");
    check(t.next(1031)->type == "end", "end follows new clock");
    t.start(s, UINT64_MAX - 200);
    check(t.next(UINT64_MAX - 190)->ms == 10, "clock arithmetic near uint64 bound");
}
} // namespace
int runInputReplayRingChecks();
int runInputReplayNodeChecks();
int main() {
    sg_desc graphics = {};
    sg_setup(&graphics);
    validation();
    scaling();
    timing();
    largeRecording();
    std::printf("%d checks, %d failures\n", checks, failures);
    const int ringFailures = runInputReplayRingChecks();
    const int nodeFailures = runInputReplayNodeChecks();
    sg_shutdown();
    return failures || ringFailures || nodeFailures ? 1 : 0;
}
