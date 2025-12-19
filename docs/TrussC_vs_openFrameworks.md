# **TrussC vs openFrameworks: Evolution & Philosophy**

TrussC は、openFrameworks (oF) が築き上げた「クリエイティブコーディングの楽しさ」と「直感的なプログラミング体験」を深くリスペクトし、その精神を継承しています。

しかし、oFが設計された2000年代初頭と現在とでは、ハードウェアの性能、C++の言語仕様、そして求められるアプリケーションの複雑さが大きく異なります。

このドキュメントでは、TrussCがoFから**何を受け継ぎ**、**何を変えたのか**、そして**その変更がどのようなメリットをもたらすのか**を解説します。

## **1\. 共通点：変わらない「楽しさ」 (Shared DNA)**

oFユーザーがTrussCに触れたとき、違和感なく書き始められるように、以下のコア・コンセプトは意図的に維持しています。

| 項目 | openFrameworks | TrussC | 解説 |
| :---- | :---- | :---- | :---- |
| **ライフサイクル** | setup, update, draw | setup, update, draw | クリエイティブコーディングの基本リズムです。この構造は不変です。 |
| **即時描画** | ofDrawCircle(x, y, r) | tc::drawCircle(x, y, r) | 「1行書けば絵が出る」という魔法のような手軽さは、絶対に失ってはいけない要素です。 |
| **ステートマシン** | ofSetColor, ofPushMatrix | tc::setColor, tc::pushMatrix | ステート（状態）を変更して描画するスタイルを踏襲しています。 |
| **学習曲線** | 初心者フレンドリー | 初心者フレンドリー | 複雑なGPUの概念（コマンドバッファ、ディスクリプタヒープ等）は隠蔽し、誰でもコードで表現できるようにします。 |

## **2\. 相違点：現代への適応 (Evolution & Merits)**

TrussCは、現代のアプリケーション開発における「複雑さ」と「パフォーマンス」の課題を解決するために、大胆な構造改革を行いました。

### **A. 構造と座標系：Scene Graphの導入**

* **openFrameworks:**  
  * **構造:** 基本的にフラット。親子関係を作るには ofNode を手動で管理するか、自前で translate を計算する必要がありました。  
  * **座標:** マウスイベントは常に「画面左上からの絶対座標」で届きます。回転したオブジェクトの中でのクリック判定は困難な計算が必要でした。  
  * **課題:** UIや複雑な階層構造を持つ作品を作ると、座標変換のコードで溢れかえります。  
* **TrussC:**  
  * **構造:** **シーングラフ (tc::Node)** を標準装備。UnityのGameObjectのように、親子関係を前提とします。  
  * **座標:** マウスイベントは自動的に変換され、**「そのオブジェクトにとってのローカル座標」** として届きます。  
  * **メリット:**  
    * 「回転している親の中にあるボタン」も、rect.contains(pos) だけで判定できます。  
    * グループごとの移動・回転・表示切り替えが極めて容易になります。

### **B. 描画バックエンド：OpenGLからの脱却**

* **openFrameworks:**  
  * **技術:** OpenGL (Legacy & Modern)。  
  * **課題:** Apple (macOS/iOS) がOpenGLを非推奨にし、Metalへ移行したことで、将来的な動作保証やパフォーマンス最適化が困難になっています。  
* **TrussC:**  
  * **技術:** **Sokol (Metal / DirectX 11/12 / Vulkan / WebGPU)**。  
  * **メリット:**  
    * OSが推奨する最新のネイティブAPI上で動作するため、**高速かつ省電力**です。  
    * バイナリサイズが圧倒的に小さくなります。  
    * Webブラウザ (WebAssembly \+ WebGPU) への移植性も高まります。

### **C. メモリ管理：Modern C++ Safety**

* **openFrameworks:**  
  * **方針:** 生ポインタ (\*) や、独自のスマートポインタ (ofPtr) が混在。  
  * **課題:** 初心者が delete を忘れてメモリリークしたり、無効なポインタにアクセスしてクラッシュする事故が多発します。  
