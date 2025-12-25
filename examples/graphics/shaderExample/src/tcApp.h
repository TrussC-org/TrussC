#pragma once

#include <TrussC.h>
#include "shaders/effects.glsl.h"
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    static constexpr int NUM_EFFECTS = 4;

    // 各エフェクト用のシェーダーとパイプライン
    sg_shader shaders[NUM_EFFECTS] = {};
    sg_pipeline pipelines[NUM_EFFECTS] = {};

    // 共有リソース
    sg_buffer vertexBuffer = {};
    sg_buffer indexBuffer = {};
    fs_params_t uniforms = {};

    int currentEffect = 0;
    bool loaded = false;

    void loadEffect(int index);
    const char* getEffectName(int index);
};
