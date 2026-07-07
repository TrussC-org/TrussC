// =============================================================================
// tcWindowMac.mm - Secondary window native glue (macOS)
// =============================================================================
// One NSWindow + CAMetalLayer + CADisplayLink per secondary window. The display
// link fires on the main run loop at the window's own display rate, so a
// window on a 60 Hz display ticks at 60 while the main window runs at 120.
// A fully occluded window skips drawable acquisition entirely (the PoC showed
// acquiring while occluded blocks ~1s and stalls the main thread).
// All rendering shares the one sokol_gfx context; each window gets its own
// sokol_gl context (same pattern as Fbo).

#include "TrussC.h"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CADisplayLink.h>

using namespace trussc;

// ---------------------------------------------------------------------------
// Native per-window state
// ---------------------------------------------------------------------------
@class TCWindowView;
@class TCWindowDelegate;

namespace {

struct NativeWindow {
    Window* owner = nullptr;              // back-pointer (owner outlives native)
    NSWindow* window = nil;
    TCWindowView* view = nil;
    TCWindowDelegate* delegate = nil;
    CAMetalLayer* layer = nil;
    CADisplayLink* link = nil;
    id<MTLTexture> depthTex = nil;        // depth-stencil, drawable-sized
    id<MTLTexture> msaaTex = nil;         // MSAA color (when sampleCount > 1)
    int sampleCount = 1;
    int texW = 0, texH = 0;               // size the aux textures were made for
    id<CAMetalDrawable> frameDrawable = nil;  // valid during one tick only
    bool loggedOccluded = false;
    bool loggedGeometry = false;
};

// The swapchain provider handed to WindowContext::acquireSwapchain. Returns
// the drawable acquired at the top of the current tick (never blocks here).
sg_swapchain acquireSecondarySwapchain(void* user) {
    auto* nw = static_cast<NativeWindow*>(user);
    sg_swapchain sc = {};
    if (!nw || nw->frameDrawable == nil) return sc;
    sc.width = nw->texW;
    sc.height = nw->texH;
    sc.sample_count = nw->sampleCount;
    sc.color_format = SG_PIXELFORMAT_BGRA8;
    sc.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    sc.metal.current_drawable = (__bridge const void*)nw->frameDrawable;
    sc.metal.depth_stencil_texture = (__bridge const void*)nw->depthTex;
    if (nw->sampleCount > 1) {
        sc.metal.msaa_color_texture = (__bridge const void*)nw->msaaTex;
    }
    return sc;
}

void ensureAuxTextures(NativeWindow* nw, int w, int h) {
    if (nw->texW == w && nw->texH == h && nw->depthTex != nil &&
        (nw->sampleCount <= 1 || nw->msaaTex != nil)) return;
    id<MTLDevice> dev = nw->layer.device;
    MTLTextureDescriptor* dd = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
        width:w height:h mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget;
    dd.storageMode = MTLStorageModePrivate;
    dd.sampleCount = nw->sampleCount;
    dd.textureType = nw->sampleCount > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    nw->depthTex = [dev newTextureWithDescriptor:dd];
    if (nw->sampleCount > 1) {
        MTLTextureDescriptor* md = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:w height:h mipmapped:NO];
        md.usage = MTLTextureUsageRenderTarget;
        md.storageMode = MTLStorageModePrivate;
        md.sampleCount = nw->sampleCount;
        md.textureType = MTLTextureType2DMultisample;
        nw->msaaTex = [dev newTextureWithDescriptor:md];
    }
    nw->texW = w; nw->texH = h;
}

} // namespace

// ---------------------------------------------------------------------------
// Content view: input forwarding into the window's WindowContext / CoreEvents
// ---------------------------------------------------------------------------
@interface TCWindowView : NSView
@property (assign) NativeWindow* nw;
@end

@implementation TCWindowView

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }   // top-left origin, matches TrussC coords

- (void)updateTrackingAreas {
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    // ActiveAlways: hover must work while the window is NOT key (the main
    // window keeps focus; a control-panel window still tracks the pointer).
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                 NSTrackingActiveAlways | NSTrackingEnabledDuringMouseDrag |
                 NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:area];
    [super updateTrackingAreas];
}