* **TrussC:**  
  * **方針:** **std::shared\_ptr / std::weak\_ptr (C++11以降)** の完全採用。  
  * **メリット:**  
    * 親ノードが消えれば、子ノードも自動的に消滅します。  
    * ユーザーがメモリ解放を意識する必要がほぼなくなります。  
    * AI (LLM) が生成するモダンなC++コードと相性が抜群です。

### **D. 時間と非同期：安全なタイマー**

* **openFrameworks:**  
  * **手法:** ofThread を使うか、自前で update 内で時間を計測して if 文を書く。  
  * **課題:** マルチスレッドはデータの競合（Data Race）を起こしやすく、デバッグが地獄です。update 内の分岐はコードを汚くします。  
* **TrussC:**  
  * **手法:** **同期タイマー (callAfter, callEvery)**。  
  * **メリット:**  
    * 「3秒後にこの関数を実行」といった処理を、**メインスレッド内で安全に** 記述できます。  
    * 排他制御（Mutex）が不要で、変数の競合を気にする必要がありません。

### **E. 依存関係とビルド：Minimalism & CMake**

* **openFrameworks:**  
  * **構成:** "Battery Included"（全部入り）。  
  * **課題:**  
    * 「四角形を一つ描くだけ」のアプリでも数百MBのサイズになり、ビルド時間が長いです。  
    * アドオンを追加する際、ヘッダーパスやリンク設定を手動で管理する必要があり、**「ライブラリはあるのにリンクエラー」** に悩まされがちです。  
* **TrussC:**  
  * **構成:** **"Thin Wrapper" \+ CMake**。  
  * **メリット:**  
    * **依存関係の自動解決:** target\_link\_libraries を使ってアドオンを1行指定するだけで、必要なヘッダーパスやリンク設定がすべて自動的に伝播します。  
    * **軽量:** コンパイルが爆速で、実行ファイルも軽量です。  
    * **Addon命名:** tc (TrussC) プレフィックスで統一し、拡張機能も「標準パーツ」として扱います。

### **F. アドオンシステム**

* **openFrameworks:**
  * **命名:** `ofx` プレフィックス（例: `ofxGui`, `ofxOsc`）
  * **導入方法:** projectGenerator でチェックボックスを選択、または `addons.make` ファイルに記述。
  * **ビルド:** IDE のプロジェクト設定に手動でパスを追加する場合も多い。
  * **名前空間:** 標準的な規約がなく、グローバル名前空間に直接定義されることが多い。
  * **課題:** アドオン同士の依存関係解決が手動。「ofxA が ofxB に依存」という場合、両方を addons.make に書く必要がある。

* **TrussC:**
  * **命名:** `tcx` プレフィックス（例: `tcxBox2d`, `tcxOsc`）
  * **導入方法:** CMakeLists.txt に1行追加するだけ。
    ```cmake
    use_addon(${PROJECT_NAME} tcxBox2d)
    ```
  * **ビルド:** CMake が自動的にインクルードパス・ライブラリリンクを設定。
  * **名前空間:** `tcx::アドオン名` で統一（例: `tcx::box2d::World`）。
  * **メリット:**
    * **依存関係の自動解決:** tcxA が tcxB に依存していれば、`use_addon(app tcxA)` だけで tcxB も自動的にリンクされる。
    * **FetchContent 対応:** 外部ライブラリは CMake の FetchContent で自動ダウンロード可能。
    * **名前の衝突回避:** 名前空間が整理されているため、複数のアドオンで同名クラスがあっても安全。

**フォルダ構造の比較:**

| 項目 | openFrameworks | TrussC |
|:-----|:---------------|:-------|
| 配置場所 | `of/addons/ofxName/` | `trussc/addons/tcxName/` |
| ソースコード | `src/` | `src/` |
| ヘッダー | `src/` (混在) | `include/tcxName/` (分離) |
| サンプル | `example/` | `examples/` |
| ビルド設定 | `addon_config.mk` | `CMakeLists.txt` |

