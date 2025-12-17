# **TrussC Design Document**

## **1\. コンセプト & 哲学 (Concept & Philosophy)**

**"Thin, Modern, and Native"**

これからの時代（AIネイティブ・GPUネイティブ）に最適化された、商用利用可能な軽量クリエイティブコーディング環境。

* **Name:** **TrussC** (Truss \+ C \[Core/Creative/Code/C++\])  
* **Namespace:**  
  * tc:: (Core & Official Modules)  
  * tcx:: (Community Addons / Extensions)  
* **脱・肥大化 (Minimalism):**  
  * 必要な機能だけを厳選し、バイナリサイズを極小に保つ。  
  * コンパイル後の成果物に不要なライブラリを含めない。  
* **脱・GPL (License Safety):**  
  * 商用利用を前提とし、MIT / Zlib / Public Domain ライセンスのみで構成する。  
  * LGPL/GPL汚染の原因となる FFmpeg や FreeType への依存を排除する。  
* **脱・ブラックボックス (Transparency):**  
  * 重厚なラッパーで隠蔽せず、OSネイティブAPIへのアクセスを容易にする「薄い」設計。  
  * Metal / DirectX 12 / Vulkan の機能を直接触れる余地を残す。  
* **AI-Native:**  
  * AI (LLM) がコードを生成しやすい、標準的で予測可能なAPI設計（C++20準拠）。  
  * 独自すぎる記法を避け、モダンなネームスペースを採用する。

## **2\. テクニカルスタック (Tech Stack)**

「自前実装（Native Wrappers）」と「高品質な軽量ライブラリ（Header-only）」のハイブリッド構成。

| カテゴリ | 機能 | 採用ライブラリ / 戦略 | ライセンス |
| :---- | :---- | :---- | :---- |
| **Core** | ウィンドウ・入力・コンテキスト | **sokol\_app** | zlib |
| **Graphics** | 描画バックエンド (Metal/DX12/VK) | **sokol\_gfx** | zlib |
| **Scene** | シーングラフ・イベント伝播 | **自前実装 (Node System)** | MIT |
| **Shader** | シェーダー管理・変換 | **sokol-shdc** (GLSL \-\> Native) | zlib |
| **Math** | ベクトル・行列演算 | **自作 (C++20 template)** or HandmadeMath | Public Domain |
| **UI** | 開発用GUI・デバッグツール | **Dear ImGui** (Addonとして提供) | MIT |
| **Image** | 画像読み込み・書き出し | **stb\_image**, **stb\_image\_write** | Public Domain |
| **Font** | フォントレンダリング | **stb\_truetype** \+ **fontstash** | Public Domain |
| **Audio** | 音声入出力・ファイル再生 | **sokol\_audio** \+ **dr\_libs** (mp3/wav) | zlib/PD |
| **Video** | 動画再生・カメラ入力 | **OS Native API Wrapper (自作)** (AVFoundation / Media Foundation) | \- |
| **Comms** | シリアル通信 | **OS Native API Wrapper (自作)** (Win32 API / POSIX) | \- |
| **Build** | ビルドシステム | **CMake** (projectGeneratorは廃止) | \- |

## **3\. プロジェクト構造 (Directory Structure)**

oFのような「階層の制約」を撤廃し、ポータビリティを確保するディレクトリ構成。

MyProject/  
├── CMakeLists.txt       \# ビルド定義ファイル  
├── src/  
│   ├── main.cpp         \# エントリーポイント  
│   ├── tcApp.h          \# ユーザーアプリ定義 (ヘッダ)  
│   └── tcApp.cpp        \# ユーザーアプリ実装 (ソース)  
├── bin/  
│   └── data/            \# アセット (画像、シェーダー、フォント等)  
├── libs/                \# フレームワーク本体  
│   └── TrussC/  
└── addons/              \# アドオン (CMakeで自動検知)  
    ├── tcImGui/  
    │   └── CMakeLists.txt  
    └── tcOsc/

### **Addon Guidelines**

**1\. Naming Convention (名前空間の衝突回避):**

