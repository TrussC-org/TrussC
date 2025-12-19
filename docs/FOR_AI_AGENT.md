# TrussC API リファレンス（AI エージェント用）

このドキュメントは AI エージェント（Cursor, Claude Code 等）が TrussC のコードを書く際に参照するための包括的な API リファレンス。

## 概要

TrussC は sokol ベースの軽量クリエイティブコーディングフレームワーク。openFrameworks に近い API を目指している。

```cpp
#include "TrussC.h"
using namespace trussc;
// または
using namespace tc;
```

---

## App 基本構造

```cpp
class tcApp : public tc::App {
public:
    void setup() override;      // 初期化（1回だけ呼ばれる）
    void update() override;     // 毎フレーム更新
    void draw() override;       // 毎フレーム描画
    void exit() override;       // 終了時（cleanup の前、全オブジェクト生存中）
    void cleanup() override;    // 終了時クリーンアップ

    // マウスイベント
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseScrolled(float x, float y) override;

    // キーボードイベント
    void keyPressed(int key) override;
    void keyReleased(int key) override;

    // ウィンドウイベント
    void windowResized(int w, int h) override;
};

// アプリ起動
int main() {
    WindowSettings settings;
    settings.title = "My App";
    settings.width = 1280;
    settings.height = 720;
    settings.highDpi = true;
    settings.msaaSamples = 4;  // アンチエイリアス
    runApp<tcApp>(settings);
    return 0;
}
```

---

## 描画関数

### 基本図形

```cpp
// 矩形
drawRect(x, y, width, height);

// 円
drawCircle(cx, cy, radius);

// 楕円
drawEllipse(cx, cy, radiusX, radiusY);

// 線
drawLine(x1, y1, x2, y2);

// 三角形
drawTriangle(x1, y1, x2, y2, x3, y3);

// 点
drawPoint(x, y);

// 背景クリア
clear(r, g, b);           // 0-255
clear(gray);              // グレースケール
clear(Color c);           // Color構造体
```

### 塗り・線

```cpp
fill();                   // 塗りつぶし有効
noFill();                 // 塗りつぶし無効
stroke();                 // 輪郭線有効
noStroke();               // 輪郭線無効
setStrokeWeight(weight);  // 線幅設定
```

### 文字列描画

```cpp
// ビットマップフォント（固定幅、高速）Y座標はベースライン基準
drawBitmapString("Hello", x, y);
drawBitmapString("Hello", x, y, scale);  // スケール指定
```

---

## 色設定

### setColor

```cpp
// RGB (0-255)
setColor(255, 0, 0);           // 赤
setColor(255, 0, 0, 128);      // 半透明赤

// RGB (0.0-1.0)
setColor(1.0f, 0.0f, 0.0f);

// グレースケール
setColor(128);                 // 灰色
setColor(0.5f);

// Color構造体
setColor(Color c);
setColor(colors::cornflowerBlue);
```

### 色空間

```cpp
// HSB (H: 0-TAU, S: 0-1, B: 0-1)
setColorHSB(h, s, b);
setColorHSB(h, s, b, alpha);

// OKLab
setColorOKLab(L, a, b);
setColorOKLab(L, a, b, alpha);

// OKLCH（彩度と色相で指定）
setColorOKLCH(L, C, H);
setColorOKLCH(L, C, H, alpha);
```

### Color 構造体

```cpp
Color c(r, g, b);
Color c(r, g, b, a);
Color c(gray);
Color c(gray, a);

// HSB から作成
Color c = ColorHSB(h, s, b).toRGB();
Color c = ColorHSB(h, s, b, a).toRGB();

// OKLab / OKLCH から作成
Color c = ColorOKLab(L, a, b).toRGB();
Color c = ColorOKLCH(L, C, H).toRGB();

// 色補間
Color c = Color::lerp(c1, c2, t);          // RGB 線形補間
Color c = Color::lerpHSB(c1, c2, t);       // HSB 補間
Color c = Color::lerpOKLab(c1, c2, t);     // OKLab 補間（推奨）
Color c = Color::lerpOKLCH(c1, c2, t);     // OKLCH 補間
```