詳細は [ADDONS.md](ADDONS.md) を参照。

## **3\. どちらを選ぶべきか？**

### **openFrameworks を選ぶべき場合**

* 過去の膨大なアドオン資産（ofxAddon）をそのまま使いたい。  
* インターネット上に存在する10年分のチュートリアルやサンプルコードを参考にしたい。  
* Kinect v1/v2 や古い周辺機器との安定した接続が必要。

### **TrussC を選ぶべき場合**

* **「商用案件」** で、ライセンス問題やクラッシュリスクを最小限に抑えたい。  
* **「モダンなUI」** やインタラクションを含む、複雑な構造のアプリケーションを作りたい。  
* **「AI (ChatGPT/Claude)」** とペアプログラミングをして、最新のC++でコードを書きたい。  
* とにかくビルドを速くし、軽快に試行錯誤したい。

TrussCは、oFへのリスペクトを込めて作られた、**「次の10年を戦うための新しい足場（Truss）」** です。

---

## **4. oF で困っていたことへの解決策**

oF ユーザーが長年悩まされてきた問題に対する、TrussC での解決策を紹介します。

### **太線の描画（StrokeMesh）**

**oF の問題:**
- `ofSetLineWidth()` は OpenGL の制限で太さ 1px 以上が保証されない
- macOS / Metal 環境では完全に無視される
- 太い線を描くには自前でメッシュを生成する必要があった

**TrussC の解決:**
```cpp
// StrokeMesh で任意の太さの線が描ける
tc::StrokeMesh stroke;
stroke.setStrokeWidth(5.0f);  // 5px の太線
stroke.moveTo(0, 0);
stroke.lineTo(100, 100);
stroke.draw();

// Polyline からも生成可能
tc::Polyline line;
line.addVertex(0, 0);
line.addVertex(100, 50);
line.addVertex(200, 0);
tc::StrokeMesh stroke2(line, 3.0f);  // 3px
stroke2.draw();
```

### **Update と Draw のサイクル制御**

**oF の問題:**
- `ofSetFrameRate()` は Draw と Update が常に連動
- 物理シミュレーションを固定タイムステップで回したいときに困る
- イベント駆動型アプリ（ボタンを押したときだけ再描画）が作りにくい

**TrussC の解決:**
```cpp
// Draw と Update を独立制御
tc::setDrawVsync(true);    // 描画は VSync (60Hz)
tc::setUpdateFps(120);     // 物理更新は 120Hz

// イベント駆動型（省電力モード）
tc::setDrawFps(0);         // 自動描画を停止
// mousePressed 等で tc::redraw() を呼ぶと描画される
```

### **シーングラフと親子関係（Node）**

**oF の問題:**
- `ofNode` はあるが、親子関係の管理が手動
- 子ノードの追加・削除でメモリ管理が複雑
- 親が消えても子が残ってしまう問題

**TrussC の解決:**
```cpp
// shared_ptr ベースで自動管理
auto parent = tc::Node::create();
auto child = tc::Node::create();
parent->addChild(child);

// 親を破棄すれば子も自動的に解放される
// 循環参照も weak_ptr で安全に回避
```

### **ローカル座標でのマウスイベント**

**oF の問題:**
- マウス座標は常にスクリーン座標（左上原点）
- 回転・スケールしたオブジェクト上のクリック判定が地獄
- `ofNode` を使っても座標変換は自分で計算

**TrussC の解決:**
```cpp
// RectNode は自動的にローカル座標に変換
class MyButton : public tc::RectNode {
    void onMousePressed(const tc::Vec2& localPos, int button) override {
        // localPos は「このノードにとっての」座標
        // 親が回転していても、自分のローカル座標で届く
        if (localPos.x < width/2) {
            // 左半分をクリック
        }
    }
};
```

### **ヒットテスト（当たり判定）**

**oF の問題:**
- 複数のオブジェクトが重なっているときの判定が面倒
- Z オーダー（描画順）を考慮した判定は自前実装

