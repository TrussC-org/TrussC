// =============================================================================
// macOS プラットフォーム固有機能
// =============================================================================

#include "TrussC.h"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <CoreGraphics/CoreGraphics.h>

// sokol_app の swapchain 取得関数
#include "sokol_app.h"

namespace trussc {
namespace platform {

float getDisplayScaleFactor() {
    CGDirectDisplayID displayId = CGMainDisplayID();
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayId);

    if (!mode) {
        return 1.0f;
    }

    size_t pixelWidth = CGDisplayModeGetPixelWidth(mode);
    size_t pointWidth = CGDisplayModeGetWidth(mode);

    CGDisplayModeRelease(mode);

    if (pointWidth == 0) {
        return 1.0f;
    }

    return (float)pixelWidth / (float)pointWidth;
}

void setWindowSize(int width, int height) {
    // メインウィンドウを取得
    NSWindow* window = [[NSApplication sharedApplication] mainWindow];
    if (!window) {
        // mainWindow が nil の場合、最初のウィンドウを試す
        NSArray* windows = [[NSApplication sharedApplication] windows];
        if (windows.count > 0) {
            window = windows[0];
        }
    }

    if (window) {
        // 現在のフレームを取得
        NSRect frame = [window frame];

        // コンテンツ領域のサイズを変更（タイトルバーは維持）
        NSRect contentRect = [window contentRectForFrameRect:frame];
        contentRect.size.width = width;
        contentRect.size.height = height;

        // 新しいフレームを計算（左上を基準に維持）
        NSRect newFrame = [window frameRectForContentRect:contentRect];
        newFrame.origin.y = frame.origin.y + frame.size.height - newFrame.size.height;

        [window setFrame:newFrame display:YES animate:NO];
    }
}

std::string getExecutablePath() {
    NSString* path = [[NSBundle mainBundle] executablePath];
    return std::string([path UTF8String]);
}

std::string getExecutableDir() {
    NSString* path = [[NSBundle mainBundle] executablePath];
    NSString* dir = [path stringByDeletingLastPathComponent];
    return std::string([dir UTF8String]) + "/";
}

// ---------------------------------------------------------------------------
// スクリーンショット機能（Metal API を使用）
// ---------------------------------------------------------------------------

bool captureWindow(Pixels& outPixels) {
    // sokol_app から現在の swapchain を取得
    sapp_swapchain sc = sapp_get_swapchain();
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)sc.metal.current_drawable;
    if (!drawable) {
        tcLogError() << "[Screenshot] Metal drawable が取得できません";
        return false;
    }

    id<MTLTexture> texture = drawable.texture;
    if (!texture) {
        tcLogError() << "[Screenshot] Metal テクスチャが取得できません";
        return false;
    }

    NSUInteger width = texture.width;
    NSUInteger height = texture.height;

    // Pixels を確保
    outPixels.allocate((int)width, (int)height, 4);

    // Metal テクスチャからピクセルを読み取る
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    NSUInteger bytesPerRow = width * 4;

    [texture getBytes:outPixels.getData()
          bytesPerRow:bytesPerRow
           fromRegion:region
          mipmapLevel:0];

    // BGRA -> RGBA に変換（Metal は通常 BGRA フォーマット）
    unsigned char* data = outPixels.getData();
    for (NSUInteger i = 0; i < width * height; i++) {
        unsigned char temp = data[i * 4 + 0];  // B
        data[i * 4 + 0] = data[i * 4 + 2];     // R -> B位置
        data[i * 4 + 2] = temp;                 // B -> R位置
    }

    return true;
}

bool saveScreenshot(const std::filesystem::path& path) {
    // まず Pixels にキャプチャ
    Pixels pixels;
    if (!captureWindow(pixels)) {
        return false;
    }

    // CGImage を作成
    int width = pixels.getWidth();
    int height = pixels.getHeight();
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

    CGContextRef context = CGBitmapContextCreate(
        pixels.getData(),
        width, height,
        8,                          // bitsPerComponent
        width * 4,                  // bytesPerRow
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
    );

    if (!context) {
        CGColorSpaceRelease(colorSpace);
        tcLogError() << "[Screenshot] CGContext の作成に失敗しました";
        return false;
    }

    CGImageRef cgImage = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);

    if (!cgImage) {
        tcLogError() << "[Screenshot] CGImage の作成に失敗しました";
        return false;
    }

    // NSBitmapImageRep に変換
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
    CGImageRelease(cgImage);

    if (!rep) {
        tcLogError() << "[Screenshot] NSBitmapImageRep の作成に失敗しました";
        return false;
    }

    // ファイル拡張子から形式を判定
    std::string ext = path.extension().string();
    NSBitmapImageFileType fileType = NSBitmapImageFileTypePNG;
    if (ext == ".jpg" || ext == ".jpeg") {
        fileType = NSBitmapImageFileTypeJPEG;
    } else if (ext == ".tiff" || ext == ".tif") {
        fileType = NSBitmapImageFileTypeTIFF;
    } else if (ext == ".bmp") {
        fileType = NSBitmapImageFileTypeBMP;
    } else if (ext == ".gif") {
        fileType = NSBitmapImageFileTypeGIF;
    }

    // ファイルに保存
    NSData* data = [rep representationUsingType:fileType properties:@{}];
    if (!data) {
        tcLogError() << "[Screenshot] 画像データの作成に失敗しました";
        return false;
    }

    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    BOOL success = [data writeToFile:nsPath atomically:YES];

    if (success) {
        tcLogVerbose() << "[Screenshot] 保存完了: " << path;
    } else {
        tcLogError() << "[Screenshot] 保存に失敗しました: " << path;
    }

    return success;
}

} // namespace platform
} // namespace trussc

#endif // __APPLE__
