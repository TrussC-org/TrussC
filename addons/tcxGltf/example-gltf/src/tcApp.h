#pragma once

#include <TrussC.h>
#include <tcxGltf.h>
using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void filesDropped(const vector<string>& files) override;

private:
    void loadModel(const string& path);

    EasyCam cam;
    Environment env;
    GltfModel model;
    Light keyLight;
    Light fillLight;
    string modelName;
};