### 定義済みカラー

```cpp
colors::white, colors::black, colors::red, colors::green, colors::blue
colors::yellow, colors::cyan, colors::magenta, colors::orange
colors::cornflowerBlue  // oF デフォルト背景色
```

---

## 座標変換

```cpp
pushMatrix();             // 行列をスタックに保存
popMatrix();              // 行列をスタックから復元

translate(x, y);          // 平行移動（2D）
translate(x, y, z);       // 平行移動（3D）

rotate(radians);          // Z軸回転（2D用）
rotateX(radians);         // X軸回転
rotateY(radians);         // Y軸回転
rotateZ(radians);         // Z軸回転

rotateDeg(degrees);       // 度数法版
rotateXDeg(degrees);
rotateYDeg(degrees);
rotateZDeg(degrees);

scale(s);                 // 均等スケール
scale(sx, sy);            // 2Dスケール
scale(sx, sy, sz);        // 3Dスケール

resetMatrix();            // 行列をリセット
```

---

## 数学関数

### 基本数学

```cpp
// 定数
PI                        // 3.14159...
TAU                       // 6.28318... (2*PI)
HALF_PI                   // PI/2
DEG_TO_RAD                // PI/180
RAD_TO_DEG                // 180/PI

// 値の制限・補間
clamp(value, min, max);   // 範囲内に制限
lerp(a, b, t);            // 線形補間 (t: 0-1)
map(value, inMin, inMax, outMin, outMax);  // 範囲変換

// 角度変換
radians(degrees);
degrees(radians);

// その他
abs(x), floor(x), ceil(x), round(x)
min(a, b), max(a, b)
pow(x, y), sqrt(x)
sin(x), cos(x), tan(x)
asin(x), acos(x), atan(x), atan2(y, x)
```

### ベクトル

```cpp
// Vec2
Vec2 v(x, y);
Vec2 v = Vec2::zero();
v.x, v.y
v.length();               // 長さ
v.lengthSquared();        // 長さの2乗
v.normalized();           // 正規化
v.dot(other);             // 内積
v.cross(other);           // 外積（スカラー）
v.distance(other);        // 距離
v.lerp(other, t);         // 補間
v.angle();                // X軸との角度
Vec2::fromAngle(radians); // 角度から作成

// 演算子
v1 + v2, v1 - v2, v1 * scalar, v1 / scalar

// Vec3
Vec3 v(x, y, z);
Vec3 v = Vec3::zero();
Vec3 v = Vec3::up();      // (0, 1, 0)
Vec3 v = Vec3::right();   // (1, 0, 0)
Vec3 v = Vec3::forward(); // (0, 0, 1)
v.x, v.y, v.z
v.length(), v.normalized(), v.dot(other), v.cross(other)
```

### 行列

```cpp
Mat4 m = Mat4::identity();
Mat4 m = Mat4::translate(x, y, z);
Mat4 m = Mat4::rotateX(radians);
Mat4 m = Mat4::rotateY(radians);
Mat4 m = Mat4::rotateZ(radians);
Mat4 m = Mat4::scale(sx, sy, sz);
Mat4 m = m1 * m2;         // 行列の乗算
Mat4 inv = m.inverted();  // 逆行列
```

### ノイズ

```cpp
// パーリンノイズ (0.0 - 1.0)
noise(x);
noise(x, y);
noise(x, y, z);

// 符号付きノイズ (-1.0 - 1.0)
signedNoise(x);
signedNoise(x, y);
signedNoise(x, y, z);

// フラクタルノイズ (fbm)
fbm(x, y);
fbm(x, y, octaves, lacunarity, gain);
fbm(x, y, z, octaves, lacunarity, gain);
```

### 乱数

```cpp
random();                 // 0.0 - 1.0
random(max);              // 0.0 - max
random(min, max);         // min - max
randomInt(max);           // 0 - max-1
randomInt(min, max);      // min - max
randomSeed(seed);         // シード設定
```

---

## 入力

### マウス