**TrussC の解決:**
```cpp
// RectNode は自動的に Z オーダーを考慮
// 手前のノードが優先的にイベントを受け取る
// イベントを「消費」すれば後ろには伝播しない

void onMousePressed(const tc::Vec2& pos, int button) override {
    // このノードで処理したら、後ろのノードには届かない
    consumeEvent();
}
```

### **スレッドセーフなタイマー**

**oF の問題:**
- `ofThread` はデータ競合を起こしやすい
- 「3秒後に実行」を安全にやる標準的な方法がない

**TrussC の解決:**
```cpp
// メインスレッドで安全に遅延実行
node->addTimerFunction(3.0f, []() {
    // 3秒後にメインスレッドで実行
    // Mutex 不要、データ競合の心配なし
});
```

---

## **5. API 対応表（oF → TrussC）**

oF ユーザーが TrussC で同等の機能を探す際のリファレンスです。

### **アプリ構造**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofApp : public ofBaseApp` | `tcApp : public tc::App` | emptyExample | |
| `ofRunApp(new ofApp())` | `tc::runApp<tcApp>()` | emptyExample | テンプレート形式 |
| `setup()` | `setup()` | emptyExample | 同じ |
| `update()` | `update()` | emptyExample | 同じ |
| `draw()` | `draw()` | emptyExample | 同じ |
| `keyPressed(int key)` | `keyPressed(int key)` | keyboardExample | 同じ |
| `mousePressed(x, y, button)` | `mousePressed(x, y, button)` | mouseExample | 同じ |
| `windowResized(w, h)` | `windowResized(w, h)` | emptyExample | 同じ |
| `dragEvent(ofDragInfo)` | `filesDropped(paths)` | dragDropExample | |
| `ofSetFrameRate(60)` | `tc::setFps(60)` | loopModeExample | |
| `ofSetVerticalSync(true)` | `tc::setVsync(true)` | loopModeExample | |
| - | `tc::setDrawFps(fps)` | loopModeExample | 描画レート個別設定 |
| - | `tc::setUpdateFps(fps)` | loopModeExample | 更新レート個別設定 |
| `ofGetElapsedTimef()` | `tc::getElapsedTime()` | graphicsExample | |
| `ofGetFrameRate()` | `tc::getFrameRate()` | loopModeExample | |
| `ofGetWidth()` | `tc::getWindowWidth()` | vectorMathExample | |
| `ofGetHeight()` | `tc::getWindowHeight()` | vectorMathExample | |

### **描画（Graphics）**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofBackground(r, g, b)` | `tc::clear(r, g, b)` | graphicsExample | 0-255 ではなく 0.0-1.0 |
| `ofSetColor(r, g, b, a)` | `tc::setColor(r, g, b, a)` | graphicsExample | 0.0-1.0 |
| `ofSetColor(ofColor::red)` | `tc::setColor(tc::colors::red)` | colorExample | |
| `ofDrawRectangle(x, y, w, h)` | `tc::drawRect(x, y, w, h)` | graphicsExample | |
| `ofDrawCircle(x, y, r)` | `tc::drawCircle(x, y, r)` | graphicsExample | |
| `ofDrawEllipse(x, y, w, h)` | `tc::drawEllipse(x, y, w, h)` | graphicsExample | |
| `ofDrawLine(x1, y1, x2, y2)` | `tc::drawLine(x1, y1, x2, y2)` | graphicsExample | |
| `ofDrawTriangle(...)` | `tc::drawTriangle(...)` | graphicsExample | |
| `ofNoFill()` | `tc::noFill()` | graphicsExample | |
| `ofFill()` | `tc::fill()` | graphicsExample | |
| `ofSetLineWidth(w)` | `tc::setLineWidth(w)` | graphicsExample | |
| `ofDrawBitmapString(s, x, y)` | `tc::drawBitmapString(s, x, y)` | graphicsExample | |
| `ofPolyline` | `tc::Polyline` | polylinesExample | |
| - | `tc::StrokeMesh` | strokeMeshExample | 太線描画 |
| `ofEnableBlendMode()` | `tc::setBlendMode()` | blendingExample | |
| `ofScissor()` | `tc::setScissor()` | clippingExample | |

