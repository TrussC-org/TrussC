#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <string>

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

using Replace = void (*)(std::string&, const std::string&, const std::string&);
void checkReplace(Replace replace, const char* implementation) {
    std::printf("%s replacement:\n", implementation);
    struct Case {
        const char* name;
        const char* input;
        const char* search;
        const char* replacement;
        const char* expected;
    };
    const Case cases[] = {
        {"empty search with replacement", "abc", "", "x", "abc"},
        {"empty search and replacement", "abc", "", "", "abc"},
        {"empty input and search", "", "", "x", ""},
        {"all empty", "", "", "", ""},
        {"empty input", "", "a", "b", ""},
        {"no match", "abc", "z", "x", "abc"},
        {"search longer than input", "a", "aa", "b", "a"},
        {"ordinary replacement", "one two one", "one", "three", "three two three"},
        {"deletion", "banana", "na", "", "ba"},
        {"nonoverlapping matches", "aaa", "aa", "b", "ba"},
        {"replacement is not searched", "aa", "a", "aa", "aaaa"},
        {"replacement equals search", "aaaa", "aa", "aa", "aaaa"},
        {"match at both ends", "ab-middle-ab", "ab", "x", "x-middle-x"},
    };
    for (const auto& c : cases) {
        std::string value = c.input;
        replace(value, c.search, c.replacement);
        check(c.name, value == c.expected);
    }
    std::string input, expected;
    for (int line = 0; line < 20000; ++line) {
        input += "row\r\n";
        expected += "row\n";
    }
    replace(input, "\r\n", "\n");
    check("many CRLF replacements preserve every row", input == expected);
}
} // namespace

TC_CORE_TEST_MAIN() {
    check("empty needle matches nothing", trussc::stringTimesInString("abc", "") == 0);
    check("empty needle and haystack match nothing", trussc::stringTimesInString("", "") == 0);
    check("empty haystack has no nonempty matches", trussc::stringTimesInString("", "a") == 0);
    check("count is nonoverlapping", trussc::stringTimesInString("aaa", "aa") == 1);
    check("count includes last match", trussc::stringTimesInString("banana", "na") == 2);
    check("absent needle count is zero", trussc::stringTimesInString("abc", "x") == 0);
    check("containment still finds an empty string", trussc::isStringInString("abc", ""));
    checkReplace(trussc::stringReplace, "public");
    checkReplace(trussc::internal::stringReplace, "timestamp helper");
    check("timestamp millisecond placeholder is replaced",
          trussc::getTimestampString("%i/%i").find("%i") == std::string::npos);
    return failures ? 1 : 0;
}
