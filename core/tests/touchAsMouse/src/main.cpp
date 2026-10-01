// =============================================================================
// core/tests/touchAsMouse — behavioral regression test for the touch-as-mouse
// mapping (#295).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI). Drives
// internal::TouchMouseMapper (tc/events/tcTouchMouse.h) directly, the helper
// the event callback uses, since the callback itself needs a window.
//
// Guards the invariants:
//   - The first finger down is the primary and the only touch that drives the
//     mouse: its BEGAN gives one press, its MOVED a drag, its ENDED /
//     CANCELLED one release. Other fingers give no mouse events.
//   - The primary is found by its identifier, not by its index in the touch
//     array, and the full uintptr_t identifier is compared.
//   - After the primary is released, no drag is produced until a new press.
//   - An event with no touches clears the primary.
//   - A primary whose end event never arrived (it is no longer among the
//     touches at the next BEGAN) is dropped, and the new finger presses.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace std;
using namespace tc;
using internal::TouchMouseAction;
using internal::TouchMouseMapper;
using internal::TouchPhase;
using internal::TouchSample;

namespace {

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

// Two identifiers that differ only above the low 32 bits on 64-bit targets
// (as UITouch pointers can), so a comparison on a truncated int would mix
// them up.
static const uintptr_t kIdA = (sizeof(uintptr_t) > 4) ? (uintptr_t)0x100000007ull : 7u;
static const uintptr_t kIdB = (sizeof(uintptr_t) > 4) ? (uintptr_t)0x200000007ull : 8u;
static const uintptr_t kIdC = 42u;

static TouchSample touch(uintptr_t id, float x, float y, bool changed) {
    TouchSample t;
    t.id = id;
    t.x = x;
    t.y = y;
    t.changed = changed;
    return t;
}

struct Counts {
    int press = 0;
    int drag = 0;
    int release = 0;
    vector<TouchMouseAction> actions;  // non-None actions, in order
};

static TouchMouseAction feed(TouchMouseMapper& m, Counts& c, TouchPhase phase,
                             vector<TouchSample> touches) {
    TouchMouseAction a = m.update(phase, touches.data(), (int)touches.size());
    switch (a.kind) {
        case TouchMouseAction::Kind::Press:   c.press++; break;
        case TouchMouseAction::Kind::Drag:    c.drag++; break;
        case TouchMouseAction::Kind::Release: c.release++; break;
        case TouchMouseAction::Kind::None:    break;
    }
    if (a.kind != TouchMouseAction::Kind::None) c.actions.push_back(a);
    return a;
}

static bool at(const TouchMouseAction& a, float x, float y) {
    return a.x == x && a.y == y;
}

} // namespace

