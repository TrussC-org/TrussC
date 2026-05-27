#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// fboMipmapExample - Demonstrates Fbo::allocate(..., mipmaps=true).
//
// Two cubes share the same 512×512 black/white striped texture pattern,
// but each is mapped to a different Fbo:
//   - LEFT  cube : Fbo allocated WITHOUT mipmaps (default behaviour)
//   - RIGHT cube : Fbo allocated WITH    mipmaps
//
// The camera distance oscillates so each cube periodically shrinks far into
// the distance. When small, the no-mipmap cube shows obvious moiré /
// shimmering across the 16-pixel stripes (texture undersampled), while the
// mipmap cube stays smooth because the GPU samples a pre-downsampled level.

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    EasyCam cam;
    Fbo fboPlain;     // mipmaps = false
    Fbo fboMipped;    // mipmaps = true
    Mesh boxMesh;

    void renderStripes(Fbo& fbo);
};