- (Vec2)localPos:(NSEvent*)ev {
    NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    return Vec2((float)p.x, (float)p.y);   // isFlipped => already top-left
}

- (void)forwardMods:(NSEvent*)ev shift:(bool*)s ctrl:(bool*)c alt:(bool*)a super:(bool*)sp {
    NSEventModifierFlags f = ev.modifierFlags;
    *s = (f & NSEventModifierFlagShift) != 0;
    *c = (f & NSEventModifierFlagControl) != 0;
    *a = (f & NSEventModifierFlagOption) != 0;
    *sp = (f & NSEventModifierFlagCommand) != 0;
}

- (void)mouse:(NSEvent*)ev button:(int)btn pressed:(bool)down {
    if (!self.nw || !self.nw->owner) return;
    Window& win = *self.nw->owner;
    auto& ctx = win.context();
    Vec2 p = [self localPos:ev];
    auto* prev = internal::currentWindowCtx;
    internal::currentWindowCtx = &ctx;
    ctx.mouseX = p.x; ctx.mouseY = p.y;
    ctx.mousePressed = down;
    ctx.mouseButton = down ? btn : -1;
    MouseEventArgs e;
    e.x = p.x; e.y = p.y; e.pos = p; e.globalPos = p; e.button = btn;
    [self forwardMods:ev shift:&e.shift ctrl:&e.ctrl alt:&e.alt super:&e.super];
    if (down) {
        win.events().mousePressed.notify(e);
        if (win.app_) win.app_->mousePressed(e);
        win.dispatchMousePressToTree(e);
    } else {
        win.events().mouseReleased.notify(e);
        if (win.app_) win.app_->mouseReleased(e);
        win.dispatchMouseReleaseToTree(e);
    }
    internal::currentWindowCtx = prev;
}

- (void)mouseDown:(NSEvent*)ev  { [self mouse:ev button:0 pressed:true]; }
- (void)mouseUp:(NSEvent*)ev    { [self mouse:ev button:0 pressed:false]; }
- (void)rightMouseDown:(NSEvent*)ev { [self mouse:ev button:1 pressed:true]; }
- (void)rightMouseUp:(NSEvent*)ev   { [self mouse:ev button:1 pressed:false]; }

- (void)moveWith:(NSEvent*)ev {
    if (!self.nw || !self.nw->owner) return;
    Window& win = *self.nw->owner;
    auto& ctx = win.context();
    Vec2 p = [self localPos:ev];
    ctx.pmouseX = ctx.mouseX; ctx.pmouseY = ctx.mouseY;
    ctx.mouseX = p.x; ctx.mouseY = p.y;
    MouseMoveEventArgs e;
    e.x = p.x; e.y = p.y;
    e.deltaX = (float)ev.deltaX; e.deltaY = (float)ev.deltaY;
    [self forwardMods:ev shift:&e.shift ctrl:&e.ctrl alt:&e.alt super:&e.super];
    win.events().mouseMoved.notify(e);
    if (self.nw->owner->app_) self.nw->owner->app_->mouseMoved(e);
}
- (void)mouseMoved:(NSEvent*)ev   { [self moveWith:ev]; }
- (void)mouseDragged:(NSEvent*)ev { [self moveWith:ev]; }
- (void)mouseEntered:(NSEvent*)ev { [self moveWith:ev]; }
- (void)mouseExited:(NSEvent*)ev {
    // Park the cursor offscreen so the next tick's updateHoverState clears
    // this window's hover (hoveredNode lives in ITS WindowContext).
    if (!self.nw || !self.nw->owner) return;
    auto& ctx = self.nw->owner->context();
    ctx.pmouseX = ctx.mouseX; ctx.pmouseY = ctx.mouseY;
    ctx.mouseX = -1.0f; ctx.mouseY = -1.0f;
}

