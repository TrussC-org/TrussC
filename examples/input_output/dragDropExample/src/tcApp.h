#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// dragDropExample - Drag & Drop Demo
// Dropping files onto the window displays file information
// Image files also show a preview

class tcApp : public App {
public:
    void setup() override;
    void draw() override;

    void filesDropped(const vector<fs::path>& files) override;

private:
    // Dropped file information
    struct DroppedFile {
        fs::path path;
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
