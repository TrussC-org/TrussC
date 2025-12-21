// =============================================================================
// tcVideoPlayer_mac.mm - VideoPlayer macOS implementation (AVFoundation)
// =============================================================================

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include "TrussC.h"

// =============================================================================
// Internal Objective-C class for AVFoundation handling
// =============================================================================
@interface TCVideoPlayerImpl : NSObject

@property (nonatomic, strong) AVPlayer* player;
@property (nonatomic, strong) AVPlayerItem* playerItem;
@property (nonatomic, strong) AVPlayerItemVideoOutput* videoOutput;
@property (nonatomic, strong) AVAsset* asset;

@property (nonatomic, assign) NSInteger videoWidth;
@property (nonatomic, assign) NSInteger videoHeight;
@property (nonatomic, assign) float frameRate;
@property (nonatomic, assign) float duration;

@property (nonatomic, assign) BOOL isReady;
@property (nonatomic, assign) BOOL isFinished;
@property (nonatomic, assign) BOOL hasNewFrame;
@property (nonatomic, assign) BOOL loop;

// Pixel buffer for C++ side
@property (nonatomic, assign) unsigned char* pixelBuffer;
@property (nonatomic, assign) size_t pixelBufferSize;

- (BOOL)loadWithPath:(NSString*)path;
- (void)close;
- (void)update;
- (void)play;
- (void)stop;
- (void)setPaused:(BOOL)paused;

- (float)getPosition;
- (void)setPosition:(float)pct;
- (void)setVolume:(float)vol;
- (void)setSpeed:(float)speed;

- (int)getCurrentFrame;
- (int)getTotalFrames;
- (void)setFrame:(int)frame;
- (void)nextFrame;
- (void)previousFrame;

@end

@implementation TCVideoPlayerImpl

- (instancetype)init {
    self = [super init];
    if (self) {
        _player = nil;
        _playerItem = nil;
        _videoOutput = nil;
        _asset = nil;

        _videoWidth = 0;
        _videoHeight = 0;
        _frameRate = 0;
        _duration = 0;

        _isReady = NO;
        _isFinished = NO;
        _hasNewFrame = NO;
        _loop = NO;

        _pixelBuffer = nullptr;
        _pixelBufferSize = 0;
    }
    return self;
}

- (void)dealloc {
    [self close];
}

- (BOOL)loadWithPath:(NSString*)path {
    // Close previous video if any
    [self close];

    // Create file URL
    NSURL* url = [NSURL fileURLWithPath:path];
    if (!url) {
        NSLog(@"TCVideoPlayer: Failed to create URL for path: %@", path);
        return NO;
    }

    // Create asset with precise timing
    NSDictionary* options = @{
        AVURLAssetPreferPreciseDurationAndTimingKey: @(YES)
    };
    self.asset = [AVURLAsset URLAssetWithURL:url options:options];

    if (!self.asset) {
        NSLog(@"TCVideoPlayer: Failed to create asset");
        return NO;
    }

    // Load tracks synchronously
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block BOOL loadSuccess = NO;

    [self.asset loadValuesAsynchronouslyForKeys:@[@"tracks", @"duration"]
                              completionHandler:^{
        NSError* error = nil;
        AVKeyValueStatus status = [self.asset statusOfValueForKey:@"tracks" error:&error];

        if (status == AVKeyValueStatusLoaded) {
            loadSuccess = YES;
        } else {
            NSLog(@"TCVideoPlayer: Failed to load tracks: %@", error);
        }

        dispatch_semaphore_signal(semaphore);
    }];

    // Wait for loading to complete
    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

    if (!loadSuccess) {
        self.asset = nil;
        return NO;
    }

    // Get video track info
    NSArray* videoTracks = [self.asset tracksWithMediaType:AVMediaTypeVideo];
    if (videoTracks.count == 0) {
        NSLog(@"TCVideoPlayer: No video tracks found");
        self.asset = nil;
        return NO;
    }

    AVAssetTrack* videoTrack = videoTracks[0];
    CGSize naturalSize = videoTrack.naturalSize;

    // Handle transform (rotation)
    CGAffineTransform transform = videoTrack.preferredTransform;
    if (transform.b == 1.0 && transform.c == -1.0) {
        // 90 degrees rotation
        _videoWidth = (NSInteger)naturalSize.height;
        _videoHeight = (NSInteger)naturalSize.width;
    } else if (transform.b == -1.0 && transform.c == 1.0) {
        // -90 degrees rotation
        _videoWidth = (NSInteger)naturalSize.height;
        _videoHeight = (NSInteger)naturalSize.width;
    } else {
        _videoWidth = (NSInteger)naturalSize.width;
        _videoHeight = (NSInteger)naturalSize.height;
    }

    _frameRate = videoTrack.nominalFrameRate;
    _duration = (float)CMTimeGetSeconds(self.asset.duration);

    NSLog(@"TCVideoPlayer: Loaded %ldx%ld @ %.2f fps, duration: %.2f sec",
          (long)_videoWidth, (long)_videoHeight, _frameRate, _duration);

    // Create player item
    self.playerItem = [AVPlayerItem playerItemWithAsset:self.asset];
    if (!self.playerItem) {
        NSLog(@"TCVideoPlayer: Failed to create player item");
        self.asset = nil;
        return NO;
    }

    // Create video output (BGRA format for macOS)
    NSDictionary* pixelBufferAttributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
    };
    self.videoOutput = [[AVPlayerItemVideoOutput alloc]
                        initWithPixelBufferAttributes:pixelBufferAttributes];

    if (!self.videoOutput) {
        NSLog(@"TCVideoPlayer: Failed to create video output");
        self.playerItem = nil;
        self.asset = nil;
        return NO;
    }

    self.videoOutput.suppressesPlayerRendering = YES;
    [self.playerItem addOutput:self.videoOutput];

    // Create player
    self.player = [AVPlayer playerWithPlayerItem:self.playerItem];
    if (!self.player) {
        NSLog(@"TCVideoPlayer: Failed to create player");
        [self.playerItem removeOutput:self.videoOutput];
        self.videoOutput = nil;
        self.playerItem = nil;
        self.asset = nil;
        return NO;
    }

    // Observe end of playback
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(playerDidFinishPlaying:)
                                                 name:AVPlayerItemDidPlayToEndTimeNotification
                                               object:self.playerItem];

    // Allocate pixel buffer (RGBA)
    _pixelBufferSize = _videoWidth * _videoHeight * 4;
    _pixelBuffer = new unsigned char[_pixelBufferSize];
    memset(_pixelBuffer, 0, _pixelBufferSize);

    _isReady = YES;
    _isFinished = NO;

    return YES;
}

