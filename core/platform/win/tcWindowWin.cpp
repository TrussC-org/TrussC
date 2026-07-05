// =============================================================================
// tcWindowWin.cpp - Secondary window native glue (Windows / D3D11)
// =============================================================================
// One HWND + flip-model DXGI swapchain + per-window timer per secondary window.
// The timer fires WM_TIMER at the window's monitor refresh rate; those messages
// are delivered on the main thread through sokol_app's own message pump (the
// main window keeps owning sokol_app and its frame callback), so every window
// renders synchronously on the main thread — the Windows analogue of macOS's
// per-window CADisplayLink. A minimized or fully occluded window skips
// rendering/present entirely (Present never blocks here), so it can never stall
// the others.
//
// All rendering shares the one sokol_gfx context (the device is sokol_app's,
// queried via sg_d3d11_device()); each window gets its own sokol_gl context,
// same pattern as Fbo and as tcWindowMac.mm. This file mirrors that file's
// structure so the two read as siblings.

#include "TrussC.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <d3d11.h>
#include <dxgi1_2.h>    // IDXGIFactory2 / CreateSwapChainForHwnd / DXGI_SWAP_CHAIN_DESC1

using namespace trussc;

namespace {

// Secondary windows render at 10-bit color to match the main window (sokol_app
// is patched to DXGI_FORMAT_R10G10B10A2_UNORM / SAPP_PIXELFORMAT_RGB10A2). The
// sgl context, swapchain and MSAA/backbuffer textures all agree on this format.
constexpr DXGI_FORMAT   kSwapFormat  = DXGI_FORMAT_R10G10B10A2_UNORM;
constexpr DXGI_FORMAT   kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
constexpr sg_pixel_format kSgColor   = SG_PIXELFORMAT_RGB10A2;

constexpr wchar_t kWindowClass[] = L"TrussCSecondaryWindow";
constexpr UINT_PTR kTickTimerId  = 1;

// ---------------------------------------------------------------------------
// Native per-window state
// ---------------------------------------------------------------------------
struct NativeWindow {
    Window* owner = nullptr;              // back-pointer (owner outlives native)
    HWND hwnd = nullptr;
    IDXGISwapChain1* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;      // backbuffer view (resolve target when MSAA)
    ID3D11Texture2D* msaaTex = nullptr;         // MSAA color (when sampleCount > 1)
    ID3D11RenderTargetView* msaaRtv = nullptr;  // MSAA render view
    ID3D11Texture2D* depthTex = nullptr;        // depth-stencil, drawable-sized
    ID3D11DepthStencilView* dsv = nullptr;
    int sampleCount = 1;
    int texW = 0, texH = 0;               // size the views were made for
    UINT dpi = 96;                        // per-monitor DPI (96 = 100%)
    int buttonMask = 0;                   // pressed mouse buttons (bit per button)
    bool mouseTracked = false;            // TrackMouseEvent armed (for WM_MOUSELEAVE)
    bool occluded = false;                // Present() returned DXGI_STATUS_OCCLUDED
    bool loggedOccluded = false;
    bool loggedGeometry = false;
};

// The swapchain provider handed to WindowContext::acquireSwapchain. Unlike
// Metal there is no per-frame drawable to acquire: the flip-model backbuffer
// view is persistent (recreated only on resize), so this just hands back the
// current views + size for the pass.
sg_swapchain acquireSecondarySwapchain(void* user) {
    auto* nw = static_cast<NativeWindow*>(user);
    sg_swapchain sc = {};
    if (!nw || nw->rtv == nullptr) return sc;
    sc.width = nw->texW;
    sc.height = nw->texH;
    sc.sample_count = nw->sampleCount;
    sc.color_format = kSgColor;
    sc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    if (nw->sampleCount > 1) {
        sc.d3d11.render_view = nw->msaaRtv;   // draw into MSAA, resolve into backbuffer
        sc.d3d11.resolve_view = nw->rtv;
    } else {
        sc.d3d11.render_view = nw->rtv;
    }
    sc.d3d11.depth_stencil_view = nw->dsv;
    return sc;
}

// Query the refresh rate (Hz) of the monitor the window is on. Falls back to 60.
int monitorRefreshHz(HWND hwnd) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi = {}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        DEVMODEW dm = {}; dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
            dm.dmDisplayFrequency > 1) {
            return (int)dm.dmDisplayFrequency;
        }
    }
    return 60;
}

