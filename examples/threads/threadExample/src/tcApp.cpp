#include "tcApp.h"

// ---------------------------------------------------------------------------
// setup - 初期化
// ---------------------------------------------------------------------------
void tcApp::setup() {
    threadedObject.setup();
    doLock = false;

    printf("main thread id: %llu\n", (unsigned long long)std::hash<std::thread::id>{}(getMainThreadId()));
}

// ---------------------------------------------------------------------------
// update - 更新
// ---------------------------------------------------------------------------
void tcApp::update() {
    if (doLock) {
        threadedObject.update();
    } else {
        // ロックなし更新（ティアリングが発生する可能性）
        // クラッシュはしないが、データの整合性が保証されない
        threadedObject.updateNoLock();
    }
}

// ---------------------------------------------------------------------------
// draw - 描画
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.1f, 0.1f, 0.1f);

    // スレッドで生成したデータを描画
    setColor(1.0f);
    threadedObject.draw(20, 100);

    // フレーム情報を表示
    auto appFrame = getFrameCount();
    auto threadFrame = threadedObject.getThreadFrameNum();

    setColor(1.0f, 0.4f, 0.4f);
    drawBitmapString("app frame: " + toString(appFrame), 20, 20);
    drawBitmapString("thread frame: " + toString(threadFrame), 20, 35);
    drawBitmapString("diff: " + toString((int64_t)appFrame - threadFrame), 20, 50);

    setColor(0.78f, 0.78f, 0.78f);
    drawBitmapString("a: starts the thread", 20, 320);
    drawBitmapString("s: stops the thread", 20, 335);
    drawBitmapString("l: turns lock on", 20, 350);
    drawBitmapString("n: turns lock off (tearing)", 20, 365);

    // ロック状態を表示
    setColor(0.4f, 1.0f, 0.4f);
    drawBitmapString(doLock ? "Mode: LOCKED (safe)" : "Mode: NO LOCK (may tear)", 20, 395);
}

// ---------------------------------------------------------------------------
// keyPressed - キー入力
// ---------------------------------------------------------------------------
void tcApp::keyPressed(int key) {
    if (isMainThread()) {
        printf("[keyPressed] processed in main thread\n");
    } else {
        printf("[keyPressed] processed in other thread\n");
    }

    if (key == 'a' || key == 'A') {
        threadedObject.start();
        printf("Thread started\n");
    } else if (key == 's' || key == 'S') {
        threadedObject.stop();
        printf("Thread stopped\n");
    } else if (key == 'n' || key == 'N') {
        doLock = false;
        printf("Lock OFF - may see tearing\n");
    } else if (key == 'l' || key == 'L') {
        doLock = true;
        printf("Lock ON - safe mode\n");
    }
}