- (void)keyEvt:(NSEvent*)ev pressed:(bool)down {
    if (!self.nw || !self.nw->owner) return;
    Window& win = *self.nw->owner;
    auto& ctx = win.context();
    // NSEvent keyCode is a hardware code; map letters/digits via characters
    // for the common case (full keycode table is Phase 2 polish).
    int key = 0;
    NSString* chars = ev.charactersIgnoringModifiers;
    if (chars.length > 0) key = toupper([chars characterAtIndex:0]);
    if (down) ctx.keysPressed.insert(key); else ctx.keysPressed.erase(key);
    KeyEventArgs e;
    e.key = key; e.isRepeat = ev.isARepeat;
    [self forwardMods:ev shift:&e.shift ctrl:&e.ctrl alt:&e.alt super:&e.super];
    if (down) {
        win.events().keyPressed.notify(e);
        if (win.app_) win.app_->keyPressed(e);
    } else {
        win.events().keyReleased.notify(e);
        if (win.app_) win.app_->keyReleased(e);
    }
}
- (void)keyDown:(NSEvent*)ev { [self keyEvt:ev pressed:true]; }
- (void)keyUp:(NSEvent*)ev   { [self keyEvt:ev pressed:false]; }

// -- per-frame tick (CADisplayLink target) --
- (void)tick:(CADisplayLink*)link {
    NativeWindow* nw = self.nw;
    if (!nw || !nw->owner) return;
    Window& win = *nw->owner;
    auto& ctx = win.context();

    // Occluded => not due. This is what prevents the ~1s nextDrawable stall.
    if ((nw->window.occlusionState & NSWindowOcclusionStateVisible) == 0) {
        if (!nw->loggedOccluded) {
            nw->loggedOccluded = true;
            logNotice("Window") << "second window occluded - skipping frames (no stall)";
        }
        return;
    }
    nw->loggedOccluded = false;

    // Keep layer size in sync with the view
    CGFloat scale = nw->window.backingScaleFactor;
    NSSize sz = self.bounds.size;
    int fbw = (int)(sz.width * scale), fbh = (int)(sz.height * scale);
    if (fbw <= 0 || fbh <= 0) return;
    if ((int)nw->layer.drawableSize.width != fbw || (int)nw->layer.drawableSize.height != fbh) {
        nw->layer.contentsScale = scale;
        nw->layer.drawableSize = CGSizeMake(fbw, fbh);
    }
    ctx.fbWidth = fbw; ctx.fbHeight = fbh; ctx.dpiScale = (float)scale;
    // Keep a RectNode root in sync with the window's logical size (same
    // contract as the main App on SAPP_EVENTTYPE_RESIZED).
    win.syncRootSize((float)sz.width, (float)sz.height);
    if (!nw->loggedGeometry) {
        nw->loggedGeometry = true;
        logNotice("Window") << "geometry: bounds=" << sz.width << "x" << sz.height
            << " backingScale=" << (float)nw->window.backingScaleFactor
            << " contentsScale=" << (float)nw->layer.contentsScale
            << " drawable=" << fbw << "x" << fbh
            << " screen=" << nw->window.screen.frame.size.width << "x" << nw->window.screen.frame.size.height;
    }

    nw->frameDrawable = [nw->layer nextDrawable];
    if (nw->frameDrawable == nil) return;
    ensureAuxTextures(nw, fbw, fbh);

    // Per-window delta time: wall-clock between THIS window's processed ticks,
    // so getDeltaTime() inside this window's update/draw is correct even when
    // its display runs at a different refresh rate than the main window's.
    // Same semantics as the main loop: a long gap (occlusion) yields one large
    // delta, exactly like an event-driven main window.
    {
        auto callNow = std::chrono::high_resolution_clock::now();
        if (!ctx.lastUpdateCallTimeInitialized) {
            ctx.lastUpdateCallTimeInitialized = true;
            // First tick: estimate from the display link's frame interval.
            ctx.updateDeltaTime = link.targetTimestamp - link.timestamp;
        } else {
            ctx.updateDeltaTime = std::chrono::duration<double>(callNow - ctx.lastUpdateCallTime).count();
        }
        ctx.lastUpdateCallTime = callNow;
    }

    // --- render this window's tree with its context active ---
    auto* prev = internal::currentWindowCtx;
    internal::currentWindowCtx = &ctx;

    // Own sokol_gl context, created lazily on the first tick (sgl is set up
    // by then). Same lifecycle caveats as Fbo contexts on sgl resize.
    if (ctx.swapchainTarget.context.id == SG_INVALID_ID) {
        sgl_context_desc_t cdesc = {};
        cdesc.max_vertices = 65536;
        cdesc.max_commands = 16384;
        cdesc.color_format = SG_PIXELFORMAT_BGRA8;
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

    present();
    win.events().afterFrame.notify();

    sgl_set_context(sgl_default_context());
    internal::currentWindowCtx = prev;
    nw->frameDrawable = nil;
}

@end

// ---------------------------------------------------------------------------
// Window delegate: user closes the window
// ---------------------------------------------------------------------------
@interface TCWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) NativeWindow* nw;
@end