// Create the backbuffer view + (optional) MSAA color + depth-stencil for a size.
bool createRenderTargets(NativeWindow* nw, int w, int h) {
    ID3D11Device* dev = (ID3D11Device*)sg_d3d11_device();
    if (!dev || !nw->swapChain) return false;

    // Backbuffer render target view (the swapchain's buffer 0; flip model keeps
    // buffer 0 as the current back buffer every frame, so this view is reusable).
    ID3D11Texture2D* backbuffer = nullptr;
    if (FAILED(nw->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backbuffer)) || !backbuffer) {
        logError("Window") << "GetBuffer(0) failed";
        return false;
    }
    HRESULT hr = dev->CreateRenderTargetView(backbuffer, nullptr, &nw->rtv);
    backbuffer->Release();
    if (FAILED(hr)) { logError("Window") << "CreateRenderTargetView failed"; return false; }

    // Common desc for MSAA and depth-stencil textures.
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.SampleDesc.Count = (UINT)nw->sampleCount;
    td.SampleDesc.Quality = (UINT)(nw->sampleCount > 1 ? D3D11_STANDARD_MULTISAMPLE_PATTERN : 0);

    // MSAA color target (flip-model swapchains are always single-sample, so MSAA
    // is a separate texture that sokol_gfx resolves into the backbuffer).
    if (nw->sampleCount > 1) {
        td.Format = kSwapFormat;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &nw->msaaTex)) ||
            FAILED(dev->CreateRenderTargetView(nw->msaaTex, nullptr, &nw->msaaRtv))) {
            logError("Window") << "MSAA target creation failed";
            return false;
        }
    }

    // Depth-stencil (matches the color sample count).
    td.Format = kDepthFormat;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &nw->depthTex)) ||
        FAILED(dev->CreateDepthStencilView(nw->depthTex, nullptr, &nw->dsv))) {
        logError("Window") << "depth-stencil creation failed";
        return false;
    }

    nw->texW = w; nw->texH = h;
    return true;
}

void releaseRenderTargets(NativeWindow* nw) {
    if (nw->dsv)     { nw->dsv->Release();     nw->dsv = nullptr; }
    if (nw->depthTex){ nw->depthTex->Release();nw->depthTex = nullptr; }
    if (nw->msaaRtv) { nw->msaaRtv->Release(); nw->msaaRtv = nullptr; }
    if (nw->msaaTex) { nw->msaaTex->Release(); nw->msaaTex = nullptr; }
    if (nw->rtv)     { nw->rtv->Release();     nw->rtv = nullptr; }
}

// Resize the swapchain buffers and recreate the views (WM_SIZE / DPI change).
void resizeSwapchain(NativeWindow* nw, int w, int h) {
    if (!nw->swapChain || w <= 0 || h <= 0) return;
    releaseRenderTargets(nw);
    // Flip-model ResizeBuffers requires that EVERY reference to the back buffers
    // be released first — including the render target still bound on sokol's
    // (shared, single-threaded) immediate context from the last pass. Unbind and
    // flush before resizing, or ResizeBuffers fails and the next present crashes.
    ID3D11DeviceContext* dc = (ID3D11DeviceContext*)sg_d3d11_device_context();
    if (dc) { dc->OMSetRenderTargets(0, nullptr, nullptr); dc->Flush(); }
    HRESULT hr = nw->swapChain->ResizeBuffers(0, (UINT)w, (UINT)h, kSwapFormat, 0);
    if (FAILED(hr)) { logError("Window") << "ResizeBuffers failed"; return; }
    createRenderTargets(nw, w, h);
}

