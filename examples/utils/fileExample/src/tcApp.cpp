#include "tcApp.h"

// =============================================================================
// fileExample - File utilities demo for tcFile.h
// =============================================================================
// Demonstrates path utilities, file system operations, FileWriter and FileReader

void tcApp::setup() {
    logNotice("tcApp") << "fileExample: File Utilities Demo";
    logNotice("tcApp") << "Press SPACE to write a log entry";
    logNotice("tcApp") << "Press R to read the log file";
    logNotice("tcApp") << "Press C to create logs directory";

    // Check initial state
    dataExists_ = directoryExists("");  // data folder
    logsExists_ = directoryExists("logs");

    // List data directory contents
    dirContents_ = listDirectory("");
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
    drawBitmapStringHighlight("fileExample - File Utilities Demo (tcFile.h)",
        10, y, Color(0, 0, 0, 0.7f), colors::white);
    y += lineHeight + sectionGap;

    // ==========================================================================
    // Path Utilities
    // ==========================================================================
    setColor(colors::cornflowerBlue);
    drawBitmapString("[ Path Utilities ]", 10, y);
    y += lineHeight + 5;

    setColor(colors::lightGray);
    drawBitmapString("testPath: \"" + testPath_ + "\"", 20, y);
    y += lineHeight;

    drawBitmapString("getFileName():        \"" + getFileName(testPath_) + "\"", 20, y);
    y += lineHeight;

    drawBitmapString("getBaseName():        \"" + getBaseName(testPath_) + "\"", 20, y);
    y += lineHeight;

    drawBitmapString("getFileExtension():   \"" + getFileExtension(testPath_) + "\"", 20, y);
    y += lineHeight;

    drawBitmapString("getParentDirectory(): \"" + getParentDirectory(testPath_) + "\"", 20, y);
    y += lineHeight;

    drawBitmapString("joinPath(\"a\", \"b.txt\"): \"" + joinPath("a", "b.txt") + "\"", 20, y);
    y += lineHeight + sectionGap;

    // ==========================================================================
    // File System Operations
    // ==========================================================================
    setColor(colors::coral);
    drawBitmapString("[ File System Operations ]", 10, y);
    y += lineHeight + 5;

    setColor(colors::lightGray);
    drawBitmapString("directoryExists(\"\"): " + string(dataExists_ ? "true" : "false") +
                     "  (data folder)", 20, y);
    y += lineHeight;

    drawBitmapString("directoryExists(\"logs\"): " + string(logsExists_ ? "true" : "false"), 20, y);
    y += lineHeight;

    drawBitmapString("fileExists(\"log.txt\"): " + string(fileExists("log.txt") ? "true" : "false"), 20, y);
    y += lineHeight;

    // Directory listing
    string contents = "listDirectory(\"\"): ";
    if (dirContents_.empty()) {
        contents += "(empty)";
    } else {
        for (size_t i = 0; i < dirContents_.size() && i < 5; i++) {
            if (i > 0) contents += ", ";
            contents += dirContents_[i];
        }
        if (dirContents_.size() > 5) contents += ", ...";
    }
    drawBitmapString(contents, 20, y);
    y += lineHeight + sectionGap;

    // ==========================================================================
    // FileWriter Demo
    // ==========================================================================
    setColor(colors::mediumSeaGreen);
    drawBitmapString("[ FileWriter Demo ]", 10, y);
    y += lineHeight + 5;

    setColor(colors::lightGray);
    drawBitmapString("Press SPACE to append a log entry to log.txt", 20, y);
    y += lineHeight;

    drawBitmapString("Log entries written: " + to_string(logCount_), 20, y);
    y += lineHeight;

    if (!lastLogMessage_.empty()) {
        drawBitmapString("Last: " + lastLogMessage_, 20, y);
    }
    y += lineHeight + sectionGap;

    // ==========================================================================
    // FileReader Demo
    // ==========================================================================
    setColor(colors::orchid);
    drawBitmapString("[ FileReader Demo ]", 10, y);
    y += lineHeight + 5;

    setColor(colors::lightGray);
    drawBitmapString("Press R to read log.txt", 20, y);
    y += lineHeight;

    drawBitmapString("Lines read: " + to_string(totalLinesRead_), 20, y);
    y += lineHeight;

    // Show last few lines read
    int startLine = max(0, (int)readLines_.size() - 5);
    for (int i = startLine; i < (int)readLines_.size(); i++) {
        drawBitmapString("  " + readLines_[i], 20, y);
        y += lineHeight;
    }

    // ==========================================================================
    // Instructions
    // ==========================================================================
    setColor(colors::white);
    drawBitmapString("SPACE: Write log | R: Read log | C: Create logs dir",
        10, getHeight() - 20);
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        // Write a log entry using FileWriter
        if (!logWriter_.isOpen()) {
            // First time: open in append mode
            logWriter_.open("log.txt", true);
        }

        if (logWriter_.isOpen()) {
            lastLogMessage_ = "[" + getTimestampString() + "] Log entry #" + to_string(logCount_ + 1);
            logWriter_.writeLine(lastLogMessage_);
            logCount_++;
            logNotice("FileWriter") << "Wrote: " << lastLogMessage_;
        } else {
            logError("FileWriter") << "Failed to open log.txt for writing";
        }
    }
    else if (key == 'R') {
        // Read the log file using FileReader
        readLines_.clear();
        totalLinesRead_ = 0;

        FileReader reader;
        if (reader.open("log.txt")) {
            string line;
            while (reader.readLine(line)) {
                readLines_.push_back(line);
                totalLinesRead_++;
            }
            reader.close();
            logNotice("FileReader") << "Read " << totalLinesRead_ << " lines";
        } else {
            logError("FileReader") << "Failed to open log.txt for reading";
        }
    }
    else if (key == 'C') {
        // Create logs directory
        if (createDirectory("logs")) {
            logsExists_ = true;
            logNotice("FileSystem") << "Created logs directory";
        } else {
            logError("FileSystem") << "Failed to create logs directory";
        }
        // Refresh directory listing
        dirContents_ = listDirectory("");
    }
}
