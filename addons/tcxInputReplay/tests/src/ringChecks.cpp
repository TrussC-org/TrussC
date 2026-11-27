#include <tcxInputReplay.h>
#include <cstdio>
#include <sstream>

namespace {
using namespace tcx::inputreplay::detail;
int failures = 0, checks = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", label); }
}
Json header() {
    return {{"type", "header"}, {"version", 1}, {"app", "ring"}, {"trussc", "test"}, {"started", "0"}, {"fps", 60},
        {"windows", Json::array({{{"key", "main"}, {"width", 100}, {"height", 50}, {"dpi", 1}, {"pixelPerfect", false}}})}};
}
Json mark(uint64_t ms, uint64_t frame, const std::string& name = "scene") {
    return {{"type", "mark"}, {"ms", ms}, {"frame", frame}, {"name", name}};
}
Session exported(Ring& ring, uint64_t ms, uint64_t frame) {
    std::stringstream stream;
    ring.save(stream, ms, frame);
    return load(stream);
}
void ringChecks() {
    Ring ring;
    ring.start(header(), 100, 2);
    ring.push(mark(10, 2, "evicted"));
    auto event = Json{{"type", "mouseMoved"}, {"ms", 20}, {"frame", 3}, {"window", "main"},
        {"x", 10}, {"y", 20}, {"mods", 0}, {"injected", false}, {"tag", ""}};
    ring.push(event);
    event["ms"] = 30;
    event["frame"] = 4;
    ring.push(event);
    auto session = exported(ring, 40, 5);
    check(session.records.size() == 3 && session.records.front().type == "mouseMoved", "count eviction removes initial mark");
    check(session.records[0].ms == 0 && session.records[0].frame == 0 &&
        session.records[1].ms == 10 && session.records[1].frame == 1 &&
        session.records.back().ms == 20 && session.records.back().frame == 2, "flush rebases time and frame");
    Timeline timeline;
    timeline.start(session, 1000);
    check(timeline.next(1000)->type == "mouseMoved", "retained segment plays without evicted mark");
    check(timeline.next(1010)->type == "mouseMoved" && timeline.next(1020)->type == "end", "retained segment completes");
    session = exported(ring, 120, 6);
    check(session.records.size() == 3, "duration cutoff retains exact boundary");
    session = exported(ring, 121, 6);
    check(session.records.size() == 2 && session.records.front().ms == 0, "flush trims time even with no new input");
    session = exported(ring, 1000, 7);
    check(session.records.size() == 1 && session.records[0].ms == 0 && session.records[0].frame == 0, "idle ring exports empty valid session");
    try { ring.start(header(), 1, 0); check(false, "zero capacity rejected"); }
    catch (const std::exception&) { check(true, "zero capacity rejected"); }
    try { ring.start(header(), 0); check(false, "zero duration rejected"); }
    catch (const std::exception&) { check(true, "zero duration rejected"); }
    ring.start(header(), 604800001);
    auto line = mark(604800001, 0, std::string(65537, 'x'));
    ring.push(line);
    session = exported(ring, 1209600002, 0);
    check(session.records.back().ms == 604800001, "long duration and line export loads");
    ring.start(header(), 1);
    for (size_t i = 0; i < 1000000; ++i) ring.push(mark(0, 0, ""));
    check(ring.records.size() == 1000000, "default ring capacity exceeds old cap");
}
}
int runInputReplayRingChecks() {
    ringChecks();
    std::printf("Ring: %d checks, %d failures\n", checks, failures);
    return failures;
}