bool createSwapchain(NativeWindow* nw, int w, int h) {
    ID3D11Device* dev = (ID3D11Device*)sg_d3d11_device();
    if (!dev) { logError("Window") << "no D3D11 device (sg not set up?)"; return false; }

    // Reach the DXGI factory that made sokol_app's device.
    IDXGIDevice* dxgiDev = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    bool ok = false;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) &&
        SUCCEEDED(dxgiDev->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory))) {

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = (UINT)w;
        desc.Height = (UINT)h;
        desc.Format = kSwapFormat;
        desc.SampleDesc.Count = 1;   // flip model: swapchain itself is single-sample
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        if (SUCCEEDED(factory->CreateSwapChainForHwnd(dev, nw->hwnd, &desc, nullptr, nullptr, &nw->swapChain))) {
            factory->MakeWindowAssociation(nw->hwnd, DXGI_MWA_NO_ALT_ENTER);
            ok = createRenderTargets(nw, w, h);
        } else {
            logError("Window") << "CreateSwapChainForHwnd failed";
        }
    } else {
        logError("Window") << "could not reach IDXGIFactory2 from the device";
    }
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgiDev) dxgiDev->Release();
    return ok;
}

// ---------------------------------------------------------------------------
// Per-frame tick (WM_TIMER target) — mirrors tcWindowMac.mm's -tick:
// ---------------------------------------------------------------------------
void tickWindow(NativeWindow* nw) {
    if (!nw || !nw->owner || !nw->swapChain) return;
    Window& win = *nw->owner;
    auto& ctx = win.context();

    // Minimized => fully idle. The timer keeps firing, so we resume on restore.
    if (IsIconic(nw->hwnd)) return;

    // Occluded (fully covered) => keep the main thread free: poll cheaply with a
    // present-test and skip all rendering until the window is visible again.
    // This is the D3D11 analogue of mac skipping while occlusionState is hidden.
    if (nw->occluded) {
        if (nw->swapChain->Present(0, DXGI_PRESENT_TEST) == S_OK) {
            nw->occluded = false;
            nw->loggedOccluded = false;
        } else {
            return;
        }
    }

    // Keep framebuffer size / DPI in sync with the client area.
    RECT rc; GetClientRect(nw->hwnd, &rc);
    int fbw = rc.right - rc.left, fbh = rc.bottom - rc.top;
    if (fbw <= 0 || fbh <= 0) return;
    UINT dpi = GetDpiForWindow(nw->hwnd);
    if (dpi == 0) dpi = 96;
    nw->dpi = dpi;
    float scale = (float)dpi / 96.0f;
    if (fbw != nw->texW || fbh != nw->texH) resizeSwapchain(nw, fbw, fbh);
    ctx.fbWidth = fbw; ctx.fbHeight = fbh; ctx.dpiScale = scale;

    // Keep a RectNode root in sync with the window's LOGICAL size (same contract
    // as the main App on SAPP_EVENTTYPE_RESIZED). One ratio source (dpiScale)
    // drives both this projection size and the mouse conversion below.
    float logicalW = (float)fbw / scale, logicalH = (float)fbh / scale;
    win.syncRootSize(logicalW, logicalH);
    if (!nw->loggedGeometry) {
        nw->loggedGeometry = true;
        logNotice("Window") << "geometry: client=" << fbw << "x" << fbh
            << " dpi=" << dpi << " scale=" << scale
            << " logical=" << (int)logicalW << "x" << (int)logicalH
            << " sampleCount=" << nw->sampleCount;
    }

    // --- render this window's tree with its context active ---
    auto* prev = internal::currentWindowCtx;
    internal::currentWindowCtx = &ctx;

    // Own sokol_gl context, created lazily on the first tick (sgl is set up by
    // then). Same lifecycle caveats as Fbo contexts on sgl resize.
    if (ctx.swapchainTarget.context.id == SG_INVALID_ID) {
        sgl_context_desc_t cdesc = {};
        cdesc.max_vertices = internal::sglMaxVertices;
        cdesc.max_commands = internal::sglMaxCommands;
        cdesc.color_format = kSgColor;
        cdesc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        cdesc.sample_count = nw->sampleCount;
        ctx.swapchainTarget.context = sgl_make_context(&cdesc);
    }
    sgl_set_context(ctx.swapchainTarget.context);
    sgl_defaults();

    beginFrame();

    win.events().update.notify();
    win.tickTree();

    clear(win.clearColor_.r, win.clearColor_.g, win.clearColor_.b, win.clearColor_.a);
    win.events().draw.notify();
    win.drawTreeNow();

    present();   // sg_end_pass + sg_commit (draws THIS window's sgl context)
    win.events().afterFrame.notify();

    sgl_set_context(sgl_default_context());
    internal::currentWindowCtx = prev;

    // Present the flip-model swapchain. SyncInterval 0: never block the main
    // thread waiting on this window's vblank (the main window drives overall
    // pacing; a blocking wait here would halve the main window's frame rate).
    HRESULT hr = nw->swapChain->Present(0, 0);
    if (hr == DXGI_STATUS_OCCLUDED) {
        nw->occluded = true;
        if (!nw->loggedOccluded) {
            nw->loggedOccluded = true;
            logNotice("Window") << "second window occluded - skipping frames (no stall)";
        }
    }
}

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------
void fillMods(bool* shift, bool* ctrl, bool* alt, bool* super) {
    *shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    *ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    *alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;
    *super = ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0;
}

