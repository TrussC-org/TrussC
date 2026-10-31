#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <cstdlib>
#include <new>
using namespace std;
using namespace tc;
namespace {
thread_local bool failAll = false, failNext = false;
thread_local size_t failedRequests = 0;
thread_local int remaining = -1;
thread_local bool failBake = false;
thread_local size_t armSourceBytes = 0;
thread_local int sourceBudget = 0;
int failures = 0;
void check(const char* name, bool ok) {
    printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
#include "../../asyncLoads/src/fontFixture.h"
struct Settings {
    int count = 1;
    TC_REFLECT_ROOT(Settings) { TC_VALUE(count) }
};
struct IesRecoveryApp : App {
    void setup() override {
        const string previous = "TILT=NONE\n1 1 1 1 1 1 1 1 1 1 1 1 1\n0 0 21\n";
        const string replacement = "TILT=NONE\n1 1 2 1 1 1 1 1 1 1 1 1 1\n0 0 42\n";
        const auto path = fs::current_path() / ".ies-loaded-exhaustion.ies";
        { ofstream file(path, ios::binary); file << replacement; }
        IesProfile profile;
        check("IES control creates a valid GPU profile", profile.loadFromString(previous) && profile.isLoaded());
        const auto view = profile.getView().id, sampler = profile.getSampler().id;
        for (bool fromFile : {false, true}) {
            bool returned = false, loaded = true;
            failBake = true;
            try { loaded = fromFile ? profile.load(path) : profile.loadFromString(replacement); returned = true; } catch (...) {}
            failBake = false; failAll = false;
            check(fromFile ? "IES file exhaustion preserves loaded profile and GPU handles" :
                             "IES string exhaustion preserves loaded profile and GPU handles",
                  returned && !loaded && profile.isLoaded() && profile.getMaxCandela() == 21 &&
                  profile.getTextureWidth() == 256 && profile.getView().id == view &&
                  profile.getSampler().id == sampler);
        }
        check("IES invalid replacement also preserves loaded profile", !profile.loadFromString("invalid") &&
              profile.isLoaded() && profile.getMaxCandela() == 21 && profile.getView().id == view);
        check("IES profile can be replaced after exhaustion", profile.loadFromString(replacement) &&
              profile.isLoaded() && profile.getMaxCandela() == 84);
        const auto replacementView = profile.getView().id;
        IesProfile moved(std::move(profile));
        IesProfile assigned;
        assigned = std::move(moved);
        check("IES prepared resource ownership survives both moves", assigned.isLoaded() &&
              assigned.getView().id == replacementView && !profile.isLoaded() && !moved.isLoaded());
        fs::remove(path);
        exitApp();
    }
};

}
void* operator new(size_t size) {
    if (failBake && size == 1024) { failBake = false; failAll = true; }
    if (remaining == 0 || failAll || failNext) { failNext = false; ++failedRequests; throw bad_alloc(); }
    if (remaining > 0) --remaining;
    // MSVC's allocator may add alignment overhead to source blocks >= 4096 bytes.
    if (armSourceBytes && size >= armSourceBytes && size - armSourceBytes <= 64) {
        armSourceBytes = 0;
        remaining = sourceBudget;
    }
    if (void* p = malloc(size ? size : 1)) return p;
    throw bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

TC_CORE_TEST_MAIN(int argc, char** argv) {
    const string mode = argc > 1 ? argv[1] : "all";
    set_terminate([] { fputs("FAIL: terminated during import recovery\n", stderr); _Exit(90); });
    getLogger();
    if (mode == "ies-display") {
        WindowSettings settings;
        settings.setSize(32, 32);
        settings.setHighDpi(false);
        return tcCoreTest::runDisplayTest<IesRecoveryApp>(settings, failures);
    }
    if (mode == "all" || mode == "reflect") {
        for (bool parsed : {false, true}) {
            Settings settings;
            const Json input = {{"count", 99}};
            const Json source = parsed ? Json::parse(input.dump()) : input;
            bool propagated = false;
            failNext = true;
            try { (void)reflectFromJson(settings, source); }
            catch (const bad_alloc&) { propagated = true; }
            failNext = false;
            check("reflection allocation failure propagates for all JSON", propagated && settings.count == 1);
        }
    }
    if (mode == "all" || mode == "ies") {
        const string valid = "TILT=NONE\n1 1 2 1 1 1 1 1 1 1 1 1 1\n0 0 42\n";
        const auto path = fs::current_path() / ".ies-bake-exhaustion.ies";
        { ofstream file(path, ios::binary); file << valid; }
        for (bool fromFile : {false, true}) {
            IesProfile profile;
            bool returned = false, loaded = true;
            failedRequests = 0;
            failBake = true;
            try { loaded = fromFile ? profile.load(path) : profile.loadFromString(valid); returned = true; } catch (...) {}
            failBake = false; failAll = false;
            check(fromFile ? "IES file bake failure preserves the previous profile" :
                             "IES string bake failure preserves the previous profile",
                  returned && !loaded && failedRequests > 0 && !profile.isLoaded() &&
                  profile.getTextureWidth() == 0 && profile.getMaxCandela() == 0 &&
                  profile.getView().id == 0 && profile.getSampler().id == 0);
        }
        fs::remove(path);
    }
    if (mode == "all" || mode == "documents") {
        const auto path = fs::current_path() / ".document-entry-exhaustion.json";
        { ofstream file(path, ios::binary); file << "{}"; }
        Xml xml;
        xml.parse("<previous/>");
        bool jsonReturned = false, xmlReturned = false, xmlLoaded = true;
        failedRequests = 0;
        failAll = true;
        try { jsonReturned = loadJson(path).is_null(); } catch (...) {}
        failAll = false;
        check("JSON file entry recovers from the first persistent allocation failure",
              jsonReturned && failedRequests > 0);
        failedRequests = 0;
        failAll = true;
        try { xmlLoaded = xml.load(path); xmlReturned = true; } catch (...) {}
        failAll = false;
        check("XML file entry recovers and preserves the previous document",
              xmlReturned && !xmlLoaded && failedRequests > 0 && xml.child("previous"));
        fs::remove(path);
    }
    if ((mode == "all" || mode == "font") &&
        !tcTest::skipPersistentAllocationFailure("font persistent initialization")) {
        auto font = makeFont();
        internal::FontAtlasManager manager;
        bool fontResult = true;
        bool returned = false;
        failAll = true;
        try { fontResult = manager.setupFromMemory(font.data(), font.size(), 16); returned = true; } catch (...) {}
        failAll = false;
        check("font returns failure under persistent exhaustion", returned && !fontResult);
    }
    if ((mode == "all" || mode == "font-init") &&
        !tcTest::skipPersistentAllocationFailure("font-init persistent initialization")) {
        auto font = makeFont();
        bool sawFailure = false, sawSuccess = false;
        // Sweep allocations after the source copy until a fault-free load.
        // The stopping budget depends on the STL, not on the test fixture.
        for (int budget = 0; ; ++budget) {
            internal::FontAtlasManager manager;
            bool returned = false, loaded = true;
            failedRequests = 0;
            armSourceBytes = font.size(); sourceBudget = budget;
            try { loaded = manager.setupFromMemory(font.data(), font.size(), 16); returned = true; } catch (...) {}
            remaining = -1; armSourceBytes = 0;
            if (failedRequests > 0) {
                sawFailure = true;
                check("font memory initialization recovers after the source copy", returned && !loaded && manager.getAtlasCount() == 0);
                check("rejected font source capacity is released", manager.getFontDataCapacityForTests() == 0);
                check("font cleanup resets metrics", manager.getFontSize() == 0 && manager.getLineHeight() == 0);
                check("font manager is reusable after failed initialization", manager.setupFromMemory(font.data(), font.size(), 16));
            } else {
                sawSuccess = returned && loaded;
                break;
            }
        }
        check("font memory budgets exercise both failure and success", sawFailure && sawSuccess);
    }
    if ((mode == "all" || mode == "font-reason") &&
        !tcTest::skipPersistentAllocationFailure("font-reason persistent initialization")) {
        auto font = makeFont();
        font[0] = 0xfe;
        bool sawFailure = false, sawRejection = false;
        // Include name/diagnostic allocations without assuming their indices.
        for (int budget = 0; ; ++budget) {
            internal::FontAtlasManager manager;
            bool returned = false, loaded = true;
            failedRequests = 0;
            armSourceBytes = font.size(); sourceBudget = budget;
            try { loaded = manager.setupFromMemory(font.data(), font.size(), 16); returned = true; } catch (...) {}
            remaining = -1; armSourceBytes = 0;
            if (failedRequests > 0) {
                sawFailure = true;
                check("font validation-reason exhaustion returns failure", returned && !loaded && manager.getAtlasCount() == 0);
            } else {
                sawRejection = returned && !loaded && manager.getAtlasCount() == 0;
                break;
            }
        }
        check("font reason budgets exercise exhaustion and ordinary rejection", sawFailure && sawRejection);
    }
    if ((mode == "all" || mode == "font-file") &&
        !tcTest::skipPersistentAllocationFailure("font file persistent initialization")) {
        auto font = makeFont();
        const auto path = fs::current_path() / ".font-init-exhaustion.ttf";
        { ofstream file(path, ios::binary); file.write((const char*)font.data(), font.size()); }
        const string filename = path.string();
        bool sawFailure = false, sawSuccess = false;
        for (int budget = 0; ; ++budget) {
            internal::FontAtlasManager manager;
            bool returned = false, loaded = true;
            failedRequests = 0;
            armSourceBytes = font.size(); sourceBudget = budget;
            try { loaded = manager.prepareFile(filename, 16, 0, 4096); returned = true; } catch (...) {}
            remaining = -1; armSourceBytes = 0;
            if (failedRequests > 0) {
                sawFailure = true;
                check("font file initialization recovers after the source read", returned && !loaded && manager.getAtlasCount() == 0);
            } else {
                sawSuccess = returned && loaded;
                break;
            }
        }
        check("font file budgets exercise both failure and success", sawFailure && sawSuccess);
        fs::remove(path);
    }
    return failures ? 1 : 0;
}