* **tcPrefix (No 'x'):** アドオンフォルダ名は tcName とする（例: tcOrbbec）。  
* **Sub-Namespaces:** クラス名の衝突を防ぐため、固有ライブラリのアドオンはサブ名前空間を使用する。  
  * Bad: class tc::Device (衝突する)  
  * Good: namespace tc::orb { class Device; } (安全)

**2\. Build Logic (CMake):**

* 各アドオンは独自の CMakeLists.txt を持つ。  
* if(WIN32), if(APPLE) 等によるOS分岐が可能。  
* add\_custom\_command(POST\_BUILD ...) を使用することで、DLLやアセットを実行ファイル階層へ**自動コピー**する処理を記述できる。  
* これにより、ユーザーは trussc\_use\_addon(tcOrbbec) を記述するだけで、複雑な環境構築なしに利用できる。

環境構築の自動化ロジック (CMake):  
ユーザーは配置場所を気にせずビルド可能にする。

1. プロジェクト内の libs/ を探索。  
2. なければ環境変数 TRUSSC\_PATH を参照。  
3. (Optional) なければ GitHub から FetchContent で自動ダウンロード。

## **4\. APIデザイン (API Design)**

tc::App クラス継承による、直感的かつオブジェクト指向的な構造を採用。  
tc::App 自体が tc::Node を継承しているため、アプリケーション自体がシーングラフのルートとして機能する。

### **1\. User Application (tcApp.h)**

ユーザーは tc::App を継承したクラスを作成する。setup, update, draw をオーバーライドするだけでよい。

\#pragma once  
\#include "TrussC.h"

// tc::App は tc::Node を継承している！  
// そのため、addChild() や this-\>setPosition() などがそのまま使える。  
class tcApp : public tc::App {  
    std::shared\_ptr\<tc::Texture\> image;  
    float angle \= 0.0f;

public:  
    void setup() override;  
    void update() override;  
    void draw() override;  
};

### **2\. Implementation (tcApp.cpp)**

\#include "tcApp.h"  
\#include "tcImGui.h" // アドオン

void tcApp::setup() {  
    // ループモード設定  
    tc::setLoopMode(tc::LoopMode::Game);  
      
    // 自分自身(App)もNodeなので、タイマーやイベントも使える  
    this-\>callAfter(1.0, \[\]{ printf("1sec passed\!\\n"); });

    tc::Gui::setup();  
    image \= std::make\_shared\<tc::Texture\>("test.png");  
}

void tcApp::update() {  
    // Nodeとしての更新処理は基底クラスで呼ばれるため記述不要  
    // 独自のロジックのみここに書く  
}

void tcApp::draw() {  
    tc::clear(0.1f, 0.1f, 0.1f);

    // アドオン  
    tc::Gui::begin();  
    ImGui::SliderFloat("Angle", \&angle, 0, 360);  
    tc::Gui::end();

    tc::pushMatrix();  
    tc::translate(640, 360);  
    tc::rotate(angle);  
    image-\>draw(-50, \-50, 100, 100);  
    tc::popMatrix();  
}

### **3\. Entry Point (main.cpp)**

設定オブジェクトを作成し、アプリを起動する明示的なスタイル。

```cpp
#include "tcApp.h"

int main() {
    // 1. 設定オブジェクトを作成 (oFライク)
    tc::WindowSettings settings;
    settings.setSize(1024, 768);
    settings.setTitle("My TrussC App");
    // settings.setFullscreen(true);
    // settings.setHighDpi(false);
    // settings.setSampleCount(8);  // MSAA 8x

    // 2. アプリを指定して実行
    return tc::runApp<tcApp>(settings);
}
```

### **4\. Global Helper Functions**

oFスタイルの、どこからでも呼べるグローバル関数群。