// Physical client pixels -> logical points (the window's coordinate system).
Vec2 logicalPos(NativeWindow* nw, int px, int py) {
    float scale = (float)nw->dpi / 96.0f;
    return Vec2((float)px / scale, (float)py / scale);
}

void mouseButtonEvent(NativeWindow* nw, int button, bool down, int px, int py) {
    if (!nw->owner) return;
    Window& win = *nw->owner;
    auto& ctx = win.context();
    Vec2 p = logicalPos(nw, px, py);

    auto* prev = internal::currentWindowCtx;
    internal::currentWindowCtx = &ctx;
    ctx.mouseX = p.x; ctx.mouseY = p.y;

    MouseEventArgs e;
    e.pos = p; e.globalPos = p; e.button = button;
    fillMods(&e.shift, &e.ctrl, &e.alt, &e.super);
    e.syncLegacy();

    if (down) {
        if (nw->buttonMask == 0) SetCapture(nw->hwnd);   // keep drag events past the edge
        nw->buttonMask |= (1 << button);
        ctx.mousePressed = true;
        ctx.mouseButton = button;
        win.events().mousePressed.notify(e);
        if (win.app_) win.app_->mousePressed(e);
        win.dispatchMousePressToTree(e);
    } else {
        nw->buttonMask &= ~(1 << button);
        if (nw->buttonMask == 0) {
            ReleaseCapture();
            ctx.mousePressed = false;
            ctx.mouseButton = -1;
        }
        win.events().mouseReleased.notify(e);
        if (win.app_) win.app_->mouseReleased(e);
        win.dispatchMouseReleaseToTree(e);
    }
    internal::currentWindowCtx = prev;
}