- (void)close {
    if (self.player) {
        [self.player pause];
    }

    [[NSNotificationCenter defaultCenter] removeObserver:self];

    if (self.playerItem && self.videoOutput) {
        [self.playerItem removeOutput:self.videoOutput];
    }

    self.player = nil;
    self.playerItem = nil;
    self.videoOutput = nil;
    self.asset = nil;

    if (_pixelBuffer) {
        delete[] _pixelBuffer;
        _pixelBuffer = nullptr;
        _pixelBufferSize = 0;
    }

    _videoWidth = 0;
    _videoHeight = 0;
    _frameRate = 0;
    _duration = 0;
    _isReady = NO;
    _isFinished = NO;
    _hasNewFrame = NO;
}

- (void)playerDidFinishPlaying:(NSNotification*)notification {
    _isFinished = YES;

    if (_loop) {
        // Loop: seek to start and play
        [self.player seekToTime:kCMTimeZero
                toleranceBefore:kCMTimeZero
                 toleranceAfter:kCMTimeZero];
        [self.player play];
        _isFinished = NO;
    }
}

- (void)update {
    if (!_isReady || !self.videoOutput) {
        _hasNewFrame = NO;
        return;
    }

    CMTime currentTime = [self.player currentTime];

    if ([self.videoOutput hasNewPixelBufferForItemTime:currentTime]) {
        CVPixelBufferRef pixelBuffer = [self.videoOutput copyPixelBufferForItemTime:currentTime
                                                               itemTimeForDisplay:NULL];

        if (pixelBuffer) {
            CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

            size_t width = CVPixelBufferGetWidth(pixelBuffer);
            size_t height = CVPixelBufferGetHeight(pixelBuffer);
            size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
            unsigned char* baseAddress = (unsigned char*)CVPixelBufferGetBaseAddress(pixelBuffer);

            // Convert BGRA to RGBA
            if (_pixelBuffer && width == (size_t)_videoWidth && height == (size_t)_videoHeight) {
                for (size_t y = 0; y < height; y++) {
                    unsigned char* srcRow = baseAddress + y * bytesPerRow;
                    unsigned char* dstRow = _pixelBuffer + y * width * 4;

                    for (size_t x = 0; x < width; x++) {
                        // BGRA -> RGBA
                        dstRow[x * 4 + 0] = srcRow[x * 4 + 2];  // R <- B
                        dstRow[x * 4 + 1] = srcRow[x * 4 + 1];  // G <- G
                        dstRow[x * 4 + 2] = srcRow[x * 4 + 0];  // B <- R
                        dstRow[x * 4 + 3] = srcRow[x * 4 + 3];  // A <- A
                    }
                }
                _hasNewFrame = YES;
            }

            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            CVBufferRelease(pixelBuffer);
        }
    } else {
        _hasNewFrame = NO;
    }
}

- (void)play {
    if (!_isReady) return;

    if (_isFinished) {
        // Restart from beginning
        [self.player seekToTime:kCMTimeZero
                toleranceBefore:kCMTimeZero
                 toleranceAfter:kCMTimeZero];
        _isFinished = NO;
    }

    [self.player play];
}

- (void)stop {
    if (!_isReady) return;

    [self.player pause];
    [self.player seekToTime:kCMTimeZero
            toleranceBefore:kCMTimeZero
             toleranceAfter:kCMTimeZero];
    _isFinished = NO;
}

- (void)setPaused:(BOOL)paused {
    if (!_isReady) return;

    if (paused) {
        [self.player pause];
    } else {
        [self.player play];
    }
}