```cpp
namespace tc {
    // ----- Window Control -----
    void setWindowTitle(const std::string& title);
    void setWindowSize(int w, int h);
    void setWindowPos(int x, int y);
    void setFullscreen(bool full);
    bool isFullscreen();
    int getWindowWidth();   // ウィンドウの幅
    int getWindowHeight();  // ウィンドウの高さ

    // ----- Screen Info -----
    int getScreenWidth();   // ディスプレイの幅
    int getScreenHeight();  // ディスプレイの高さ

    // ----- Mouse (Window Coordinates) -----
    float getMouseX();      // ウィンドウ内のマウスX座標
    float getMouseY();      // ウィンドウ内のマウスY座標
    float getPMouseX();     // 前フレームのマウスX
    float getPMouseY();     // 前フレームのマウスY
    bool isMousePressed();
    int getMouseButton();

    // ----- Time -----
    double getElapsedTime();   // アプリ開始からの経過秒数
    double getDeltaTime();     // 前フレームからの経過秒数
    uint64_t getFrameCount();  // フレーム番号

    // ----- Loop Control -----
    void setLoopMode(LoopMode mode);  // Game or Demand
    void redraw();  // Demandモード時に再描画をリクエスト
}
```

**座標系の考え方:**
* `tc::getMouseX()` / `tc::getMouseY()`: ウィンドウ左上を原点とした座標
* `tc::Node::onMousePress(localX, localY)`: そのノードのローカル座標系に変換された座標

## **5\. コアアーキテクチャ (Core Architecture)**

### **A. Loop Modes (On-Demand Rendering)**

アプリケーションの用途に応じて、メインループの挙動を切り替え可能にする。

* **tc::LoopMode::Game (Default):**  
  * VSyncに同期して毎フレーム update / draw を呼び出す。アニメーション作品向け。  
* **tc::LoopMode::Demand (Tool / Eco):**  
  * 入力イベントが発生した時や、明示的に tc::redraw() が呼ばれた時のみ描画する。  
  * ツールアプリやバッテリー重視の常駐アプリ向け。

### **B. Scene Graph & Event System**

ofxComponent の思想をコアに取り込み、相対座標管理と高度なイベント制御を実現する。

* **tc::Node Class:**  
  * 親子関係とローカル変換行列を持つ基底クラス。  
  * virtual void onMousePress(...) 等のインターフェースを持つが、**デフォルトでは呼び出されない**。  
  * this-\>enableEvents() を呼んだノードのみが、ヒットテストの対象となる（パフォーマンス最適化）。  
* **Activation Control (isActive):**  
  * isActive が false の場合、そのノードおよび全ての子孫ノードは**完全に停止する**。  
  * update、draw、イベント判定、タイマー進行のすべてが行われない（Unityの SetActive(false) 相当）。  
* **Visibility Control (isVisible):**  
  * isVisible が false の場合、**draw（描画）のみがスキップされる**。  
  * update やイベント判定は継続するため、「透明な当たり判定」や「見えないバックグラウンド処理」が可能。  
* **Orphan Nodes (親なしノード):**  
  * 生成直後や removeChild されたノードなど、シーングラフ（tc::App をルートとするツリー）に含まれないノード。  
  * **挙動:** メモリ上には存在するが、自動的な update, draw, イベント処理は行われない（休止状態）。  
  * **手動制御:** ユーザーが明示的に node-\>updateTree() や node-\>draw() を呼べば動作するが、基本的には addChild して使用することを推奨する。  
* **Event Traverse Rule (Skipper Type):**  
  * 親ノードが enableEvents() されていない（イベント無効）場合でも、**子ノードへの探索は継続する**。  
  * これにより、イベントを持たない「グループ化用ノード」の下にあるボタンも正しく反応する。  
* **Hit Test Logic:**  
  * virtual bool hitTest(const tc::Ray& localRay)  
  * 各ノードは自分のローカル座標系で当たり判定を行う。  
  * 2D/3D混在に対応するため、マウス座標をRayとして扱い、平面交差または形状交差で判定する。  
* **Event Consumption (Blocking):**  
  * イベントハンドラは bool を返す。true でイベント消費（他への伝播停止）。  
* **Global Event Listeners:**  
  * Nodeツリーとは独立して、画面全体のイベントをフックする仕組み。  
  * std::function をベースにし、ラムダ式やメンバ関数のバインドを容易にする。

### **C. Timer System (Synchronous)**

ofxComponent の addTimerFunction を継承・発展させ、メインスレッドでの安全な遅延実行を提供する。