void mouseMoveEvent(NativeWindow* nw, int px, int py) {
    if (!nw->owner) return;
    Window& win = *nw->owner;
    auto& ctx = win.context();

    if (!nw->mouseTracked) {
        TRACKMOUSEEVENT tme = {}; tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE; tme.hwndTrack = nw->hwnd;
        TrackMouseEvent(&tme);
        nw->mouseTracked = true;
    }

    Vec2 p = logicalPos(nw, px, py);
    auto* prev = internal::currentWindowCtx;
    internal::currentWindowCtx = &ctx;
    ctx.pmouseX = ctx.mouseX; ctx.pmouseY = ctx.mouseY;
    ctx.mouseX = p.x; ctx.mouseY = p.y;
    Vec2 delta(p.x - ctx.pmouseX, p.y - ctx.pmouseY);

    if (nw->buttonMask != 0) {
        // Dragging: report the primary held button.
        int btn = 0;
        if      (nw->buttonMask & (1 << 0)) btn = 0;
        else if (nw->buttonMask & (1 << 1)) btn = 1;
        else if (nw->buttonMask & (1 << 2)) btn = 2;
        MouseDragEventArgs e;
        e.pos = p; e.globalPos = p; e.delta = delta; e.globalDelta = delta; e.button = btn;
        fillMods(&e.shift, &e.ctrl, &e.alt, &e.super);
        e.syncLegacy();
        win.events().mouseDragged.notify(e);
        if (win.app_) win.app_->mouseDragged(e);
    } else {
        MouseMoveEventArgs e;
        e.pos = p; e.globalPos = p; e.delta = delta; e.globalDelta = delta;
        fillMods(&e.shift, &e.ctrl, &e.alt, &e.super);
        e.syncLegacy();
        win.events().mouseMoved.notify(e);
        if (win.app_) win.app_->mouseMoved(e);
    }
    // Hover is resolved from ctx.mouseX/Y in tickTree (updateHoverState), same
    // as macOS — no per-move node dispatch needed here.
    internal::currentWindowCtx = prev;
}

void mouseScrollEvent(NativeWindow* nw, int screenX, int screenY, float wheel) {
    if (!nw->owner) return;
    Window& win = *nw->owner;
    auto& ctx = win.context();
    POINT pt = { screenX, screenY };
    ScreenToClient(nw->hwnd, &pt);
    Vec2 p = logicalPos(nw, pt.x, pt.y);

    auto* prev = internal::currentWindowCtx;
    internal::currentWindowCtx = &ctx;
    ScrollEventArgs e;
    e.pos = p; e.globalPos = p; e.scroll = Vec2(0.0f, wheel);
    fillMods(&e.shift, &e.ctrl, &e.alt, &e.super);
    e.syncLegacy();
    win.events().mouseScrolled.notify(e);
    if (win.app_) win.app_->mouseScrolled(e);
    internal::currentWindowCtx = prev;
}

