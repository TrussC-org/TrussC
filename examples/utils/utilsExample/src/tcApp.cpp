#include "tcApp.h"

// =============================================================================
// utilsExample - Utility function demo for tcUtils.h and tcTime.h
// =============================================================================
// Display results of various utility functions in real-time on screen

void tcApp::setup() {
    tcLogNotice("tcApp") << "utilsExample: Utility Functions Demo";

    // String split test
    splitResult_ = splitString("apple,banana,cherry,date", ",");
}

void tcApp::update() {
    // No update processing needed
}

void tcApp::draw() {
    clear(0.12f, 0.14f, 0.18f);

    float y = 30;
    float lineHeight = 18;
    float sectionGap = 25;

    // ==========================================================================
    // Title
    // ==========================================================================
    setColor(colors::white);
    drawBitmapStringHighlight("utilsExample - Utility Functions Demo",
        10, y, Color(0, 0, 0, 0.7f), colors::white);
    y += lineHeight + sectionGap;

    // ==========================================================================
    // Time Utilities (tcTime.h)
    // ==========================================================================
    setColor(colors::cornflowerBlue);
    drawBitmapString("[ Time Utilities - tcTime.h ]", 10, y);
    y += lineHeight + 5;

    setColor(colors::lightGray);

    // Elapsed time
    drawBitmapString("getElapsedTimef(): " + toString(getElapsedTimef(), 3), 20, y);
    y += lineHeight;

    drawBitmapString("getElapsedTimeMillis(): " + to_string(getElapsedTimeMillis()), 20, y);
    y += lineHeight;

    // Timestamp
    drawBitmapString("getTimestampString(): " + getTimestampString(), 20, y);
    y += lineHeight;

    drawBitmapString("getTimestampString(\"%H:%M:%S\"): " + getTimestampString("%H:%M:%S"), 20, y);
    y += lineHeight;

    // Current time
    string timeStr = toString(getHours(), 2, '0') + ":" +
                     toString(getMinutes(), 2, '0') + ":" +
                     toString(getSeconds(), 2, '0');
    drawBitmapString("getHours/Minutes/Seconds: " + timeStr, 20, y);
    y += lineHeight;

    // Current date
    string dateStr = to_string(getYear()) + "/" +
                     toString(getMonth(), 2, '0') + "/" +
                     toString(getDay(), 2, '0');
    drawBitmapString("getYear/Month/Day: " + dateStr, 20, y);
    y += lineHeight;

    // Day of week
    const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    drawBitmapString("getWeekday(): " + to_string(getWeekday()) + " (" + weekdays[getWeekday()] + ")", 20, y);
    y += lineHeight + sectionGap;

    // ==========================================================================
    // String Utilities (tcUtils.h)
    // ==========================================================================
    setColor(colors::coral);
    drawBitmapString("[ String Utilities - tcUtils.h ]", 10, y);
    y += lineHeight + 5;

    setColor(colors::lightGray);

    // Test string
    drawBitmapString("testString: \"" + testString_ + "\"", 20, y);
    y += lineHeight;

    // isStringInString
    bool found = isStringInString(testString_, "TrussC");
    drawBitmapString("isStringInString(test, \"TrussC\"): " + string(found ? "true" : "false"), 20, y);
    y += lineHeight;

    // stringTimesInString
    size_t count = stringTimesInString("abcabcabc", "abc");
    drawBitmapString("stringTimesInString(\"abcabcabc\", \"abc\"): " + to_string(count), 20, y);
    y += lineHeight;

    // splitString
    drawBitmapString("splitString(\"apple,banana,cherry,date\", \",\"): " + toString(splitResult_), 20, y);
    y += lineHeight;

    // joinString
    string joined = joinString(splitResult_, " | ");
    drawBitmapString("joinString(result, \" | \"): " + joined, 20, y);
    y += lineHeight;

    // trim
    drawBitmapString("trim(\"  hello  \"): \"" + trim("  hello  ") + "\"", 20, y);
    y += lineHeight;

    // toLower / toUpper
    drawBitmapString("toLower(\"HELLO\"): " + toLower("HELLO") + "  |  toUpper(\"hello\"): " + toUpper("hello"), 20, y);
    y += lineHeight + sectionGap;

    // ==========================================================================
    // Number Conversion Utilities
    // ==========================================================================
    setColor(colors::mediumSeaGreen);
    drawBitmapString("[ Number Conversion - tcUtils.h ]", 10, y);
    y += lineHeight + 5;

    setColor(colors::lightGray);

    // toString
    drawBitmapString("toString(3.14159, 2): " + toString(3.14159, 2), 20, y);
    y += lineHeight;

    drawBitmapString("toString(42, 5, '0'): " + toString(42, 5, '0'), 20, y);
    y += lineHeight;

    // toHex
    drawBitmapString("toHex(255): " + toHex(255) + "  |  toHex(255, 4): " + toHex(255, 4), 20, y);
    y += lineHeight;

    // toBinary
    drawBitmapString("toBinary((char)65): " + toBinary((char)65) + " (= 'A')", 20, y);
    y += lineHeight;

    // hexToInt
    drawBitmapString("hexToInt(\"FF\"): " + to_string(hexToInt("FF")), 20, y);
    y += lineHeight + sectionGap;

    // ==========================================================================
    // Frame Information
    // ==========================================================================
    setColor(colors::white);
    string frameInfo = "FPS: " + toString(getFrameRate(), 1) +
                       "  |  Update: " + to_string(getUpdateCount()) +
                       "  |  Draw: " + to_string(getDrawCount());
    drawBitmapString(frameInfo, 10, getWindowHeight() - 20);
}
