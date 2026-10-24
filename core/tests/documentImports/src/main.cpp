#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "impl/tcFontScratch.h"
#include <cstdio>
#include <limits>

using namespace std;
using namespace tc;

namespace {
#include "../../asyncLoads/src/fontFixture.h"
int failures = 0;
pugi::allocation_function originalXmlAllocate = nullptr;
bool failXmlAllocation = false;
void check(const char* label, bool ok) {
    printf("%-72s %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
struct NestedSettings {
    float gain = 1;
    TC_REFLECT_ROOT(NestedSettings) { TC_VALUE(gain) }
};
enum class Selection { First, Second };
TC_ENUM_LABELS(Selection, "First", "Second")
struct CountOnlySettings {
    int count = 1;
    TC_REFLECT_ROOT(CountOnlySettings) { TC_VALUE(count) }
};
struct Settings {
    int count = 1;
    bool active = false;
    string name = "before";
    Vec2 position{1, 2};
    Color color{1, 1, 1, 1};
    NestedSettings nested;
    Selection choice = Selection::First;
    int setterCalls = 0;
    float gain = 1;
    float getGain() const { return gain; }
    void setGain(float value) { gain = value; ++setterCalls; }
    TC_REFLECT_ROOT(Settings) {
        TC_VALUE(count) TC_VALUE(active) TC_VALUE(name) TC_VALUE(position)
        TC_VALUE(color) TC_VALUE(nested) TC_VALUE(choice) TC_VALUE(gain, getGain, setGain)
    }
};
string jsonNesting(int depth) { return string(size_t(depth), '[') + "0" + string(size_t(depth), ']'); }
string xmlNesting(int depth) {
    string text;
    for (int i = 0; i < depth; ++i) text += "<n>";
    for (int i = 0; i < depth; ++i) text += "</n>";
    return text;
}
}

TC_CORE_TEST_MAIN() {
    int notices = 0, warnings = 0;
    string lastError;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Notice) ++notices;
        if (event.level == LogLevel::Warning) ++warnings;
        if (event.level == LogLevel::Error) lastError = event.message;
    });
    check("JSON nesting at 64 succeeds", !Json::parse(jsonNesting(64), nullptr, false).is_discarded());
    check("JSON nesting beyond 64 fails as a whole", Json::parse(jsonNesting(65), nullptr, false).is_discarded());
    check("JSON callback path enforces nesting", Json::parse(jsonNesting(65), [](int, Json::parse_event_t, Json&) { return true; }, false).is_discarded());
    const string nulJson = string("{\"count\":42}") + char(0) + "not-json";
    check("JSON raw NUL rejects the whole string", parseJson(nulJson).is_null());
    check("JSON trailing raw NUL rejects the document", parseJson(string("{}") + char(0)).is_null());
    check("JSON escaped NUL remains valid", parseJson(R"({"name":"\u0000"})")["name"] == string(1, char(0)));
    Xml xml;
    check("XML nesting at 64 succeeds", xml.parse(xmlNesting(64)));
    check("XML nesting beyond 64 preserves previous document", !xml.parse(xmlNesting(65)) && !xml.empty());
    check("XML syntax failure preserves previous document", !xml.parse("<broken>") && string(xml.root().name()) == "n");
    check("XML internal terminator rejects the whole input", !xml.parse(string("<n/>\0<tail/>", 12)) && string(xml.root().name()) == "n");
    originalXmlAllocate = pugi::get_memory_allocation_function();
    const auto originalXmlFree = pugi::get_memory_deallocation_function();
    pugi::set_memory_management_functions([](size_t bytes) -> void* {
        if (failXmlAllocation) { failXmlAllocation = false; return nullptr; }
        return originalXmlAllocate(bytes);
    }, originalXmlFree);
    failXmlAllocation = true;
    const bool xmlAllocationResult = xml.parse("<replacement><value/></replacement>");
    pugi::set_memory_management_functions(originalXmlAllocate, originalXmlFree);
    check("XML allocation failure preserves previous document", !xmlAllocationResult && string(xml.root().name()) == "n");

    JsonReadReflector directString(Json::parse(R"({"name":"new"})"));
    string directValue;
    check("direct string visitor reads parsed JSON safely", directString.visit("name", directValue) && directValue == "new" && directString.applied.size() == 1 && directString.unknownKeys().empty());

    Settings settings;
    auto failed = reflectFromJson(settings, parseJson(R"({"count":7,"active":"bad","gain":2})"));
    check("external update validates before changing any field or setter", settings.count == 1 && settings.setterCalls == 0 && failed.applied.empty());
    check("external failure names the invalid field", lastError.find("active") != string::npos && !failed.skipped.empty());
    auto applied = reflectFromJson(settings, parseJson(R"({"count":7,"active":true,"gain":2,"nested":{"gain":3},"choice":"Second"})"));
    check("valid external settings commit through setters once", settings.count == 7 && settings.active && settings.gain == 2 && settings.setterCalls == 1 && settings.nested.gain == 3 && settings.choice == Selection::Second && !applied.applied.empty());
    reflectFromJson(settings, parseJson(R"({"count":8,"nested":{"gain":"bad"}})"));
    check("nested validation rejects entire external update", settings.count == 7 && settings.nested.gain == 3 && lastError.find("nested.gain") != string::npos);
    for (const char* source : {R"({"count":8,"nested":4})", R"({"count":8,"typo":1})", R"({"count":8,"nested":{"typo":1}})", R"({"count":2147483648})", R"({"count":1.5})", R"({"count":8,"position":[1,2,3]})", R"({"count":8,"color":[1,2,3,"bad"]})", R"({"count":8,"gain":1e100})", R"({"count":8,"choice":"Unknown"})"}) {
        auto rejected = reflectFromJson(settings, parseJson(source));
        check("invalid external field leaves all settings unchanged", settings.count == 7 && settings.setterCalls == 1 && rejected.applied.empty());
    }
    Json parsed = Json::parse(R"({"settings":{"count":8,"active":"bad"}})");
    Json copied = parsed["settings"];
    Json moved = std::move(copied);
    Json swapped;
    swapped.swap(moved);
    JsonReadReflector direct(swapped);
    settings.reflectMembers(direct);
    check("parsed child provenance survives copy move and swap", settings.count == 7 && direct.applied.empty());
    JsonReadReflector reusable(parseJson(R"({"count":11,"active":true})"));
    Settings firstTarget;
    firstTarget.reflectMembers(reusable);
    CountOnlySettings secondTarget;
    secondTarget.reflectMembers(reusable);
    check("reused external reader validates the current object's schema", firstTarget.count == 11 && secondTarget.count == 1 && internal::JsonReadResultAccess::rejected(reusable));
    auto permissive = reflectFromJson(settings, Json{{"count", 9}, {"active", "bad"}});
    check("app-constructed JSON retains partial-apply behavior", settings.count == 9 && !permissive.applied.empty() && !permissive.skipped.empty());

    const fs::path folder = fs::current_path() / ".document-import-test";
    fs::create_directories(folder);
    // Portable counterpart of filePath's Windows surrogate-name error check.
    // Keep the caller's spelling and legacy prefix, with no filesystem quoting.
    const fs::path brokenJson = folder / fs::path(u8"壊れた Café.json");
    { ofstream file(brokenJson, ios::binary); file << "{"; }
    check("JSON file parse error names the non-ASCII file with its legacy prefix",
          loadJson(brokenJson).is_null() && lastError.rfind("JSON parse error: ", 0) == 0 &&
          lastError.find(internal::pathToDisplayUtf8(brokenJson)) != string::npos);
    const auto oldDataRoot = getDataPathRoot();
    setDataPathRoot(folder);
    const fs::path relativeJson = brokenJson.filename();
    check("JSON file parse error retains the caller's relative path",
          loadJson(relativeJson).is_null() &&
          lastError.rfind("JSON parse error: " + internal::pathToDisplayUtf8(relativeJson) + " - ", 0) == 0);
    setDataPathRoot(oldDataRoot);
    const fs::path brokenXml = folder / fs::path(u8"壊れた Café.xml");
    { ofstream file(brokenXml, ios::binary); file << "<broken>"; }
    check("XML file error retains the path, parser description and offset",
          !xml.load(brokenXml) && lastError.rfind("XML load error: ", 0) == 0 &&
          lastError.find(internal::pathToDisplayUtf8(brokenXml)) != string::npos &&
          lastError.find(" - ") != string::npos && lastError.find(" (offset: ") != string::npos);
    const fs::path brokenIes = folder / fs::path(u8"壊れた Café.ies");
    { ofstream file(brokenIes, ios::binary); file << "invalid"; }
    IesProfile ies;
    check("IES file parser error names the non-ASCII file",
          !ies.load(brokenIes) && lastError.find(internal::pathToDisplayUtf8(brokenIes)) != string::npos);
    const fs::path brokenFont = folder / fs::path(u8"壊れた Café.ttf");
    { ofstream file(brokenFont, ios::binary); file << "invalid"; }
    internal::FontAtlasManager rejectedFont;
    check("font file validation error names the non-ASCII file",
          !rejectedFont.prepareFile(pathToUtf8(brokenFont), 16, 0, 4096) &&
          lastError.rfind("FontAtlasManager: not a usable font (", 0) == 0 &&
          lastError.find(internal::pathToDisplayUtf8(brokenFont)) != string::npos);
    const auto nulPath = folder / "nul.json";
    { ofstream file(nulPath, ios::binary); file.write(nulJson.data(), nulJson.size()); }
    check("JSON raw NUL rejects the complete local file", loadJson(nulPath).is_null());
    check("JSON raw NUL file error names the file", lastError.find(pathToUtf8(nulPath)) != string::npos);
    const fs::path jsonPath = folder / "large.json";
    const fs::path xmlPath = folder / "large.xml";
    const string payload(16u * 1024u * 1024u + 32, 'a');
    { ofstream file(jsonPath, ios::binary); file << "{\"name\":\"" << payload << "\"}"; }
    { ofstream file(xmlPath, ios::binary); file << "<root>" << payload << "</root>"; }
    const int noticeBefore = notices, warningBefore = warnings;
    Json largeJson = loadJson(jsonPath);
    check("local JSON above 16 MiB loads completely", largeJson.is_object() && largeJson["name"].get_ref<const string&>() == payload);
    reflectFromJson(settings, largeJson);
    check("settings loaded from local JSON commit completely", settings.name == payload);
    check("local XML above 16 MiB loads completely", xml.load(xmlPath) && string(xml.root().text().get()) == payload);
    check("large local inputs issue Notices without Warnings", notices >= noticeBefore + 2 && warnings == warningBefore);
    const fs::path utf16Path = folder / "utf16.xml";
    { ofstream file(utf16Path, ios::binary);
      file.put(char(0xff)); file.put(char(0xfe));
      for (char16_t unit : u16string(u"<root><value/></root>")) { file.put(char(unit & 255)); file.put(char(unit >> 8)); }
    }
    check("XML encoded-file conversion retains structural checks", xml.load(utf16Path) && string(xml.root().name()) == "root" && xml.root().child("value"));
    fs::remove_all(folder);

    auto font = makeFont();
    internal::FontAtlasManager manager;
    const bool fixtureLoaded = manager.setupFromMemory(font.data(), font.size(), 128);
    check("deterministic font fixture loads", fixtureLoaded);
    if (!fixtureLoaded) return 1;
    internal::failFontScratchAllocationAfterForTests(0);
    check("outline allocation failure rejects cleanly", manager.getGlyphPath('A').empty());
    check("failed outline requests no allocation", internal::fontScratchLargestAllocationForTests() == 0);
    const auto* glyph = manager.getOrLoadGlyph('A');
    check("raster allocation failure never publishes a valid glyph", glyph && !glyph->isValid());
    internal::failFontScratchAllocationAfterForTests(-1);
    manager.clearAtlas();
    glyph = manager.getOrLoadGlyph('A');
    check("glyph rendering recovers after allocation failure", glyph && glyph->isValid());
    {
        internal::FontScratchScope scratch(1, 1);
        check("scratch rejects expansion before a huge allocation", !internal::fontScratchMalloc(256u * 1024u * 1024u) && scratch.failed() && internal::fontScratchLargestAllocationForTests() < 256u * 1024u * 1024u);
    }
    stbtt_fontinfo fontInfo{};
    stbtt_InitFont(&fontInfo, font.data(), 0);
    const size_t firstGlyph = size_t(fontInfo.glyf);
    auto shortGlyph = font;
    shortGlyph[firstGlyph] = 0x7f;
    shortGlyph[firstGlyph + 1] = 0xff;
    check("font with an incomplete outline can load its directory", manager.setupFromMemory(shortGlyph.data(), shortGlyph.size(), 128));
    internal::failFontScratchAllocationAfterForTests(-1);
    check("incomplete contour records reject before outline allocation", manager.getGlyphPath('A').empty() && internal::fontScratchLargestAllocationForTests() == 0);
    auto compound = font;
    compound[firstGlyph] = compound[firstGlyph + 1] = 0xff;
    compound[firstGlyph + 10] = 0;
    compound[firstGlyph + 11] = 0x22;
    compound[firstGlyph + 12] = 0;
    compound[firstGlyph + 13] = 1;
    compound[firstGlyph + 14] = compound[firstGlyph + 15] = 0;
    compound[firstGlyph + 16] = 0; compound[firstGlyph + 17] = 2;
    compound[firstGlyph + 18] = 0; compound[firstGlyph + 19] = 1;
    compound[firstGlyph + 20] = compound[firstGlyph + 21] = 0;
    check("compound font directory loads", manager.setupFromMemory(compound.data(), compound.size(), 128));
    check("compound outline depth failure rejects cleanly", manager.getGlyphPath('A').empty());
    printf("%d failed\n", failures);
    return failures ? 1 : 0;
}
