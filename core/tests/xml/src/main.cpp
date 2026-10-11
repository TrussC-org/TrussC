// XML serialization and query contracts across pugixml updates.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <sstream>
#include <string>

using namespace std;
using namespace tc;

namespace {
int failures = 0;

void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
} // namespace

TC_CORE_TEST_MAIN() {
    Xml xml;
    auto root = xml.addRoot("root");
    root.append_child("empty").text().set("");
    root.append_child("item").append_attribute("value") = 7;
    root.child("item").text().set("text & <markup>");

    // pugixml 1.16 serializes a single empty PCDATA child as an empty tag.
    const string serialized = xml.toString();
    check("empty text serializes as an empty element",
          serialized.find("<empty />") != string::npos);
    check("text is escaped on serialization",
          serialized.find("text &amp; &lt;markup&gt;") != string::npos);

    Xml restored = parseXml(serialized);
    check("empty element survives the round trip", restored.root().child("empty"));
    check("text survives the round trip",
          string(restored.root().child("item").text().get()) == "text & <markup>");
    check("attribute survives the round trip",
          restored.root().child("item").attribute("value").as_int() == 7);
    check("XPath selects by numeric attribute",
          restored.document().select_nodes("/root/item[@value > 5]").size() == 1);

    // Callers using the underlying document can still request paired tags.
    ostringstream paired;
    xml.document().save(paired, "", pugi::format_raw | pugi::format_no_empty_element_tags);
    check("explicit paired-tag formatting is preserved",
          paired.str().find("<empty></empty>") != string::npos);

    Xml invalid;
    check("malformed XML reports failure", !invalid.parse("<root>"));
    check("empty XML reports failure", !invalid.parse(""));
    printf("%d failed\n", failures);
    return failures ? 1 : 0;
}
