# TrussC アドオン

TrussC はアドオンシステムで機能を拡張できる。openFrameworks の ofxAddon に相当する仕組み。

---

## 目次

1. [アドオンの使い方](#アドオンの使い方)
2. [既存アドオンの導入](#既存アドオンの導入)
3. [アドオンの作り方](#アドオンの作り方)
4. [命名規則](#命名規則)
5. [依存関係](#依存関係)

---

## アドオンの使い方

### 1. CMakeLists.txt に追加

```cmake
# TrussC をリンク
target_link_libraries(${PROJECT_NAME} PRIVATE tc::TrussC)

# アドオンを追加（この1行だけでOK）
use_addon(${PROJECT_NAME} tcxBox2d)
```

### 2. コードで使用

```cpp
#include <tcxBox2d/tcxBox2d.h>

using namespace tcx::box2d;

World world;
Circle circle;

void setup() {
    world.setup(Vec2(0, 300));
    circle.setup(world, 400, 100, 30);
}
```

---

## 既存アドオンの導入

### 公式アドオン

TrussC に同梱されているアドオン:

| アドオン名 | 説明 |
|-----------|------|
| tcxBox2d | Box2D 2D物理エンジン |

### サードパーティアドオン

1. アドオンを `addons/` フォルダに配置
2. `use_addon()` で追加

```
trussc_v0.0.1/
└── addons/
    ├── tcxBox2d/        # 公式
    └── tcxSomeAddon/    # サードパーティ
```

---

## アドオンの作り方

### フォルダ構造

```
addons/tcxMyAddon/
├── CMakeLists.txt           # ビルド設定
├── include/
│   └── tcxMyAddon/
│       ├── tcxMyAddon.h     # メインヘッダー（全部 include）
│       ├── tcxMyClass.h     # 各クラスのヘッダー
│       └── ...
├── src/
│   ├── tcxMyClass.cpp       # 実装ファイル
│   └── ...
└── examples/                # サンプル（任意）
    └── myAddonExample/
        ├── CMakeLists.txt
        └── src/
            └── tcApp.cpp
```

### CMakeLists.txt テンプレート

```cmake
# =============================================================================
# tcxMyAddon - アドオン説明
# =============================================================================

cmake_minimum_required(VERSION 3.16)

set(ADDON_NAME tcxMyAddon)

# =============================================================================
# 外部ライブラリ（必要な場合）
# =============================================================================
# include(FetchContent)
# FetchContent_Declare(...)
# FetchContent_MakeAvailable(...)

# =============================================================================
# アドオンライブラリ
# =============================================================================
set(ADDON_SOURCES
    src/tcxMyClass.cpp
)

set(ADDON_HEADERS
    include/tcxMyAddon/tcxMyAddon.h
    include/tcxMyAddon/tcxMyClass.h
)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 他のアドオンに依存する場合
# use_addon(${ADDON_NAME} tcxOtherAddon)

target_link_libraries(${ADDON_NAME} PUBLIC
    TrussC
)

# =============================================================================
# サンプル（任意）
# =============================================================================
option(TCX_MYADDON_BUILD_EXAMPLES "Build tcxMyAddon examples" ON)

if(TCX_MYADDON_BUILD_EXAMPLES)
    add_subdirectory(examples/myAddonExample)
endif()
```

### メインヘッダー（tcxMyAddon.h）

```cpp
// =============================================================================
// tcxMyAddon - アドオン説明
// =============================================================================

#pragma once

// 全てのヘッダーをインクルード
#include "tcxMyClass.h"
#include "tcxAnotherClass.h"
```

### クラス実装例

```cpp
// tcxMyClass.h
#pragma once

#include <TrussC.h>

namespace tcx::myaddon {

class MyClass {
public:
    void setup();
    void update();
    void draw();

private:
    // ...
};

} // namespace tcx::myaddon
```

---

## 命名規則

### アドオン名

| 項目 | 規則 | 例 |
|-----|------|-----|
| フォルダ名 | `tcx` + PascalCase | `tcxBox2d`, `tcxGui` |
| ライブラリ名 | フォルダ名と同じ | `tcxBox2d` |
| メインヘッダー | `アドオン名.h` | `tcxBox2d.h` |

### 名前空間

```cpp
namespace tcx::アドオン名 {
    // ...
}
```

| アドオン | 名前空間 |
|---------|---------|
| tcxBox2d | `tcx::box2d` |
| tcxGui | `tcx::gui` |
| tcxOsc | `tcx::osc` |

**注意:** TrussC 本体は `tc::` を使用。アドオンは `tcx::` を使用。

### ファイル名

| 種類 | 規則 | 例 |
|-----|------|-----|
| ヘッダー | `tcx` + PascalCase + `.h` | `tcxBox2dWorld.h` |
| 実装 | `tcx` + PascalCase + `.cpp` | `tcxBox2dWorld.cpp` |

### クラス名

名前空間内ではプレフィックスなし:

```cpp
namespace tcx::box2d {
    class World { ... };    // OK: tcx::box2d::World
    class Circle { ... };   // OK: tcx::box2d::Circle
}
```

---

## 依存関係

### 他のアドオンに依存する場合

アドオンの CMakeLists.txt 内で `use_addon()` を使用:

```cmake
# tcxMyAddon が tcxBox2d に依存
add_library(${ADDON_NAME} STATIC ...)

use_addon(${ADDON_NAME} tcxBox2d)

target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
```

ユーザーが `use_addon(myapp tcxMyAddon)` を呼ぶと、tcxBox2d も自動的に追加される。

### 外部ライブラリに依存する場合

FetchContent を使用:

```cmake
include(FetchContent)

FetchContent_Declare(
    somelib
    GIT_REPOSITORY https://github.com/example/somelib.git
    GIT_TAG v1.0.0
)

FetchContent_MakeAvailable(somelib)

target_link_libraries(${ADDON_NAME} PUBLIC somelib)
```

---

## 公式アドオン一覧

### tcxBox2d

Box2D 2D物理エンジンの TrussC ラッパー。

**機能:**
- World（物理ワールド）
- Body（物理ボディ基底クラス、tc::Node 継承）
- Circle, Rect, Polygon（形状）
- マウスドラッグ（b2MouseJoint）

**使用例:**

```cpp
#include <tcxBox2d/tcxBox2d.h>

using namespace tc;
using namespace tcx::box2d;

class tcApp : public App {
    World world;
    Circle circle;

    void setup() override {
        world.setup(Vec2(0, 300));  // 重力
        world.createBounds();        // 画面端に壁
        circle.setup(world, 400, 100, 30);
    }

    void update() override {
        world.update();
        circle.updateTree();  // Box2D → Node 座標同期
    }

    void draw() override {
        clear(30);
        setColor(255, 200, 100);
        circle.drawTree();    // 位置・回転を適用して描画
    }
};
```
