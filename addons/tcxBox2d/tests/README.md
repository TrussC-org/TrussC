# tcxBox2d tests

Headless console test (no window). The world is stepped by hand and nothing is
drawn.

Polygon points (#342). `PolyShape::setup()` and `RigidBody2D` with
`Shape2D::polygon()` share one check, run before the body is created:

- fewer than 3 or more than 8 points, collinear points, points Box2D would
  merge, and a hull with almost no area each give one warning and no body, in
  Debug and Release alike;
- concave input with 3 to 8 points becomes its convex hull, and
  `getVertices()` / `shape().verts` hold that hull (what draws is what
  collides); convex input already in outline order keeps its order, and hull
  points listed in a crossing order come back in hull order;
- when the hull leaves points inside it (#500), `setup()` and
  `Shape2D::polygon()` each log one warning with the counts that points to
  `setupCompound()` / `Shape2D::compound()`, and only the first time in the
  process; convex input, hull points in a crossing order and a collinear
  middle point give none (checked before the first warning);
- `PolyShape::setup()` and `Shape2D::polygon()` with a `tc::Path` use every
  point of every subpath;
- `setupSimplified()` / `Shape2D::simplified()` take any number of points and make one
  fixture of at most 8 points whose mass is close to the outline's; input
  they collapse to fewer than 3 points gets a warning saying so. The
  reduction picks the same points as the O(h^2) scan it replaced (kept in the
  harness as the reference) and reduces a 50,000-point outline in under 1 s.

Compound bodies (#427). `setupCompound()` / `Shape2D::compound()` keep any
outline exactly, one fixture per triangle:

- a 20-point circle gives one fixture per triangle and the 20-gon's mass; a
  convex outline of at most 8 points (also with a closing point) gives exactly
  one fixture, a pentagram does not (its center, covered twice by the fill,
  weighs twice, as documented);
- the fill kept for drawing is `Path::buildFillTriangles()`' own;
- a notch and a hole stay empty: a ball in a ring's hole falls to the hole's
  floor, and a ball above the solid part lands on it;
- slivers are skipped with one warning; an outline with nothing usable gives a
  warning and no body;
- `Collider2D` filter setters, `setSensor()` and `RigidBody2D::setTrigger()`
  reach every fixture;
- a box pressed into a compound bar touches several of its fixtures at once,
  and still gives exactly one Enter / Began per side, one Stay per update, and
  one Exit / Ended per side when they separate (classic `Collider2D` and Mod
  `RigidBody2D` events);
- a small sensor box moved back and forth across the seam between two
  fixtures, touching one at a time while both contacts live on, gets one
  Enter / Began and no Exit / Ended until it leaves during managed World
  sub-steps (raw Steps preserve all aggregated transitions);
- a Stay listener may destroy a body: of the next pair in the list (the
  survivor still gets its Stay, the destroyed box no further Exit or Stay), of
  the pair being dispatched (no Stay after it, and the next pair still gets
  its Stay), or drop a `RigidBody2D` node;
- the offset/inertia check runs once on the whole body: a 600x40 rounded
  rectangle with 4 px corners, a 128-gon 1000 px off the origin and a
  4096-gon keep every triangle with no warning and the analytic mass; a 1 px
  outline 2000 px off is refused as a whole, and a tiny convex ring far off
  as one polygon, with the "too small for its distance" warning; bodies just
  inside the limit, built through `setupCompound()` and `RigidBody2D`, keep a
  positive inertia at densities 0.001 to 1000 (Debug: no Box2D assert while
  the fixtures are added);
- on a compound 128-gon and a hexagon just inside that limit, every fixture
  has the requested density after creation (`PolyShape` at 1, `RigidBody2D`
  at its density, dynamic or static), and `setDensity()` /
  `setBodyType(Dynamic)` (classic: `setStatic()`, `setDensity()`,
  `setDynamic()`) afterwards give exactly the mass, inertia and center of a
  body created that way, at densities 0.001 to 1000 (Debug: no Box2D assert).

`--density-and-type` runs those 20 checks alone and prints both sides' mass,
local center and inertia as hex floats for every density and transition.
The comparison remains exact. Its `massProps()` reader is deliberately not
inlined: with FMA enabled, GCC can contract `GetInertia()`'s `x*x + y*y` in
opposite orders at different inline sites, even for identical stored body
data. Both sides must use the same readout instructions. The x86 contraction
check builds **both the tests/addon and Box2D** with
`-O2 -mfma -ffp-contract=fast`, then with `-ffp-contract=off`; also run Release
(`-O3`) to exercise GCC's different inlining choices. See `REPORT.md` for the
base/head matrix, instruction-level diagnosis and exact values.

Event lifetimes (#427). Listeners of a deferred Exit / Ended or of Stay may
free the other body of their pair, and bodies may be destroyed between
`Step()` and `update()`:

- an Exit / Ended listener that frees the other body (classic `Body` or a
  `RigidBody2D`'s node) leaves it unnotified; when two balls leave a
  platform in one step and the first one's listener frees the platform, the
  second ball still gets its Exit / Ended, with no other body;
- `Step()`, destroy the ball, add a new one (Box2D hands out the freed
  `b2Body` again), then `update()`: the platform's Exit / Ended names no
  other body, and neither the freed ball nor the new one hears anything;
  `createBounds()` (new walls at the freed address) and `clear()` there
  leave the world-level Ended naming no freed body;
- a `RigidBody2D` Stay listener that drops the other node of the pair being
  dispatched, and one that also adds a new ball at the freed address: the
  dropped side gets no Stay, the new ball nothing.

Outside-step Exits (#473) are queued until Box2D returns:

- `Body::destroy()`, disabling, each body type setter,
  and each `setBodyType()` transition deliver eligible Exits before the wrapper
  returns, unless an earlier raw-Step record is waiting for update; an Exit listener destroys a third touching body, with each live
  side notified exactly once, in both body creation orders;
- listeners check the completed operation (destroyed or disabled state,
  or removed contacts), and a later update does not repeat any Exit;
- raw disabling, type changes and destruction deliver no Exit during the
  Box2D call; the next update flushes them and its listener can destroy a
  third body safely.
- owner destructors and `RigidBody2D::onDestroy()` release bodies silently;
  surviving sides receive their pending Exits at the next World delivery
  point. Destruction/removal never calls user listeners synchronously.

ASan/UBSan cover wrapper-side lifetimes (`Body`, `RigidBody2D`,
`CollisionManager`, World members). Pooled `b2Body` / `b2Fixture` / `b2Joint`
reuse is invisible to ASan because Box2D returns them to its block allocator;
the defence is destruction observation, endpoint retirement, owner generations
and explicit handle-invalid assertions.

CI builds this harness with Box2D's asserts compiled out (RelWithDebInfo, the
default build type, or Release). Adding the fixtures at density 0 (and
setting the density once all are in) is guarded only by a Debug build:
without it, "combined inertia check ... just inside" aborts on Box2D's
`m_I > 0` assert in Debug and still passes in CI. Run a Debug build after
touching fixture creation:

```bash
trusscli build -p . --debug   # from this directory; generates the build files if missing
./bin/tests                   # macOS: ./bin/tests.app/Contents/MacOS/tests
```

`trusscli run -p . --debug` builds and runs in one go. Build again without
`--debug` (or with `--release`) to get back to the default build type.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR; a non-zero exit fails the job. Run it locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```

Owner lifetimes (#842, #471, #456). `--owner-lifetimes` runs only the
headless lifetime regressions (also included in the full suite):

- destroying the World first, `clear()`, and a second `setup()` silently
  invalidate live classic bodies, Mod bodies, and collider fixture handles;
- calls on released owners, repeated `destroy()`, and later owner destruction
  are safe; a cleared World remains usable without another setup;
- destroying a dragged body ends the mouse joint, and another drag can start;
- point queries return classic owners and skip Mod owners; drag anchors have
  null owner data.

`--clear-reuse` selects the clear-contract regressions (also in the full suite
and `--owner-lifetimes`): clear preserves the same b2World, gravity, simulation
rate, auto-update subscription and raw Box2D settings. Ground, raw unowned bodies
and drag anchors are all destroyed. Bounds plus fresh classic/Mod bodies work
immediately and move under gravity in both manual and automatic updates; stale
owner calls cannot affect them. Fresh bounds and drags can be replaced/cleared
with exact body/joint counts. Deferred destructor Exits are discarded by clear
for Mod removal, Node destruction and classic destruction; fresh contacts then
Enter and Exit exactly once through the preserved listeners.

Review round 1 (adversarial). `--review-round-1` selects the callback and
replacement regressions; they also run in the full suite and with
`--owner-lifetimes`. Both groups now exercise `World::update()` using controlled
frame IDs and delta time, without raw Box2D stepping:

- `clear-stay` / `setup-stay`: release from world-level Stay and either classic
  side; cancel the active epoch and dispatch fresh contacts on later steps;
- `clear-update` / `setup-update`: clear or replace the World on the first
  sub-step's Ended. As on base, clear keeps the accumulator and continues the
  current update's remaining sub-steps (including bodies created by that
  listener); only setup changes the release generation and stops the old update,
  preserving the replacement's fresh accumulator;
- `replace-mod`: replace an exclusive Mod with active contacts, then clear the
  World; explicit Mod removal must also release exactly once;
- `reentrant-circle`, `reentrant-rect`, `reentrant-poly`,
  `reentrant-simplified`, `reentrant-compound`: an Exit listener sets up the
  same owner from the surviving partner's Exit during its outer setup; retain the inner body, leave no orphan,
  then tear down the World after the owner is gone.

Pass a case name after `--review-round-1` to run it alone. For example:

```bash
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./bin/tests --review-round-1 clear-update
```

Review round 2 (adversarial). `--review-round-2` selects the new regressions;
they also run in the full suite and with `--owner-lifetimes`:

- `replace-remove` / `replace-replace`: an Ended or surviving Mod Exit
  listener removes or replaces the newly installed Mod. The outer `addMod()`
  must return with setup complete before the listener runs; the next delivery
  mutates it safely and delivers exactly once (both listener layers per mode).
- `owner-teardown`: explicit Mod removal, Node destruction, and classic Body
  destruction release the body with no synchronous events. The next delivery
  names only surviving owners, never dispatches to the destroyed side, and
  does not repeat the Exit.

Pass a case name after `--review-round-2` to run it alone.

Sub-step delivery (#842 PR 2). `--delivery` runs the new deterministic group,
also included in the ordinary full suite. `--delivery <mode>` selects one:

- `clear-late-world-stay`, `clear-late-world-exit`, `delete-world-stay`,
  `delete-world-exit`, `delete-classic-stay`, `delete-classic-exit` reproduce
  #845's six cases: zero gravity, auto-update off, classic circles at
  (100,100)/(110,100), radius 10, first sensor; establish contact first.
  For Exit move the second to (400,400); clear uses dt=1/60, deletion dt=0.04.
  Clear runs BeforeApp and must stop App in that same snapshot. Tests assert
  one mutation, zero later deliveries, null executing payload fields, invalid
  body/fixture handles, and preserved world/subscriptions or expired World token.
- `clear-world-begin`, `delete-world-begin`, `delete-classic-begin`,
  `clear-classic-begin/stay/exit`, `clear-mod-begin/stay/exit` and
  `delete-mod-begin/stay/exit` extend cancellation across phases and layers
  (replace slash notation with the individual phase).
- `raw-destroy-unowned-active-exit-reuse` destroys an ownerless, already
  separated body inside End, then asserts its distant replacement reused the
  same address. The executing and later payloads cannot acquire the replacement.
  Compound and queued-record variants assert counts and no destruction EndContact.
- `three-raw-steps-recontact` asserts no early callbacks and exactly
  Begin/End/Begin/Stay on a zero-dt update, with single and compound fixtures.
- `raw-steps-inside-listener` calls three raw Steps inside a managed Begin
  callback and inside an Immediate wrapper's Exit callback. Its Begin/End/Begin
  and first Stay must wait for the next update, even with more managed sub-steps
  in the initiating update; wrapper eligibility cannot leak into raw calls.
- `fixture-sensor` retires the collider's borrowed fixture and reuses its
  address on another body. Body setter, collider setter and changed-flag sync
  must affect only the owner's remaining live fixtures and keep the cache.
- `fixtures` and `fixtures-owned` remove the last fixture before a raw
  fixtureless DestroyBody/reallocation. Unowned, classic and Mod variants cover
  queued Begin, delivered Begin, already-separated End and active Stay. Assert
  reused body/fixture addresses, immediate payload nulling, null later Exit,
  detached owner handles, exact counts and no SayGoodbye hook. The separated-End
  case runs `update()` between fixture removal and body destruction; it does
  not cover silent last-fixture/body destruction before update. Owned raw-release
  cases are defensive checks outside the supported ownership contract.
- Review round 2 (adversarial): `partial` runs `partial-ownerless`,
  `partial-classic` and `partial-mod`. Each removes the initial pair witness
  from two overlapping sensor fixtures with Begin delivered, queued, executing,
  or during executing Stay. Assert one remaining touching contact, one Begin,
  continuing Stay at both listener priorities and the owner layer, preserved
  executing payloads, and one final Exit with both live body pointers. These
  tests also run in the full suite. The unobserved `separated` destruction/reuse
  path is excluded by auditor decision (a) in REPORT.md; no stamp table is added.
- Review round 3 (adversarial): `inactive` adds a non-touching fixture at the
  fixture-list head, removes the original witness, then removes the idle
  fixture with Begin delivered, queued, executing, during Stay, or inside End.
  Both listener priorities and owner delivery must retain the live pair;
  separation produces exactly one Exit with both bodies. `revalidate` lets
  the original witness stop touching before removing it without EndContact,
  checking delivery-time rebinding while another fixture still touches.
  `mixed` removes three touching and three idle fixtures one by one, starting
  with delivered/queued/executing Begin or executing Stay, with either a
  touching or idle fixture removed last. Every removal checks exact world and
  owner counts; the final touching removal closes the endpoint and gives one
  survivor-only Exit, with no further Stay or duplicate Begin/Exit.
  All three groups run for ownerless, classic and Mod A (51 scenarios,
  684 assertions), in the full suite and under `--delivery r3`. Individual
  groups accept `-ownerless`, `-classic` and `-mod` suffixes. Owned raw-fixture
  removal remains a defensive check outside the supported public contract.
- `sensor-raw` and `sensor-managed` observe solid then sensor contributors to
  one continuously touching pair. The first Begin must remain collision Enter
  (1 collision, 0 trigger), while the later Stay uses trigger routing.
- `substeps`, `recorded-order`, `listener-releases`, `setup-caller-deletion`,
  `destruction-forwarding`, `triggers`, `router-gates` and
  `classic-source-deletion` cover ordered unlocked delivery,
  once-per-update Stay, frame deduplication, self/other/both release at all
  three layers/phases, callback-return Exit barriers, forwarding through
  clear/setup, raw owned destruction, and classic flag/setter/fixture behavior.

The original compound seam test now drives managed `World::update()`; raw
Steps deliberately have no fixture hand-over suppression. The reentrant shape
setup tests use a surviving-side Exit, since a released owner receives no
further notification. Exact mass comparisons and clear/unspent-time tests remain.

Run the full suite in Debug and Release and under ASan+UBSan. Repeat `--delivery`,
`--owner-lifetimes` and `--contact-lifetimes` 50 times under sanitizers. The
last selector runs the existing compound aggregation/seam and classic/Mod/world
contact-lifetime tests without unrelated polygon-reduction work.
Use `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` (leak detection disabled).
Windows, MacBook Air and Raspberry Pi still require the suite and a physics
example that starts and exits cleanly.


World member router (#842 PR 3): `--delivery router-gates` covers Mod pairs in
independent Worlds (including nested updates), clearing/deleting a World inside
routed Enter/Stay/Exit, address reuse during suspended delivery, lazy subscription
order and exactly one subscription through clear/setup. Compile-time assertions
reject move construction/assignment of World, Body and all three classic shapes.

Selected runs fail with `unknown case` if no checks ran. Verify unknown and known
selectors with `python3 test_selectors.py bin/tests` from this directory (use the
appropriate executable path on each platform).