@implementation TCWindowDelegate
- (void)windowWillClose:(NSNotification*)n {
    if (self.nw && self.nw->owner) {
        self.nw->owner->close();   // tears down link + native state
    }
}
@end

// ---------------------------------------------------------------------------
// Window methods (macOS implementations)
// ---------------------------------------------------------------------------
namespace trussc {

Window::~Window() { close(); }

void Window::close() {
    auto* nw = static_cast<NativeWindow*>(native_);
    if (!nw) return;
    native_ = nullptr;             // re-entrancy guard (windowWillClose)
    ctx_.acquireSwapchain = nullptr;
    ctx_.acquireSwapchainUser = nullptr;
    if (nw->link) { [nw->link invalidate]; nw->link = nil; }
    nw->view.nw = nullptr;
    nw->delegate.nw = nullptr;
    if (nw->window) {
        nw->window.delegate = nil;
        [nw->window orderOut:nil];
        nw->window = nil;
    }
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
    if (nw && nw->window) nw->window.title = [NSString stringWithUTF8String:title.c_str()];
}

int Window::getWidth() const {
    auto* nw = static_cast<NativeWindow*>(native_);
    if (!nw || !nw->view) return 0;
    return (int)nw->view.bounds.size.width;
}

int Window::getHeight() const {
    auto* nw = static_cast<NativeWindow*>(native_);
    if (!nw || !nw->view) return 0;
    return (int)nw->view.bounds.size.height;
}

std::shared_ptr<Window> createWindow(const WindowSettings& settings) {
    if (headless::isActive()) {
        logError("Window") << "createWindow(): not available in headless mode";
        return nullptr;
    }
    auto win = std::shared_ptr<Window>(new Window());
    auto* nw = new NativeWindow();
    nw->owner = win.get();
    nw->sampleCount = settings.sampleCount;

    NSRect rect = NSMakeRect(120, 120, settings.width, settings.height);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                              NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    if (!settings.decorated) style = NSWindowStyleMaskBorderless;
    nw->window = [[NSWindow alloc] initWithContentRect:rect styleMask:style
                                   backing:NSBackingStoreBuffered defer:NO];
    nw->window.title = [NSString stringWithUTF8String:settings.title.c_str()];
    nw->window.releasedWhenClosed = NO;
    nw->window.acceptsMouseMovedEvents = YES;   // hover without key status

    TCWindowView* view = [[TCWindowView alloc] initWithFrame:rect];
    view.nw = nw;
    nw->view = view;
    nw->layer = [CAMetalLayer layer];
    // Share the device sokol_gfx renders with (sokol_app created it)
    nw->layer.device = (__bridge id<MTLDevice>)sglue_environment().metal.device;
    nw->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    nw->layer.opaque = YES;
    view.wantsLayer = YES;
    view.layer = nw->layer;
    nw->window.contentView = view;

    TCWindowDelegate* del = [TCWindowDelegate new];
    del.nw = nw;
    nw->delegate = del;
    nw->window.delegate = del;

    win->native_ = nw;
    win->ctx_.acquireSwapchain = &acquireSecondarySwapchain;
    win->ctx_.acquireSwapchainUser = nw;

    [nw->window makeKeyAndOrderFront:nil];

    // Per-window display link: fires on the main run loop at THIS window's
    // display rate (macOS 14+).
    nw->link = [view displayLinkWithTarget:view selector:@selector(tick:)];
    [nw->link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];

    return win;
}

} // namespace trussc

#endif // __APPLE__