void keyEvent(NativeWindow* nw, int vk, bool down, bool isRepeat) {
    if (!nw->owner) return;
    Window& win = *nw->owner;
    auto& ctx = win.context();
    // sokol keycodes for letters/digits equal their uppercase ASCII, which also
    // equals the Win32 virtual-key code — so the common case maps directly.
    // The full SAPP_KEYCODE table is Phase 2 polish (matches tcWindowMac.mm).
    int key = vk;

    auto* prev = internal::currentWindowCtx;
    internal::currentWindowCtx = &ctx;
    if (down) ctx.keysPressed.insert(key); else ctx.keysPressed.erase(key);
    KeyEventArgs e;
    e.key = key; e.isRepeat = isRepeat;
    fillMods(&e.shift, &e.ctrl, &e.alt, &e.super);
    if (down) {
        win.events().keyPressed.notify(e);
        if (win.app_) win.app_->keyPressed(e);
    } else {
        win.events().keyReleased.notify(e);
        if (win.app_) win.app_->keyReleased(e);
    }
    internal::currentWindowCtx = prev;
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // nw is stashed in GWLP_USERDATA at WM_NCCREATE.
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    auto* nw = (NativeWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!nw) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_TIMER:
            if (wParam == kTickTimerId) { tickWindow(nw); return 0; }
            break;

        case WM_LBUTTONDOWN: mouseButtonEvent(nw, 0, true,  GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_LBUTTONUP:   mouseButtonEvent(nw, 0, false, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_RBUTTONDOWN: mouseButtonEvent(nw, 1, true,  GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_RBUTTONUP:   mouseButtonEvent(nw, 1, false, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_MBUTTONDOWN: mouseButtonEvent(nw, 2, true,  GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_MBUTTONUP:   mouseButtonEvent(nw, 2, false, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;

        case WM_MOUSEMOVE:
            mouseMoveEvent(nw, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSELEAVE:
            // Park the cursor offscreen so the next tick's updateHoverState clears
            // this window's hover (hoveredNode lives in ITS WindowContext).
            nw->mouseTracked = false;
            if (nw->owner) {
                auto& ctx = nw->owner->context();
                ctx.pmouseX = ctx.mouseX; ctx.pmouseY = ctx.mouseY;
                ctx.mouseX = -1.0f; ctx.mouseY = -1.0f;
            }
            return 0;

        case WM_MOUSEWHEEL:
            mouseScrollEvent(nw, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                             (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            keyEvent(nw, (int)wParam, true, (lParam & 0x40000000) != 0);
            if (msg == WM_SYSKEYDOWN) break;   // let DefWindowProc handle system keys
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            keyEvent(nw, (int)wParam, false, false);
            if (msg == WM_SYSKEYUP) break;
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED && nw->owner) {
                int w = LOWORD(lParam), h = HIWORD(lParam);
                if (w > 0 && h > 0) {
                    resizeSwapchain(nw, w, h);
                    UINT dpi = GetDpiForWindow(hwnd);
                    if (dpi == 0) dpi = 96;
                    nw->dpi = dpi;
                    float scale = (float)dpi / 96.0f;
                    auto& ctx = nw->owner->context();
                    ctx.fbWidth = w; ctx.fbHeight = h; ctx.dpiScale = scale;
                    // Fires windowResized (App RectNode already resized inside).
                    nw->owner->syncRootSize((float)w / scale, (float)h / scale);
                }
            }
            return 0;

        case WM_DPICHANGED: {
            // Per-monitor DPI v2: adopt the suggested rect; the next tick picks
            // up the new dpiScale and resizes the swapchain to match.
            nw->dpi = HIWORD(wParam);
            RECT* r = (RECT*)lParam;
            SetWindowPos(hwnd, nullptr, r->left, r->top,
                         r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_CLOSE:
            // Same teardown semantics as mac's windowWillClose: the App cleans up,
            // the native window dies, the main window is unaffected. close()
            // deletes nw, so touch nothing afterwards.
            nw->owner->close();
            return 0;

        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ensureWindowClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);  // IDC_ARROW is an atom; A/W value identical
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);
    registered = true;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

} // namespace

// ---------------------------------------------------------------------------
// Window methods (Windows implementations)
// ---------------------------------------------------------------------------
namespace trussc {

Window::~Window() { close(); }

void Window::close() {
    auto* nw = static_cast<NativeWindow*>(native_);
    if (!nw) return;
    native_ = nullptr;             // re-entrancy guard (WM_CLOSE)
    ctx_.acquireSwapchain = nullptr;
    ctx_.acquireSwapchainUser = nullptr;

    if (nw->hwnd) {
        KillTimer(nw->hwnd, kTickTimerId);
        SetWindowLongPtrW(nw->hwnd, GWLP_USERDATA, 0);  // stop routing to a dying nw
        DestroyWindow(nw->hwnd);
        nw->hwnd = nullptr;
    }
    releaseRenderTargets(nw);
    if (nw->swapChain) { nw->swapChain->Release(); nw->swapChain = nullptr; }

    if (ctx_.swapchainTarget.context.id != SG_INVALID_ID) {
        sgl_destroy_context(ctx_.swapchainTarget.context);
        ctx_.swapchainTarget.context.id = SG_INVALID_ID;
        ctx_.swapchainTarget.cache.clear();
    }
    nw->owner = nullptr;
    delete nw;

    if (app_) {
        app_->exit();
        app_->cleanup();
        internal::attachedApps.erase(app_.get());
        app_.reset();
        ctx_.rootNode = nullptr;
    }
}

void Window::setTitle(const std::string& title) {
    auto* nw = static_cast<NativeWindow*>(native_);
    if (nw && nw->hwnd) SetWindowTextW(nw->hwnd, toWide(title).c_str());
}

int Window::getWidth() const {
    auto* nw = static_cast<NativeWindow*>(native_);
    if (!nw || !nw->hwnd) return 0;
    RECT rc; GetClientRect(nw->hwnd, &rc);
    float scale = (float)nw->dpi / 96.0f;
    return (int)((rc.right - rc.left) / scale);   // logical points
}

int Window::getHeight() const {
    auto* nw = static_cast<NativeWindow*>(native_);
    if (!nw || !nw->hwnd) return 0;
    RECT rc; GetClientRect(nw->hwnd, &rc);
    float scale = (float)nw->dpi / 96.0f;
    return (int)((rc.bottom - rc.top) / scale);
}

std::shared_ptr<Window> createWindow(const WindowSettings& settings) {
    if (headless::isActive()) {
        logError("Window") << "createWindow(): not available in headless mode";
        return nullptr;
    }
    if (sg_d3d11_device() == nullptr) {
        logError("Window") << "createWindow(): D3D11 device not ready (call after the app starts)";
        return nullptr;
    }

    ensureWindowClass();

    auto win = std::shared_ptr<Window>(new Window());
    auto* nw = new NativeWindow();
    nw->owner = win.get();
    nw->sampleCount = settings.sampleCount;

    DWORD style = settings.decorated ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    // Create at the logical size interpreted as 96 DPI first; once the window
    // lands on its monitor we know the real DPI and resize the client area to
    // the requested logical size * scale (per-monitor-v2, set by sokol_app).
    HWND hwnd = CreateWindowExW(
        0, kWindowClass, toWide(settings.title).c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, settings.width, settings.height,
        nullptr, nullptr, GetModuleHandleW(nullptr), nw);
    if (!hwnd) {
        logError("Window") << "CreateWindowExW failed";
        delete nw;
        return nullptr;
    }
    nw->hwnd = hwnd;

    // Resize the CLIENT area to the requested logical size at the monitor's DPI.
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    nw->dpi = dpi;
    float scale = (float)dpi / 96.0f;
    RECT want = { 0, 0, (LONG)(settings.width * scale), (LONG)(settings.height * scale) };
    AdjustWindowRectExForDpi(&want, style, FALSE, 0, dpi);
    SetWindowPos(hwnd, nullptr, 0, 0, want.right - want.left, want.bottom - want.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    RECT rc; GetClientRect(hwnd, &rc);
    int fbw = rc.right - rc.left, fbh = rc.bottom - rc.top;
    if (!createSwapchain(nw, fbw, fbh)) {
        DestroyWindow(hwnd);
        delete nw;
        return nullptr;
    }

    win->native_ = nw;
    win->ctx_.acquireSwapchain = &acquireSecondarySwapchain;
    win->ctx_.acquireSwapchainUser = nw;
    win->ctx_.fbWidth = fbw;
    win->ctx_.fbHeight = fbh;
    win->ctx_.dpiScale = scale;

    ShowWindow(hwnd, SW_SHOW);

    // Per-window tick at the monitor's refresh rate. WM_TIMER is delivered on
    // the main thread by sokol_app's message pump and is auto-coalesced by
    // Windows (at most one pending per timer — the depth-1 coalescing we want).
    UINT interval = (UINT)(1000 / monitorRefreshHz(hwnd));
    if (interval < 1) interval = 1;
    SetTimer(hwnd, kTickTimerId, interval, nullptr);

    return win;
}

} // namespace trussc

#endif // _WIN32