- (float)getPosition {
    if (!_isReady || _duration <= 0) return 0.0f;

    CMTime currentTime = [self.player currentTime];
    float currentSec = (float)CMTimeGetSeconds(currentTime);
    return currentSec / _duration;
}

- (void)setPosition:(float)pct {
    if (!_isReady) return;

    pct = (pct < 0.0f) ? 0.0f : (pct > 1.0f) ? 1.0f : pct;
    float targetSec = pct * _duration;
    CMTime targetTime = CMTimeMakeWithSeconds(targetSec, NSEC_PER_SEC);

    [self.player seekToTime:targetTime
            toleranceBefore:kCMTimeZero
             toleranceAfter:kCMTimeZero];

    _isFinished = NO;
}

- (void)setVolume:(float)vol {
    if (!_isReady) return;
    self.player.volume = vol;
}

- (void)setSpeed:(float)speed {
    if (!_isReady) return;

    // Note: Negative speed (reverse playback) not supported in this version
    if (speed < 0.0f) {
        NSLog(@"TCVideoPlayer: Negative speed not supported");
        speed = 0.0f;
    }

    self.player.rate = speed;
}

- (int)getCurrentFrame {
    if (!_isReady || _frameRate <= 0) return 0;

    CMTime currentTime = [self.player currentTime];
    float currentSec = (float)CMTimeGetSeconds(currentTime);
    return (int)(currentSec * _frameRate);
}

- (int)getTotalFrames {
    if (!_isReady || _frameRate <= 0) return 0;
    return (int)(_duration * _frameRate);
}

- (void)setFrame:(int)frame {
    if (!_isReady || _frameRate <= 0) return;

    float pct = (float)frame / (float)[self getTotalFrames];
    [self setPosition:pct];
}

- (void)nextFrame {
    if (!_isReady) return;

    // Use step forward (requires paused state for frame-accurate stepping)
    [self.playerItem stepByCount:1];
}

- (void)previousFrame {
    if (!_isReady) return;

    // Use step backward
    [self.playerItem stepByCount:-1];
}

@end


// =============================================================================
// C++ VideoPlayer platform methods implementation
// =============================================================================

namespace trussc {

bool VideoPlayer::loadPlatform(const std::string& path) {
    // Create Objective-C implementation
    TCVideoPlayerImpl* impl = [[TCVideoPlayerImpl alloc] init];

    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];

    if (![impl loadWithPath:nsPath]) {
        return false;
    }

    // Store handle
    platformHandle_ = (__bridge_retained void*)impl;

    // Get video dimensions
    width_ = (int)impl.videoWidth;
    height_ = (int)impl.videoHeight;

    // Allocate pixel buffer
    size_t bufferSize = width_ * height_ * 4;
    pixels_ = new unsigned char[bufferSize];
    memset(pixels_, 0, bufferSize);

    return true;
}

void VideoPlayer::closePlatform() {
    if (platformHandle_) {
        TCVideoPlayerImpl* impl = (__bridge_transfer TCVideoPlayerImpl*)platformHandle_;
        [impl close];
        platformHandle_ = nullptr;
    }
}

void VideoPlayer::playPlatform() {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl play];
}

void VideoPlayer::stopPlatform() {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl stop];
}

void VideoPlayer::setPausedPlatform(bool paused) {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl setPaused:paused];
}

void VideoPlayer::updatePlatform() {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl update];

    // Copy pixels from Objective-C side
    if (impl.hasNewFrame && impl.pixelBuffer && pixels_) {
        std::lock_guard<std::mutex> lock(mutex_);
        memcpy(pixels_, impl.pixelBuffer, width_ * height_ * 4);
    }
}

bool VideoPlayer::hasNewFramePlatform() const {
    if (!platformHandle_) return false;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return impl.hasNewFrame;
}

bool VideoPlayer::isFinishedPlatform() const {
    if (!platformHandle_) return false;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return impl.isFinished;
}

float VideoPlayer::getPositionPlatform() const {
    if (!platformHandle_) return 0.0f;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return [impl getPosition];
}

void VideoPlayer::setPositionPlatform(float pct) {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl setPosition:pct];
}

float VideoPlayer::getDurationPlatform() const {
    if (!platformHandle_) return 0.0f;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return impl.duration;
}

void VideoPlayer::setVolumePlatform(float vol) {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl setVolume:vol];
}

void VideoPlayer::setSpeedPlatform(float speed) {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl setSpeed:speed];
}

void VideoPlayer::setLoopPlatform(bool loop) {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    impl.loop = loop;
}

int VideoPlayer::getCurrentFramePlatform() const {
    if (!platformHandle_) return 0;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return [impl getCurrentFrame];
}

int VideoPlayer::getTotalFramesPlatform() const {
    if (!platformHandle_) return 0;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return [impl getTotalFrames];
}

void VideoPlayer::setFramePlatform(int frame) {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl setFrame:frame];
}

void VideoPlayer::nextFramePlatform() {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl nextFrame];
}

void VideoPlayer::previousFramePlatform() {
    if (!platformHandle_) return;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    [impl previousFrame];
}

} // namespace trussc
