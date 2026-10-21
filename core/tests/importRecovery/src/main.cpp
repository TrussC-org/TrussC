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
    string name = string(100, 'x');
    int passes = 0, getterCalls = 0;
    bool failCommit = false, failGetter = false;
    const string& getName() {
        ++getterCalls;
        if (failGetter) failNext = true;
        return name;
    }
    void setName(const string& value) { name = value; }
    TC_REFLECT_ROOT(Settings) {
        if (++_tc_self.passes == 2 && _tc_self.failCommit) failNext = true;
        TC_VALUE(count) TC_VALUE(name, getName, setName)
    }
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
    if (armSourceBytes && size == armSourceBytes) {
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
    const auto source = Json::parse(R"({"count":99,"name":"new"})");
    if (mode == "all" || mode == "commit") {
        Settings settings;
        settings.failCommit = true;
        const auto result = reflectFromJson(settings, source);
        failAll = false;
        failNext = false;
        check("a rejected commit leaves both fields unchanged", !internal::JsonReadResultAccess::rejected(result) || (settings.count == 1 && settings.name == string(100, 'x')));
        check("commit uses staged getter values without allocating", failedRequests == 0 && settings.count == 99 && settings.name == "new" && settings.getterCalls == 1 && !result.applied.empty());
        Settings rejected;
        rejected.failGetter = true;
        const auto rejectedResult = reflectFromJson(rejected, source);
        failNext = false;
        check("getter staging failure changes no fields", rejected.count == 1 && rejected.name == string(100, 'x') && rejectedResult.applied.empty() && internal::JsonReadResultAccess::rejected(rejectedResult));
    }
    if (mode == "all" || mode == "reflect") {
        Settings exhausted;
        bool returned = false, rejectedFlag = false;
        failAll = true;
        try {
            auto r = reflectFromJson(exhausted, source);
            rejectedFlag = internal::JsonReadResultAccess::rejected(r);
            returned = r.applied.empty();
        } catch (...) {}
        failAll = false;
        check("reflection returns rejection under persistent exhaustion", returned && rejectedFlag && exhausted.count == 1 && exhausted.name == string(100, 'x'));
        bool fallbackReturned = false;
        failAll = true;
        try {
            JsonReadReflector empty{Json()};
            internal::JsonReadResultAccess::reject(empty);
            fallbackReturned = internal::JsonReadResultAccess::rejected(empty) && empty.applied.empty() && empty.unknownKeys().empty();
        } catch (...) {}
        failAll = false;
        check("empty rejected reader needs no allocation without iterator debugging", fallbackReturned);
        JsonReadReflector reader(source);
        failAll = true;
        exhausted.reflectMembers(reader);
        failAll = false;
        check("reader recovery hook survives persistent exhaustion", reader.applied.empty() && internal::JsonReadResultAccess::rejected(reader) && exhausted.count == 1);
    }
    if (mode == "all" || mode == "root") {
        for (const char* text : {"17", "[]"}) {
            Settings settings;
            JsonReadReflector reader(Json::parse(text));
            bool returned = false;
            failAll = true;
            try { settings.reflectMembers(reader); returned = true; } catch (...) {}
            failAll = false;
            check("direct invalid-root entry rejects under persistent exhaustion",
                  returned && internal::JsonReadResultAccess::rejected(reader) &&
                  reader.applied.empty() && settings.count == 1 && settings.passes == 0);
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
    if (mode == "all" || mode == "font") {
        auto font = makeFont();
        internal::FontAtlasManager manager;
        bool fontResult = true;
        bool returned = false;
        failAll = true;
        try { fontResult = manager.setupFromMemory(font.data(), font.size(), 16); returned = true; } catch (...) {}
        failAll = false;
        check("font returns failure under persistent exhaustion", returned && !fontResult);
    }
    if (mode == "all" || mode == "font-init") {
        auto font = makeFont();
        for (int budget = 1; budget <= 3; ++budget) {
            internal::FontAtlasManager manager;
            bool returned = false, loaded = true;
            remaining = budget;
            try { loaded = manager.setupFromMemory(font.data(), font.size(), 16); returned = true; } catch (...) {}
            remaining = -1;
            check("font memory initialization recovers after the source copy", returned && !loaded && manager.getAtlasCount() == 0);
            check("rejected font source capacity is released", manager.getFontDataCapacityForTests() == 0);
            check("font cleanup resets metrics", manager.getFontSize() == 0 && manager.getLineHeight() == 0);
            check("font manager is reusable after failed initialization", manager.setupFromMemory(font.data(), font.size(), 16));
        }
    }
    if (mode == "all" || mode == "font-reason") {
        auto font = makeFont();
        font[0] = 0xfe;
        internal::FontAtlasManager manager;
        bool returned = false, loaded = true;
        remaining = 2; // source and name succeed; validation diagnostics fail
        try { loaded = manager.setupFromMemory(font.data(), font.size(), 16); returned = true; } catch (...) {}
        remaining = -1;
        check("font validation-reason exhaustion returns failure", returned && !loaded && manager.getAtlasCount() == 0);
    }
    if (mode == "all" || mode == "font-file" || mode == "font-async") {
        auto font = makeFont();
        const auto path = fs::current_path() / ".font-init-exhaustion.ttf";
        { ofstream file(path, ios::binary); file.write((const char*)font.data(), font.size()); }
        const string filename = path.string();
        if (mode != "font-async") {
            for (int budget = 0; budget < 3; ++budget) {
                internal::FontAtlasManager manager;
                bool returned = false, loaded = true;
                armSourceBytes = font.size(); sourceBudget = budget;
                try { loaded = manager.prepareFile(filename, 16, 0, 4096); returned = true; } catch (...) {}
                remaining = -1; armSourceBytes = 0;
                check("font file initialization recovers after the source read", returned && !loaded && manager.getAtlasCount() == 0);
            }
        }
        if (mode != "font-file") {
            internal::appRunToken() = make_shared<atomic<bool>>(true);
            for (int budget = 0; budget < 3; ++budget) {
                Font async;
                bool delivered = false, rejected = false;
                atomic<bool> faultReached{false};
                auto listener = async.onLoad.listen([&](LoadResult& result) {
                    delivered = true;
                    rejected = !result && result.error == LoadError::DecodeFailed && !async.isLoaded() && !async.isLoading();
                });
                internal::setResourceLoadProbeForTests([&](const char* stage, const fs::path&) {
                    if (strcmp(stage, "font.decode") == 0) {
                        armSourceBytes = font.size(); sourceBudget = budget; failedRequests = 0;
                    } else if (strcmp(stage, "font.ready") == 0) {
                        faultReached = failedRequests > 0;
                    } else if (strcmp(stage, "font.queued") == 0) {
                        // Keep exhaustion active through mailbox publication.
                        remaining = -1; armSourceBytes = 0;
                    }
                });
                async.loadAsync(path, 16 + budget);
                internal::waitForResourceLoadsForTests();
                internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
#ifdef __EMSCRIPTEN__
                internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
#endif
                internal::setResourceLoadProbeForTests({});
                check("async font reports failure after persistent init exhaustion", faultReached && delivered && rejected);
            }
        }
        fs::remove(path);
    }
    return failures ? 1 : 0;
}