```cpp
getMouseX();              // マウスX座標
getMouseY();              // マウスY座標
getPMouseX();             // 前フレームのマウスX
getPMouseY();             // 前フレームのマウスY
isMousePressed();         // マウスボタンが押されているか
isMousePressed(button);   // 特定ボタン (MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE)

// グローバル座標（Node内から使用）
getGlobalMouseX();
getGlobalMouseY();
```

### キーボード

```cpp
isKeyPressed();           // 何かキーが押されているか
isKeyPressed(key);        // 特定キーが押されているか
getLastKey();             // 最後に押されたキー

// 特殊キー定数
KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN
KEY_ENTER, KEY_ESCAPE, KEY_SPACE, KEY_TAB
KEY_BACKSPACE, KEY_DELETE
KEY_SHIFT, KEY_CTRL, KEY_ALT
KEY_F1 ~ KEY_F12
```

---

## 時間・フレーム

```cpp
// 経過時間
getElapsedTime();         // 起動からの経過時間（秒、float）
getElapsedTimef();        // 同上
getElapsedTimeMillis();   // 経過時間（ミリ秒）
getElapsedTimeMicros();   // 経過時間（マイクロ秒）
resetElapsedTimeCounter(); // 経過時間カウンターをリセット
getDeltaTime();           // 前フレームからの経過時間

// フレームカウント
getFrameCount();          // update 呼び出し回数（= getUpdateCount）
getUpdateCount();         // update 呼び出し回数
getDrawCount();           // 描画フレーム回数
getFrameRate();           // 現在のFPS

// FPS 設定
setFps(fps);              // 目標FPS設定（Update + Draw 同期）
setDrawFps(fps);          // 描画レート個別設定
setUpdateFps(fps);        // 更新レート個別設定（Decoupled モード）
setVsync(true);           // VSync モード

// タイムスタンプ
getTimestampString();     // "2024-01-15-18-29-35-299"
getTimestampString("%Y/%m/%d %H:%M:%S");  // フォーマット指定（%i でミリ秒）

// 現在時刻
getSeconds();             // 0-59
getMinutes();             // 0-59
getHours();               // 0-23
getYear();                // 例: 2024
getMonth();               // 1-12
getDay();                 // 1-31
getWeekday();             // 0=日曜, 1=月曜, ... 6=土曜

// スリープ
sleepMillis(ms);          // ミリ秒スリープ
sleepMicros(us);          // マイクロ秒スリープ

// アプリ終了
exitApp();                // 終了リクエスト → exit() → cleanup() → デストラクタ
```

---

## ウィンドウ

```cpp
getWindowWidth();         // ウィンドウ幅
getWindowHeight();        // ウィンドウ高さ
getWidth();               // 描画領域幅（= getWindowWidth）
getHeight();              // 描画領域高さ
isFullscreen();           // フルスクリーンか
setFullscreen(bool);      // フルスクリーン設定
toggleFullscreen();       // フルスクリーン切り替え
setWindowTitle("title");  // タイトル設定
```

---

## 画像 (Image)

```cpp
Image img;

// 読み込み
img.load("image.png");
img.loadFromMemory(buffer, length);

// 確保（動的更新用）
img.allocate(width, height, channels);

// 状態
img.isAllocated();
img.getWidth();
img.getHeight();
img.getChannels();

// ピクセル操作
Color c = img.getColor(x, y);
img.setColor(x, y, color);
img.update();             // 変更をGPUに反映

// 描画
img.draw(x, y);
img.draw(x, y, width, height);

// 保存
img.save("output.png");

// ピクセルデータへのアクセス
Pixels& pixels = img.getPixels();
unsigned char* data = img.getPixelsData();
```

---

## FBO (フレームバッファ)

```cpp
Fbo fbo;

// 確保
fbo.allocate(width, height);
fbo.allocate(width, height, msaaSamples);  // MSAA対応

// オフスクリーン描画
fbo.begin();              // 描画開始（透明でクリア）
fbo.begin(r, g, b, a);    // 背景色指定
// ... 描画処理 ...
fbo.end();                // 描画終了

// 描画
fbo.draw(x, y);
fbo.draw(x, y, width, height);

// 保存
fbo.save("output.png");

// Image にコピー
Image img;
fbo.copyTo(img);

// 状態
fbo.getWidth();
fbo.getHeight();
fbo.isAllocated();
```

