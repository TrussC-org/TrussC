// Headless regression test for lighting state lifetime (#269).
// No rendering or GPU setup is needed: environments remain unloaded.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

using namespace std;
using namespace tc;

namespace {

// The standalone ASan test reproduces a sketch's pre-main lighting objects.
// Leave them registered on return from main so their destructors run AFTER
// mainWindowContext()/the window registry have been destroyed.
#ifndef TC_CORE_TEST_NAME
Material staticMaterial;
Environment staticEnvironment;
Light staticLight;
#endif

int failures = 0;

void check(const char* name, bool ok) {
    printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

static_assert(is_copy_constructible_v<Material>);
static_assert(is_copy_assignable_v<Material>);
static_assert(is_nothrow_move_constructible_v<Material>);
static_assert(is_nothrow_move_assignable_v<Material>);

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#ifdef TC_CORE_TEST_NAME
    // Preserve the same construction/destruction order in the combined runner
    // without adding lighting destructors to unrelated tests' processes.
    static Material staticMaterial;
    static Environment staticEnvironment;
    static Light staticLight;
#endif
    const bool registryFirst = argc > 1 && strcmp(argv[1], "--registry-first") == 0;
    // A null entry is ignored by openWindows(), but allocates registry storage
    // so ASan can detect a late scan even for Material/Environment alone.
    // Also allow reversing which of the two storages is destroyed first.
    if (registryFirst) internal::registerWindow(nullptr);
    auto& ctx = internal::mainWindowContext();
    clearMaterial();
    clearEnvironment();
    clearLights();

    {
        Material material;
        setMaterial(material);
        check("setMaterial registers the material", ctx.currentMaterial == &material);
    }
    check("destroyed Material is detached", ctx.currentMaterial == nullptr);

    {
        Material active;
        setMaterial(active);
        { Material unrelated; }
        check("unrelated Material destruction preserves active material",
              ctx.currentMaterial == &active);

        // Defaulted copies/moves preserve value semantics and do not retarget
        // a registered source. Destroying any destination must not clear it.
        active.setMetallic(0.75f);
        {
            Material copied(active);
            Material assigned;
            assigned = active;
            check("Material copy construction/assignment preserve parameters",
                  copied.getMetallic() == 0.75f && assigned.getMetallic() == 0.75f);
            Material moved(std::move(active));
            Material moveAssigned;
            moveAssigned = std::move(active);
            check("Material move construction/assignment preserve parameters",
                  moved.getMetallic() == 0.75f && moveAssigned.getMetallic() == 0.75f);
            check("moving the active Material leaves its source registered",
                  ctx.currentMaterial == &active);
        }
        check("destroying Material copies/moves preserves registered source",
              ctx.currentMaterial == &active);
    }
    check("moved-from Material is detached on destruction", ctx.currentMaterial == nullptr);

    {
        Environment environment;
        setEnvironment(environment);
        check("setEnvironment registers the environment", getEnvironment() == &environment);
        check("default Environment is unloaded", !environment.isLoaded());
    }
    check("destroyed Environment is detached", getEnvironment() == nullptr);

    {
        Environment active;
        setEnvironment(active);
        { Environment unrelated; }
        check("unrelated Environment destruction preserves active environment",
              getEnvironment() == &active);
    }
    check("active Environment is detached on destruction", getEnvironment() == nullptr);

    {
        Light active;
        addLight(active);
        { Light unrelated; }
        check("unrelated Light destruction preserves registered light",
              ctx.activeLights.size() == 1 && ctx.activeLights.front() == &active);
        {
            Light other;
            addLight(other);
            check("two lights are registered", getNumLights() == 2);
        }
        check("destroyed Light is removed without removing another light",
              ctx.activeLights.size() == 1 && ctx.activeLights.front() == &active);
    }
    check("destroyed last Light is removed", getNumLights() == 0);

    if (!registryFirst) internal::registerWindow(nullptr);
    setMaterial(staticMaterial);
    setEnvironment(staticEnvironment);
    addLight(staticLight);
    check("static Material remains registered for normal exit",
          ctx.currentMaterial == &staticMaterial);
    check("static Environment remains registered for normal exit",
          ctx.currentEnvironment == &staticEnvironment);
    check("static Light remains registered for normal exit",
          ctx.activeLights.size() == 1 && ctx.activeLights.front() == &staticLight);

    printf("lightingStateLifetime: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
