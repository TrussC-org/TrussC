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
    ring.start(header(), timeLimit, 3);
    ring.push(mark(timeLimit * 2, 100));
    session = exported(ring, timeLimit * 3, 101);
    check(session.records.back().ms == timeLimit, "seven-day exported offset at limit despite long uptime");
    session = exported(ring, timeLimit * 3 + 1, 102);
    check(session.records.size() == 1, "seven-day over boundary evicts");
    for (auto count : {size_t(0), recordLimit - 1}) {
        try { ring.start(header(), 1, count); check(false, "invalid capacity rejected"); }
        catch (const std::exception&) { check(true, "invalid capacity rejected"); }
    }
    for (auto duration : {uint64_t(0), timeLimit + 1}) {
        try { ring.start(header(), duration, 1); check(false, "invalid duration rejected"); }
        catch (const std::exception&) { check(true, "invalid duration rejected"); }
    }
    ring.start(header(), timeLimit, recordLimit - 2);
    auto line = mark(0, 0, "");
    line["name"] = std::string(lineLimit - line.dump().size(), 'x');
    check(line.dump().size() == lineLimit, "ring line exactly at limit");
    ring.push(line);
    session = exported(ring, 0, 0);
    check(session.records.size() == 2, "exact-limit line flush loads");
    line["name"] = line["name"].get<std::string>() + 'x';
    try { ring.push(line); check(false, "oversized line rejected"); }
    catch (const std::exception&) { check(true, "oversized line rejected"); }
    // Fill the byte bound using full-size valid lines. Export remains below 128 MiB.
    line["name"] = line["name"].get<std::string>().substr(1);
    const size_t lines = fileLimit / (lineLimit + 1) + 2;
    for (size_t i = 1; i < lines; ++i) ring.push(line);
    check(ring.records.size() < lines && ring.bytes + ring.headerBytes + 128 <= fileLimit, "byte bound evicts complete records");
    session = exported(ring, 0, 0);
    check(session.records.size() == ring.records.size() + 1, "near file-limit flush loads");
    session = {};
    ring.start(header(), 1, recordLimit - 2);
    for (size_t i = 0; i < recordLimit - 1; ++i) ring.push(mark(0, 0, ""));
    check(ring.records.size() == recordLimit - 2, "loader count bound evicts one record");
    session = exported(ring, 0, 0);
    check(session.records.size() == recordLimit - 1, "exact loader record-limit flush loads");
}
}
int runInputReplayRingChecks() {
    ringChecks();
    std::printf("Ring: %d checks, %d failures\n", checks, failures);
    return failures;
}
