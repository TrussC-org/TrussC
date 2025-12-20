#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <vector>
#include <string>

using namespace trussc;
using namespace std;

// dragDropExample - Drag & Drop Demo
// Dropping files onto the window displays file information
// Image files also show a preview

class tcApp : public App {
public:
    void setup() override;
    void draw() override;

    void filesDropped(const vector<string>& files) override;

private:
    // Dropped file information
    struct DroppedFile {
        string path;
        string name;
        string extension;
        bool isImage;
    };
    vector<DroppedFile> droppedFiles;

    // Preview image (only the latest one)
    Image previewImage;
    bool hasPreview = false;

    // Status message display
    string statusMessage = "Drop files here!";
};