* **Timer Structure:**  
  * 軽量な構造体で管理する。  
  * std::function\<void()\> callback: 実行する処理。  
  * double targetTime: 実行予定時刻（アプリ開始からの経過時間ベース）。  
  * double interval: 繰り返し間隔（0.0ならワンショット）。  
  * bool isDead: 削除フラグ（実行完了後やキャンセル時にtrueになり、Sweepフェーズで回収される）。  
  * uint64\_t id: 特定のタイマーをキャンセルするための識別子。64bit整数を採用し、事実上の枯渇を防ぐ。  
* **Phase Control (Pre/Post Update):**  
  * **Pre-Update:** 通常タイマー（delay \> 0）は、**update() 実行前**に処理される。これにより、フレーム開始時点で実行予定時刻を過ぎていた場合、状態を更新してから update() に入れる。  
  * **Post-Update:** 即時タイマー（delay \== 0）は、**update() 実行直後**に処理される。例外処理的だが、現在のフレーム内での即時反映を行う。  
* **Lifecycle Binding:**  
  * タイマーは tc::Node インスタンスに紐付く。Node削除時に自動破棄される。  
* **Zero Overhead:**  
  * タイマーリストが空の場合、チェック処理は即座にリターンするため、タイマー未使用ノードのCPU負荷はほぼゼロとなる。

### **D. Safe State Mutation (Lifecycle Safety)**

イテレーション中の構造変更によるクラッシュ（Invalidation）を防ぐため、遅延適用パターンを採用する。

* **Node Hierarchy:**  
  * update() ループ中に呼ばれた addChild / removeChild は即座には適用されず、**Pending Queue** に蓄積される。  
  * tc::App のフレーム終了処理（resolveStructure()）で一括適用することで、イテレータの安全性を保証する。  
* **Timer Mutation:**  
  * タイマーコールバック内で新規タイマーが追加された場合、それは pendingTimers リストに追加され、次フレームから有効化される。  
  * 実行中に clear() された場合、対象タイマーに isDead フラグを立てるのみとし、ループ終了後にメモリから削除（Sweep）する。

### **E. Rendering & Clipping**

FBOを使わない軽量な描画制限と、直感的なステート管理を実現する。

* **State Management (Pipeline Cache):**  
  * tc::setBlendMode(mode), tc::enableDepthTest() などのAPIを提供。  
  * 内部では、Sokolのパイプラインオブジェクト(PSO)のキャッシュ機構を用い、ステート変更時のオーバーヘッドを最小化する。  
* **Blend Strategy (Separate Blend):**  
  * Sokolの separate\_blend 機能を活用し、RGBとAlphaの計算式を個別に設定する。  
  * **RGB:** Src \* SrcA \+ Dst \* (1 \- SrcA) (通常)  
  * **Alpha:** Src \* 1 \+ Dst \* (1 \- SrcA) (維持/加算)  
  * これにより、不透明な背景（FBO等）に半透明オブジェクトを描画しても、背景の不透明度が維持される（色が抜けない）正しい挙動を保証する。  
* **Scissor Clipping:**  
  * tc::Node に isClipping フラグと width, height を持たせる。  
  * 有効時、draw() 呼び出しで sg\_apply\_scissor\_rect を適用し、描画完了後に親のScissor設定に戻す。  
  * 矩形は常に軸平行（AABB）とし、回転には対応しない（UI用途に特化しパフォーマンス優先）。  
  * イベントシステム（HitTest）もこのクリッピング範囲を尊重し、範囲外のマウスイベントを遮断する。

### **F. Graphics Context (Stack System)**

oFの push/pop パターンを踏襲しつつ、モダンなRAIIスタイルをサポートする。

* **Matrix Stack:**  
  * tc::pushMatrix() / tc::popMatrix() で座標変換行列を保存・復元。  
  * tc::Node の描画時にも内部的に使用される。  
* **Style Stack:**  
  * tc::pushStyle() / tc::popStyle() で描画ステートを一括保存・復元。  
  * **保存対象:**  
    * Color (Fill / Stroke)  
    * **Blend Mode** (重要: これを含めることで直感的な挙動を保証)  
    * Line Width  
    * Rect Mode / Image Mode  
    * Depth Test / Culling / Scissor State  