### **変換（Transform）**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofPushMatrix()` | `tc::pushMatrix()` | graphicsExample | |
| `ofPopMatrix()` | `tc::popMatrix()` | graphicsExample | |
| `ofTranslate(x, y, z)` | `tc::translate(x, y, z)` | graphicsExample | |
| `ofRotateDeg(deg)` | `tc::rotateDeg(deg)` | graphicsExample | |
| `ofRotateRad(rad)` | `tc::rotateRad(rad)` | graphicsExample | |
| `ofScale(x, y, z)` | `tc::scale(x, y, z)` | 3DPrimitivesExample | |

### **数学（Math）**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `glm::vec2` / `ofVec2f` | `tc::Vec2` | vectorMathExample | |
| `glm::vec3` / `ofVec3f` | `tc::Vec3` | vectorMathExample | |
| `glm::vec4` / `ofVec4f` | `tc::Vec4` | vectorMathExample | |
| `glm::mat4` / `ofMatrix4x4` | `tc::Mat4` | vectorMathExample | |
| `ofMap(v, a, b, c, d)` | `tc::map(v, a, b, c, d)` | vectorMathExample | |
| `ofClamp(v, min, max)` | `tc::clamp(v, min, max)` | vectorMathExample | std::clamp も可 |
| `ofLerp(a, b, t)` | `tc::lerp(a, b, t)` | vectorMathExample | |
| `ofNoise(x)` | `tc::noise(x)` | noiseField2dExample | Perlin noise |
| `ofSignedNoise(x)` | `tc::signedNoise(x)` | noiseField2dExample | |
| `ofRandom(min, max)` | `tc::random(min, max)` | vectorMathExample | |
| `ofDegToRad(deg)` | `tc::radians(deg)` | vectorMathExample | |
| `ofRadToDeg(rad)` | `tc::degrees(rad)` | vectorMathExample | |
| `PI` | `tc::PI` | vectorMathExample | |
| `TWO_PI` | `tc::TAU` | vectorMathExample | τ = 2π |

### **色（Color）**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofColor(r, g, b, a)` | `tc::Color(r, g, b, a)` | colorExample | 0.0-1.0 |
| `ofColor::fromHsb(h, s, b)` | `tc::Color::fromHSB(h, s, b)` | colorExample | 0.0-1.0 |
| - | `tc::Color::fromOKLab(L, a, b)` | colorExample | OKLab 色空間 |
| - | `tc::Color::fromOKLCH(L, C, h)` | colorExample | OKLCH 色空間 |

### **画像（Image）**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofImage` | `tc::Image` | imageLoaderExample | |
| `img.load("path")` | `img.load("path")` | imageLoaderExample | 同じ |
| `img.draw(x, y)` | `img.draw(x, y)` | imageLoaderExample | 同じ |
| `img.draw(x, y, w, h)` | `img.draw(x, y, w, h)` | imageLoaderExample | 同じ |
| `img.save("path")` | `img.save("path")` | screenshotExample | 同じ |
| `img.getWidth()` | `img.getWidth()` | imageLoaderExample | 同じ |
| `img.getHeight()` | `img.getHeight()` | imageLoaderExample | 同じ |
| `img.setColor(x, y, c)` | `img.setColor(x, y, c)` | - | 同じ |
| `img.getColor(x, y)` | `img.getColor(x, y)` | - | 同じ |
| - | `img.setFilter(Nearest/Linear)` | textureExample | テクスチャフィルター |
| - | `img.setWrap(Repeat/Clamp/...)` | textureExample | テクスチャラップ |

### **フォント（Font）**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofTrueTypeFont` | `tc::TrueTypeFont` | fontExample | |
| `font.load("font.ttf", size)` | `font.load("font.ttf", size)` | fontExample | |
| `font.drawString(s, x, y)` | `font.drawString(s, x, y)` | fontExample | |
| - | `tc::drawBitmapString(s, x, y)` | graphicsExample | 組み込みフォント |