---

## スクリーンショット

```cpp
// 画面全体をファイルに保存
saveScreenshot("screenshot.png");
saveScreenshot(getDataPath("output/frame_" + toString(getFrameNum()) + ".png"));
```

**AI エージェント向け情報**: `saveScreenshot()` は AI が自律的にデバッグを進める際に非常に有用。描画結果を画像として保存し、その画像を読み取ることで、コードの出力を視覚的に確認できる。例えば「円が表示されない」というバグを調査する際、スクリーンショットを取得して内容を確認することで、問題の原因（座標が画面外、色が背景と同じ、等）を特定できる。

---

## 3D 描画

### EasyCam

```cpp
EasyCam cam;

// setup() で初期化
cam.setDistance(500);
cam.setTarget(0, 0, 0);

// draw() で使用
cam.begin();
// ... 3D描画 ...
cam.end();

// マウス操作を有効/無効
cam.enableMouseInput();
cam.disableMouseInput();

// マウスイベントを転送（mousePressed等から呼ぶ）
cam.mousePressed(x, y, button);
cam.mouseReleased(x, y, button);
cam.mouseDragged(x, y, button);
cam.mouseScrolled(dx, dy);

// パラメータ
cam.setFovDeg(45);
cam.setNearClip(0.1f);
cam.setFarClip(10000);
cam.setSensitivity(1.0f);
cam.setZoomSensitivity(10.0f);
cam.reset();
```

### 3D プリミティブ

```cpp
// Mesh を生成
Mesh box = createBox(width, height, depth);
Mesh box = createBox(size);                    // 立方体
Mesh sphere = createSphere(radius, resolution);
Mesh cylinder = createCylinder(radius, height, resolution);
Mesh cone = createCone(radius, height, resolution);
Mesh plane = createPlane(width, height, cols, rows);
Mesh icoSphere = createIcoSphere(radius, subdivisions);

// 描画
box.draw();
box.drawWireframe();
```

### Mesh

```cpp
Mesh mesh;

// 頂点追加
mesh.addVertex(x, y, z);
mesh.addVertex(Vec3 v);
mesh.addNormal(nx, ny, nz);
mesh.addTexCoord(u, v);
mesh.addColor(Color c);
mesh.addIndex(index);
mesh.addTriangle(i0, i1, i2);

// プリミティブモード
mesh.setMode(PrimitiveMode::Triangles);
mesh.setMode(PrimitiveMode::Lines);
mesh.setMode(PrimitiveMode::Points);
mesh.setMode(PrimitiveMode::TriangleStrip);

// データ取得
vector<Vec3>& verts = mesh.getVertices();
int numVerts = mesh.getNumVertices();
int numIndices = mesh.getNumIndices();

// 描画
mesh.draw();
mesh.drawWireframe();

// クリア
mesh.clear();
```

---

## サウンド (Sound)

```cpp
Sound sound;

// 読み込み（.ogg, .wav, .mp3 対応）
sound.load("music.ogg");
sound.loadTestTone(440.0f, 1.0f);  // テスト用サイン波

// 再生制御
sound.play();
sound.stop();
sound.pause();
sound.resume();

// 設定
sound.setVolume(0.8f);      // 0.0 - 1.0
sound.setPan(-0.5f);        // -1.0（左）〜 1.0（右）
sound.setSpeed(1.5f);       // 再生速度（0.1 - 4.0）
sound.setLoop(true);

// 状態
sound.isPlaying();
sound.isPaused();
sound.isLoaded();
sound.getPosition();        // 再生位置（秒）
sound.getDuration();        // 全長（秒）
```

### マイク入力

```cpp
MicInput& mic = getMicInput();

mic.start();
mic.stop();
mic.isRunning();

// 波形データ取得
float buffer[1024];
size_t count = mic.getBuffer(buffer, 1024);
```

