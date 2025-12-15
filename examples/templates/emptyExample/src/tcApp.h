#pragma once

#include "tcBaseApp.h"

// =============================================================================
// tcApp - アプリケーションクラス
// tc::App を継承して、必要なメソッドをオーバーライドする
// =============================================================================

class tcApp : public tc::App {
public:
    void setup() override;
    void draw() override;
};