### **3D プリミティブ**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofPlanePrimitive` | `tc::createPlane(w, h)` | 3DPrimitivesExample | Mesh を返す |
| `ofBoxPrimitive` | `tc::createBox(size)` | 3DPrimitivesExample | |
| `ofSpherePrimitive` | `tc::createSphere(r, res)` | 3DPrimitivesExample | |
| `ofIcoSpherePrimitive` | `tc::createIcoSphere(r, res)` | 3DPrimitivesExample | |
| `ofCylinderPrimitive` | `tc::createCylinder(r, h, res)` | 3DPrimitivesExample | |
| `ofConePrimitive` | `tc::createCone(r, h, res)` | 3DPrimitivesExample | |

### **3D カメラ**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofEasyCam` | `tc::EasyCam` | easyCamExample | |
| `cam.begin()` | `cam.begin()` | easyCamExample | |
| `cam.end()` | `cam.end()` | easyCamExample | |

### **ライティング・マテリアル**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofEnableLighting()` | `tc::enableLighting()` | 3DPrimitivesExample | |
| `ofDisableLighting()` | `tc::disableLighting()` | 3DPrimitivesExample | |
| `ofLight` | `tc::Light` | 3DPrimitivesExample | |
| `light.setDirectional(dir)` | `light.setDirectional(dir)` | 3DPrimitivesExample | |
| `light.setPointLight()` | `light.setPoint(pos)` | 3DPrimitivesExample | |
| `light.setAmbientColor(c)` | `light.setAmbient(c)` | 3DPrimitivesExample | |
| `light.setDiffuseColor(c)` | `light.setDiffuse(c)` | 3DPrimitivesExample | |
| `light.setSpecularColor(c)` | `light.setSpecular(c)` | 3DPrimitivesExample | |
| `ofMaterial` | `tc::Material` | 3DPrimitivesExample | |
| - | `tc::Material::gold()` | 3DPrimitivesExample | プリセット |
| - | `tc::Material::silver()` | 3DPrimitivesExample | プリセット |
| - | `tc::Material::plastic(color)` | 3DPrimitivesExample | プリセット |

### **メッシュ**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofMesh` | `tc::Mesh` | 3DPrimitivesExample | |
| `mesh.addVertex(v)` | `mesh.addVertex(v)` | 3DPrimitivesExample | |
| `mesh.addColor(c)` | `mesh.addColor(c)` | 3DPrimitivesExample | |
| `mesh.addNormal(n)` | `mesh.addNormal(n)` | 3DPrimitivesExample | |
| `mesh.addIndex(i)` | `mesh.addIndex(i)` | 3DPrimitivesExample | |
| `mesh.draw()` | `mesh.draw()` | 3DPrimitivesExample | |
| `mesh.drawWireframe()` | `mesh.drawWireframe()` | 3DPrimitivesExample | |

### **FBO**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofFbo` | `tc::Fbo` | fboExample | |
| `fbo.allocate(w, h)` | `fbo.allocate(w, h)` | fboExample | |
| `fbo.begin()` | `fbo.begin()` | fboExample | |
| `fbo.end()` | `fbo.end()` | fboExample | |
| `fbo.draw(x, y)` | `fbo.draw(x, y)` | fboExample | |

### **シェーダー**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofShader` | `tc::Shader` | shaderExample | |
| `shader.load(vert, frag)` | `shader.load(vert, frag)` | shaderExample | |
| `shader.begin()` | `shader.begin()` | shaderExample | |
| `shader.end()` | `shader.end()` | shaderExample | |
| `shader.setUniform*()` | `shader.setUniform*()` | shaderExample | |