---

## Node (シーングラフ)

```cpp
class MyNode : public Node {
public:
    void setup() override { }
    void update() override { }
    void draw() override {
        setColor(255, 0, 0);
        drawRect(-50, -50, 100, 100);
    }

protected:
    // イベント（オーバーライド可能）
    bool hitTest(float localX, float localY) override {
        return localX >= -50 && localX <= 50 && localY >= -50 && localY <= 50;
    }
    bool onMousePress(float localX, float localY, int button) override {
        return true;  // イベント消費
    }
};

// 使い方
auto node = make_shared<MyNode>();
node->x = 100;
node->y = 100;
node->rotation = PI / 4;
node->scaleX = 2.0f;
node->scaleY = 2.0f;

// ツリー操作
parent->addChild(node);
parent->removeChild(node);
parent->getChildren();
node->getParent();

// 状態
node->isActive = true;   // false で update/draw スキップ
node->isVisible = true;  // false で draw のみスキップ

// 座標変換
node->globalToLocal(gx, gy, lx, ly);
node->localToGlobal(lx, ly, gx, gy);
float mx = node->getMouseX();  // ローカル座標でのマウス位置

// タイマー（update() 内で実行されるのでスレッドセーフ）
uint64_t id = node->callAfter(2.0, []{ /* 2秒後に実行 */ });
uint64_t id = node->callEvery(1.0, []{ /* 1秒ごとに実行 */ });
node->cancelTimer(id);
node->cancelAllTimers();
```

**タイマーの安全性**: `callAfter()` / `callEvery()` のコールバックは `update()` サイクル内（メインスレッド）で実行される。そのため、コールバック内でノードの状態変更、描画設定の変更、他のノード操作などを安全に行える。別スレッドからのコールバックではないので、mutex やロックは不要。

---

## ImGui

```cpp
// setup() で初期化
imguiSetup();

// draw() で使用
imguiBegin();

ImGui::Begin("Window Title");
ImGui::Text("Hello, world!");
if (ImGui::Button("Click Me")) {
    // ボタンが押された
}
ImGui::SliderFloat("Value", &value, 0.0f, 1.0f);
ImGui::End();

imguiEnd();

// ImGui がマウス/キーボードを使用中か
if (!imguiWantsMouse()) {
    // 自前のマウス処理
}
if (!imguiWantsKeyboard()) {
    // 自前のキーボード処理
}
```

---

## ユーティリティ

### データパス

```cpp
// デフォルトは実行ファイルと同じディレクトリの data/ フォルダ
string path = getDataPath("image.png");

// ルートを変更
setDataPathRoot("/path/to/data/");

// macOS バンドル用（Resources/data/ を参照）
setDataPathToResources();
```

### 文字列変換

```cpp
// 数値 → 文字列
string s = toString(42);
string s = toString(3.14159, 2);         // "3.14"（小数点2桁）
string s = toString(42, 5, '0');         // "00042"（5桁ゼロ埋め）
string s = toString(3.14, 2, 6, '0');    // "003.14"（精度+幅+フィル）

// 16進数
string s = toHex(255);                   // "FF"
string s = toHex(255, 4);                // "00FF"

// 2進数
string s = toBinary(255);                // "00000000...11111111"
string s = toBinary((char)65);           // "01000001" (= 'A')

// 文字列 → 数値
int i = toInt("42");
float f = toFloat("3.14");
double d = toDouble("3.14159");
bool b = toBool("true");                 // "true", "1", "yes" → true
int i = hexToInt("FF");                  // 255
unsigned int u = hexToUInt("FFFFFFFF");
```

### 文字列操作

