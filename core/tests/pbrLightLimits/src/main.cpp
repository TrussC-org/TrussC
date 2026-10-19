// =============================================================================
// core/tests/pbrLightLimits — regression test for the PBR light limits (#333).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariants:
//   - addLight() registers up to internal::maxLights (8) lights; a further
//     light is not registered and logs exactly one warning, however many
//     more are added. Adding an already registered light on a full list
//     changes nothing and logs nothing.
//   - internal::selectPbrSpecialLightSlots() gives the single projector slot
//     to the first Spot light with a projection texture and the single IES
//     slot to the first light with an IES profile, among the first maxLights
//     lights, and reports when a further light of either kind gets no slot.
//     The PBR draw warns once from those flags (GPU path; not run here).
//
// The Texture / IesProfile objects are default-constructed and only used as
// non-null pointers: nothing is uploaded, no GPU is needed.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-70s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_fail;
}

static vector<string> g_warnings;

static int countWarnings(const string& needle) {
    int n = 0;
    for (auto& m : g_warnings) {
        if (m.find(needle) != string::npos) ++n;
    }
    return n;
}

} // namespace

TC_CORE_TEST_MAIN() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning) g_warnings.push_back(e.message);
    });

    // --- addLight(): 8-light cap ------------------------------------------------
    {
        clearLights();
        vector<Light> lights(12);
        for (int i = 0; i < internal::maxLights; ++i) addLight(lights[i]);
        check("addLight: 8 lights are registered", getNumLights() == internal::maxLights);
        check("addLight: no warning up to the limit", countWarnings("addLight") == 0);

        addLight(lights[8]);
        check("addLight: the 9th light is not registered", getNumLights() == internal::maxLights);
        check("addLight: the 9th light logs one warning", countWarnings("addLight") == 1);

        addLight(lights[9]);
        addLight(lights[10]);
        addLight(lights[11]);
        check("addLight: further lights log no more warnings", countWarnings("addLight") == 1);

        addLight(lights[0]);
        addLight(lights[7]);
        check("addLight: re-adding a registered light on a full list is silent",
              countWarnings("addLight") == 1 && getNumLights() == internal::maxLights);

        removeLight(lights[3]);
        addLight(lights[3]);
        check("addLight: a freed slot can be used again", getNumLights() == internal::maxLights);
        clearLights();
    }

    // --- projector / IES slot selection -----------------------------------------
    Texture texA, texB;        // used only as non-null pointers
    IesProfile iesA, iesB;     // used only as non-null pointers
    using internal::selectPbrSpecialLightSlots;
    {
        vector<Light*> none;
        auto s = selectPbrSpecialLightSlots(none);
        check("slots: empty list -> no slots, no overflow",
              s.projectorIndex == -1 && s.iesIndex == -1 &&
              !s.projectorOverflow && !s.iesOverflow);
    }
    {
        Light plain, spot;
        plain.setPoint(0, 0, 0);
        spot.setSpot(0, 0, 0, 0, -1, 0);
        vector<Light*> v{&plain, &spot};
        auto s = selectPbrSpecialLightSlots(v);
        check("slots: lights without texture/profile -> no slots",
              s.projectorIndex == -1 && s.iesIndex == -1 &&
              !s.projectorOverflow && !s.iesOverflow);
    }
    {
        Light plain, proj, ies;
        plain.setPoint(0, 0, 0);
        proj.setSpot(0, 0, 0, 0, -1, 0);
        proj.setProjectionTexture(&texA);
        ies.setPoint(1, 0, 0);
        ies.setIesProfile(&iesA);
        vector<Light*> v{&plain, &proj, &ies};
        auto s = selectPbrSpecialLightSlots(v);
        check("slots: one projector and one IES light get their slots",
              s.projectorIndex == 1 && s.iesIndex == 2 &&
              !s.projectorOverflow && !s.iesOverflow);
    }
    {
        // A projection texture on a non-Spot light is not a projector.
        Light pointWithTex;
        pointWithTex.setPoint(0, 0, 0);
        pointWithTex.setProjectionTexture(&texA);
        vector<Light*> v{&pointWithTex};
        auto s = selectPbrSpecialLightSlots(v);
        check("slots: a Point light with a texture is not a projector",
              s.projectorIndex == -1 && !s.projectorOverflow);
    }
    {
        Light p1, p2, i1, i2;
        p1.setSpot(0, 0, 0, 0, -1, 0);
        p1.setProjectionTexture(&texA);
        p2.setSpot(1, 0, 0, 0, -1, 0);
        p2.setProjectionTexture(&texB);
        i1.setPoint(0, 1, 0);
        i1.setIesProfile(&iesA);
        i2.setPoint(0, 2, 0);
        i2.setIesProfile(&iesB);
        vector<Light*> v{&i1, &p1, &i2, &p2};
        auto s = selectPbrSpecialLightSlots(v);
        check("slots: two projectors -> first gets the slot, overflow set",
              s.projectorIndex == 1 && s.projectorOverflow);
        check("slots: two IES lights -> first gets the slot, overflow set",
              s.iesIndex == 0 && s.iesOverflow);
    }
    {
        // A projector Spot light that also has an IES profile takes both slots.
        Light both;
        both.setSpot(0, 0, 0, 0, -1, 0);
        both.setProjectionTexture(&texA);
        both.setIesProfile(&iesA);
        vector<Light*> v{&both};
        auto s = selectPbrSpecialLightSlots(v);
        check("slots: one light with texture and profile takes both slots",
              s.projectorIndex == 0 && s.iesIndex == 0 &&
              !s.projectorOverflow && !s.iesOverflow);
    }
    {
        // Only the first maxLights entries reach the shader.
        vector<Light> plain(internal::maxLights);
        Light proj;
        proj.setSpot(0, 0, 0, 0, -1, 0);
        proj.setProjectionTexture(&texA);
        vector<Light*> v;
        for (auto& l : plain) v.push_back(&l);
        v.push_back(&proj);
        auto s = selectPbrSpecialLightSlots(v);
        check("slots: lights past maxLights are not considered",
              s.projectorIndex == -1 && !s.projectorOverflow);
    }

    printf("%s\n", g_fail == 0 ? "pbrLightLimits: all passed" : "pbrLightLimits: FAILED");
    return g_fail ? 1 : 0;
}