### **サウンド**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofSoundPlayer` | `tc::Sound` | soundPlayerExample | |
| `sound.load("file.wav")` | `sound.load("file.wav")` | soundPlayerExample | |
| `sound.play()` | `sound.play()` | soundPlayerExample | |
| `sound.stop()` | `sound.stop()` | soundPlayerExample | |
| `sound.setVolume(v)` | `sound.setVolume(v)` | soundPlayerExample | |
| `sound.setLoop(true)` | `sound.setLoop(true)` | soundPlayerExample | |
| `ofSoundStream` | `tc::SoundStream` | micInputExample | マイク入力 |

### **ビデオ**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofVideoGrabber` | `tc::VideoGrabber` | videoGrabberExample | |
| `grabber.setup(w, h)` | `grabber.setup(w, h)` | videoGrabberExample | |
| `grabber.update()` | `grabber.update()` | videoGrabberExample | |
| `grabber.draw(x, y)` | `grabber.draw(x, y)` | videoGrabberExample | |
| `grabber.isFrameNew()` | `grabber.isFrameNew()` | videoGrabberExample | |

### **入出力**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofSystemLoadDialog()` | `tc::systemLoadDialog()` | fileDialogExample | |
| `ofSystemSaveDialog()` | `tc::systemSaveDialog()` | fileDialogExample | |
| `ofToDataPath("file")` | `tc::getDataPath() + "file"` | imageLoaderExample | |
| `ofLoadJson(path)` | `tc::loadJson(path)` | jsonXmlExample | nlohmann/json |
| `ofSaveJson(path, json)` | `tc::saveJson(path, json)` | jsonXmlExample | |
| - | `tc::loadXml(path)` | jsonXmlExample | pugixml |
| - | `tc::saveXml(path, xml)` | jsonXmlExample | |

### **ネットワーク**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofxTCPClient` | `tc::TcpClient` | tcpExample | |
| `ofxTCPServer` | `tc::TcpServer` | tcpExample | |
| `ofxUDPManager` | `tc::UdpSocket` | udpExample | |

### **GUI**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofxGui` / `ofxImGui` | Dear ImGui（組み込み） | imguiExample | `tc::imgui::Begin()` 等 |

### **イベント**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofEvent<T>` | `tc::Event<T>` | eventsExample | |
| `ofAddListener(event, obj, &method)` | `tc::EventListener` | eventsExample | RAII 形式 |

### **シーングラフ**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofNode` | `tc::Node` | ofNodeExample | shared_ptr ベース |
| `node.setPosition(x, y, z)` | `node->setPosition(x, y, z)` | ofNodeExample | |
| `node.setParent(parent)` | `parent->addChild(child)` | ofNodeExample | |
| - | `tc::RectNode` | hitTestExample | 2D UI 用、ヒットテスト対応 |

### **ログ**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofLog()` | `tc::tcLogNotice("module")` | fileDialogExample | モジュール名はオプション |
| `ofLogVerbose()` | `tc::tcLogVerbose("module")` | ofNodeExample | |
| `ofLogWarning()` | `tc::tcLogWarning("module")` | tcpExample | |
| `ofLogError()` | `tc::tcLogError("module")` | tcpExample | |

### **スレッド**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofThread` | `tc::Thread` | threadExample | |
| - | `tc::ThreadChannel` | threadChannelExample | スレッドセーフなキュー |

### **シリアル通信**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofSerial` | `tc::Serial` | serialExample | |

### **タイマー**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| - | `node->callAfter(sec, func)` | timerExample | 遅延実行 |
| - | `node->callEvery(sec, func)` | timerExample | 繰り返し実行 |

### **アドオン**

| openFrameworks | TrussC | Example | 備考 |
|:---|:---|:---|:---|
| `ofxAddon` | `tcxAddon` | - | プレフィックス |
| `addons.make` に記述 | `use_addon()` | - | CMake 関数 |
| `ofxGui` | `tcxGui` | - | GUI |
| `ofxOsc` | `tcxOsc` | - | OSC 通信 |
| `ofxBox2d` | `tcxBox2d` | - | 2D 物理エンジン |
| - | `tcx::addonname::Class` | - | 名前空間規約 |