* **Scoped Objects (RAII):**  
  * 手動の pop 忘れを防ぐため、以下のスコープオブジェクトを提供する。  
  * tc::ScopedMatrix: コンストラクタでpush、デストラクタでpop。  
  * tc::ScopedStyle: スタイル全般の保存・復元。  
  * tc::ScopedBlendMode: ブレンドモードのみを一時的に変更（軽量）。

### **G. Threading & Concurrency**

モダンC++の標準機能をベースにしつつ、クリエイティブコーディングに必要な利便性を提供する。

* **Mutex:**  
  * 独自のラッパークラスは提供しない。  
  * 標準の std::mutex と std::lock\_guard (RAII) の使用を推奨する。これらは tc::ScopedStyle と同じ使用感で安全に排他制御を行える。  
* **Thread:**  
  * tc::Thread クラスを提供する。  
  * std::thread のラッパーとして機能し、スレッドの起動(start)、停止(stop)、結合(join)のライフサイクル管理を簡素化する。  
  * 継承して threadedFunction() を実装するスタイルを採用し、oFユーザーの移行を容易にする。

## **6\. 最重要実装項目：Native Wrapper戦略**

外部ライブラリ依存（GPL汚染・サイズ肥大化）を防ぐため、以下の機能はOS固有のAPIをラップして実装する。AIコーディングを最大限活用する領域。

### **A. Video & Camera (脱FFmpeg)**

* **macOS (AVFoundation):**  
  * CoreVideo と Metal (CVMetalTextureCache) を連携させ、**Zero-copy** でテクスチャ化する。  
* **Windows (Media Foundation):**  
  * IMFSourceReader を使用し、Direct3D 11 テクスチャとして直接バインドする。

### **B. Serial (脱Boost / 旧世代ライブラリ)**

* **Implementation:**  
  * POSIX (Mac/Linux): open(), tcsetattr(), read() で実装。  
  * Windows: CreateFile(), SetCommState(), ReadFile() で実装。

## **7\. クラス設計方針 (Class Design Policy)**

拡張性を重視するクラスと、パフォーマンスを重視するクラスを明確に区別する。

### **A. 機能クラス (Infrastructure Objects)**

方針: 積極的に virtual を使用し、ユーザーによる継承・拡張を推奨する。  
メモリ管理: std::shared\_ptr による自動管理を基本とする。

* **対象:** tc::Node, tc::App, tc::Window, tc::Serial, tc::VideoPlayer, tc::Font 等  
* **設計:**  
  * **生成:** std::make\_shared\<T\>() を推奨。  
  * **所有権:** 親ノードが std::vector\<std::shared\_ptr\<Node\>\> で子を所有する。  
  * tc::Node は内部メソッド updateTree() を持ち、実行順序を制御する。  
  * tc::Node のイベントハンドラはすべて virtual。ただし、enableEvents() フラグで実行を制御する。  
  * 内部状態変数は protected、または protected なアクセサを通して公開する。

### **B. データクラス (Data Objects)**

方針: パフォーマンスとメモリレイアウト（POD性）を最優先し、継承を非推奨とする。  
メモリ管理: 基本的に値渡し（Value Semantics）、または必要に応じて shared\_ptr。

* **対象:** tc::Vec3, tc::Color, tc::Matrix, tc::Mesh, tc::Texture 等  
* **設計:**  
  * virtual メソッドは持たない。  
  * GPUへ転送する構造体は、標準レイアウト（Standard Layout）を維持する。

## **8\. ロードマップ (Roadmap to MVP)**

まず目指すべき「Ver 0.1」のゴール。

1. **Base:** sokol\_app \+ sokol\_gfx でウィンドウ表示と画面クリア。  
2. **Scene:** tc::Node とイベントシステム（逆順トラバース・RayベースのHitTest・タイマー）の実装。  
3. **Wrapper:** tc::drawRect 等の基本描画と、tc::RectNode (2D判定付き) の実装。  
4. **Modules:** tc::Gui (ImGui) を公式モジュールとして実装。  
5. **Build:** どのディレクトリに置いても動く CMakeLists.txt のテンプレート完成。