TC_CORE_TEST_MAIN() {
    // --- two fingers: the second finger gives no mouse events ----------------
    {
        TouchMouseMapper m;
        Counts c;
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, true)});
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, false), touch(kIdB, 200, 200, true)});
        feed(m, c, TouchPhase::Ended, {touch(kIdA, 10, 10, false), touch(kIdB, 200, 200, true)});
        feed(m, c, TouchPhase::Moved, {touch(kIdA, 15, 12, true)});
        feed(m, c, TouchPhase::Ended, {touch(kIdA, 15, 12, true)});
        check("two fingers: one press, one drag, one release",
              c.press == 1 && c.drag == 1 && c.release == 1);
        bool positions = c.actions.size() == 3 && at(c.actions[0], 10, 10) &&
                         at(c.actions[1], 15, 12) && at(c.actions[2], 15, 12);
        check("two fingers: every mouse event is at the primary's position", positions);
        check("two fingers: no primary after it lifts", !m.hasPrimary());
    }

    // --- primary is found by identifier, whatever its index ------------------
    {
        TouchMouseMapper m;
        Counts c;
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, true)});
        feed(m, c, TouchPhase::Began, {touch(kIdB, 200, 200, true), touch(kIdA, 10, 10, false)});
        TouchMouseAction d1 = feed(m, c, TouchPhase::Moved,
                                   {touch(kIdB, 210, 210, true), touch(kIdA, 20, 20, true)});
        TouchMouseAction d2 = feed(m, c, TouchPhase::Moved,
                                   {touch(kIdB, 220, 220, true), touch(kIdA, 30, 30, false)});
        // B lifts while listed first: no release.
        feed(m, c, TouchPhase::Ended, {touch(kIdB, 220, 220, true), touch(kIdA, 30, 30, false)});
        TouchMouseAction r = feed(m, c, TouchPhase::Ended, {touch(kIdA, 30, 30, true)});
        check("reordered touches: drags follow the primary, not touches[0]",
              d1.kind == TouchMouseAction::Kind::Drag && at(d1, 20, 20) &&
              d2.kind == TouchMouseAction::Kind::Drag && at(d2, 30, 30));
        check("reordered touches: one press and one release, at the primary",
              c.press == 1 && c.release == 1 &&
              r.kind == TouchMouseAction::Kind::Release && at(r, 30, 30));
    }

    // --- primary lifts first: the other finger does not drag -----------------
    {
        TouchMouseMapper m;
        Counts c;
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, true)});
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, false), touch(kIdB, 200, 200, true)});
        feed(m, c, TouchPhase::Ended, {touch(kIdB, 200, 200, false), touch(kIdA, 10, 10, true)});
        feed(m, c, TouchPhase::Moved, {touch(kIdB, 250, 250, true)});
        feed(m, c, TouchPhase::Moved, {touch(kIdB, 260, 260, true)});
        feed(m, c, TouchPhase::Ended, {touch(kIdB, 260, 260, true)});
        check("primary lifts first: one press, one release, no drag",
              c.press == 1 && c.release == 1 && c.drag == 0);
    }

    // --- a finger down while the other is held does not become the primary ---
    {
        TouchMouseMapper m;
        Counts c;
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, true)});
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, false), touch(kIdB, 200, 200, true)});
        feed(m, c, TouchPhase::Ended, {touch(kIdA, 10, 10, true), touch(kIdB, 200, 200, false)});
        // B is still down; a new finger C goes down and becomes the primary.
        feed(m, c, TouchPhase::Began, {touch(kIdB, 200, 200, false), touch(kIdC, 50, 60, true)});
        TouchMouseAction last = c.actions.back();
        check("next press: the next finger down after the primary lifted",
              c.press == 2 && c.release == 1 &&
              last.kind == TouchMouseAction::Kind::Press && at(last, 50, 60));
    }

    // --- CANCELLED releases and resets ----------------------------------------
    {
        TouchMouseMapper m;
        Counts c;
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, true)});
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, false), touch(kIdB, 200, 200, true)});
        feed(m, c, TouchPhase::Cancelled, {touch(kIdA, 11, 11, true), touch(kIdB, 200, 200, true)});
        check("cancelled: one release at the primary",
              c.release == 1 && at(c.actions.back(), 11, 11) && !m.hasPrimary());
        feed(m, c, TouchPhase::Began, {touch(kIdB, 5, 5, true)});
        check("cancelled: the next finger down presses again", c.press == 2);
    }

    // --- several touches began at once: the first changed one is the primary --
    {
        TouchMouseMapper m;
        Counts c;
        TouchMouseAction p = feed(m, c, TouchPhase::Began,
                                  {touch(kIdC, 1, 1, false), touch(kIdA, 10, 10, true),
                                   touch(kIdB, 20, 20, true)});
        check("simultaneous began: one press at the first changed touch",
              c.press == 1 && at(p, 10, 10));
    }

    // --- an event with no touches clears the primary --------------------------
    {
        TouchMouseMapper m;
        Counts c;
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, true)});
        feed(m, c, TouchPhase::Moved, {});
        bool cleared = !m.hasPrimary();
        feed(m, c, TouchPhase::Began, {touch(kIdB, 30, 30, true)});
        check("no touches: the primary is cleared, the next finger presses",
              cleared && c.press == 2);
    }

    // --- primary's end event lost: the next finger down still presses ------
    {
        TouchMouseMapper m;
        Counts c;
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, true)});
        // A's ENDED never arrives. B goes down and A is not among the touches.
        TouchMouseAction p = feed(m, c, TouchPhase::Began, {touch(kIdB, 40, 50, true)});
        TouchMouseAction d = feed(m, c, TouchPhase::Moved, {touch(kIdB, 45, 55, true)});
        TouchMouseAction r = feed(m, c, TouchPhase::Ended, {touch(kIdB, 45, 55, true)});
        check("stale primary: the next finger down presses at its position",
              c.press == 2 && p.kind == TouchMouseAction::Kind::Press && at(p, 40, 50));
        check("stale primary: the new primary drags and releases",
              d.kind == TouchMouseAction::Kind::Drag && at(d, 45, 55) &&
              r.kind == TouchMouseAction::Kind::Release && at(r, 45, 55) &&
              !m.hasPrimary());
    }

    // --- single finger is unchanged: press, drags, release ------------------
    {
        TouchMouseMapper m;
        Counts c;
        feed(m, c, TouchPhase::Began, {touch(kIdA, 10, 10, true)});
        feed(m, c, TouchPhase::Moved, {touch(kIdA, 11, 10, true)});
        feed(m, c, TouchPhase::Moved, {touch(kIdA, 12, 10, true)});
        feed(m, c, TouchPhase::Ended, {touch(kIdA, 12, 10, true)});
        check("single finger: press, two drags, release",
              c.press == 1 && c.drag == 2 && c.release == 1);
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail,
           g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