```cpp
// 検索
bool found = isStringInString("hello world", "world");  // true
size_t count = stringTimesInString("abcabc", "abc");    // 2

// 分割・結合
vector<string> parts = splitString("a,b,c", ",");       // {"a", "b", "c"}
vector<string> parts = splitString("a, b, c", ",", true, true);  // ignoreEmpty, trim
string joined = joinString(parts, "-");                 // "a-b-c"

// 置換（破壊的）
string s = "hello world";
stringReplace(s, "world", "TrussC");                    // "hello TrussC"

// トリム
string s = trim("  hello  ");                           // "hello"
string s = trimFront("  hello");                        // "hello"
string s = trimBack("hello  ");                         // "hello"

// 大文字・小文字
string s = toLower("HELLO");                            // "hello"
string s = toUpper("hello");                            // "HELLO"
```

### JSON

```cpp
// 読み込み
Json j = loadJson("config.json");

// 書き込み
saveJson(j, "config.json");
saveJson(j, "config.json", 4);  // インデント4

// パース
Json j = parseJson("{\"key\": \"value\"}");

// 文字列化
string s = toJsonString(j);

// 値の取得・設定
j["key"] = "value";
string val = j["key"];
int num = j["number"];
bool flag = j.value("flag", false);  // デフォルト値付き
```

### ログ

```cpp
// ストリーム形式（モジュール名付き推奨）
tcLogNotice("tcApp") << "Info: " << value;
tcLogWarning("tcApp") << "Warning: " << message;
tcLogError("tcApp") << "Error: " << error;
tcLogVerbose("tcApp") << "Debug: " << debug;

// モジュール名なしも可
tcLogNotice() << "Simple message";

// レベル設定
tcSetConsoleLogLevel(LogLevel::Warning);  // Warning以上のみ表示
tcSetFileLogLevel(LogLevel::Verbose);

// ファイル出力
tcSetLogFile("app.log");
tcCloseLogFile();
```

### イベント

```cpp
// イベント定義
Event<MouseEventArgs> onMousePress;

// リスナー登録（RAII - スコープ終了で自動解除）
EventListener listener = onMousePress.listen([](MouseEventArgs& e) {
    // イベント処理
});

// メンバ関数を登録
EventListener listener = onMousePress.listen(this, &MyClass::onPress);

// 手動解除
listener.disconnect();

// イベント発火
MouseEventArgs args(x, y, button);
onMousePress.notify(args);
```

---

## ファイル構成例

```
myProject/
├── CMakeLists.txt
├── addons.make           # 使用アドオン
├── src/
│   ├── main.cpp
│   └── tcApp.cpp
├── data/                 # アセット
│   ├── image.png
│   └── sound.ogg
└── build/                # ビルド出力
```

---

## addons.make

使用するアドオンを1行に1つ記述：

```
tcxBox2d
tcxOsc
```

---

## よくあるパターン

### 基本的な描画

```cpp
void tcApp::draw() {
    clear(30);  // 暗いグレー背景

    setColor(255, 100, 100);
    fill();
    drawCircle(getMouseX(), getMouseY(), 50);

    setColor(255);
    noFill();
    stroke();
    drawRect(100, 100, 200, 150);
}
```

### アニメーション

```cpp
void tcApp::draw() {
    clear(0);

    float t = getElapsedTime();
    float x = getWidth() / 2 + sin(t) * 200;
    float y = getHeight() / 2 + cos(t * 0.7f) * 150;

    setColor(ColorHSB(t, 1.0f, 1.0f).toRGB());
    drawCircle(x, y, 30);
}
```

### 座標変換

```cpp
void tcApp::draw() {
    clear(0);

    pushMatrix();
    translate(getWidth() / 2, getHeight() / 2);
    rotate(getElapsedTime());

    setColor(255);
    drawRect(-50, -50, 100, 100);

    popMatrix();
}
```

### マウスインタラクション

```cpp
void tcApp::mousePressed(int x, int y, int button) {
    if (button == MOUSE_BUTTON_LEFT) {
        particles.push_back({(float)x, (float)y});
    }
}

void tcApp::mouseDragged(int x, int y, int button) {
    particles.push_back({(float)x, (float)y});
}
```

### キーボード入力

```cpp
void tcApp::keyPressed(int key) {
    if (key == ' ') {
        paused = !paused;
    } else if (key == KEY_ESCAPE) {
        exit(0);
    } else if (key == 'f') {
        toggleFullscreen();
    }
}
```
