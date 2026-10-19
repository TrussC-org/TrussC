#pragma once

// Internal, synchronous test observer. No environment reads or production
// logging; captureSubmission installs it only for --diag. Pointers are borrowed
// identities for the callback and must not be dereferenced or retained.
namespace trussc::internal {
struct D3D11CaptureTrace {
    bool isMain = false, succeeded = false;
    int fbWidth = 0, fbHeight = 0;
    int lastWidth = 0, lastHeight = 0, lastFormat = 0, lastSamples = 0;
    const void* lastRenderView = nullptr;
    const void* lastResolveView = nullptr;
    const void* selectedView = nullptr;
    const void* selectedTexture = nullptr;
    unsigned width = 0, height = 0, format = 0, samples = 0;
    unsigned reconciledWidth = 0, reconciledHeight = 0;
    int pixelWidth = 0, pixelHeight = 0;
};
using D3D11CaptureTraceHook = void (*)(const D3D11CaptureTrace&);
void setD3D11CaptureTraceForTests(D3D11CaptureTraceHook hook);
} // namespace trussc::internal
