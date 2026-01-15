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

// Audio track info
@property (nonatomic, assign) BOOL hasAudioTrack;
@property (nonatomic, assign) uint32_t audioCodecFourCC;
@property (nonatomic, assign) int audioSampleRate;
@property (nonatomic, assign) int audioChannels;

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

// Audio extraction
- (NSData*)extractAudioData;

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

        _hasAudioTrack = NO;
        _audioCodecFourCC = 0;
        _audioSampleRate = 0;
        _audioChannels = 0;
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

    // Get audio track info
    NSArray* audioTracks = [self.asset tracksWithMediaType:AVMediaTypeAudio];
    if (audioTracks.count > 0) {
        AVAssetTrack* audioTrack = audioTracks[0];
        _hasAudioTrack = YES;

        // Get audio format descriptions
        NSArray* formatDescriptions = audioTrack.formatDescriptions;
        if (formatDescriptions.count > 0) {
            CMAudioFormatDescriptionRef desc = (__bridge CMAudioFormatDescriptionRef)formatDescriptions[0];
            const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription(desc);
            if (asbd) {
                _audioSampleRate = (int)asbd->mSampleRate;
                _audioChannels = (int)asbd->mChannelsPerFrame;
            }

            // Get codec FourCC
            FourCharCode mediaSubType = CMFormatDescriptionGetMediaSubType(desc);
            _audioCodecFourCC = mediaSubType;

            NSLog(@"TCVideoPlayer: Audio track found - codec: %c%c%c%c, %d Hz, %d ch",
                  (char)(mediaSubType >> 24), (char)(mediaSubType >> 16),
                  (char)(mediaSubType >> 8), (char)mediaSubType,
                  _audioSampleRate, _audioChannels);
        }
    }

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

// Helper: Get sample rate index for ADTS header
static int getADTSSampleRateIndex(int sampleRate) {
    static const int rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
    for (int i = 0; i < 13; i++) {
        if (rates[i] == sampleRate) return i;
    }
    return 4; // Default to 44100
}

// Helper: Create ADTS header (7 bytes)
static void createADTSHeader(uint8_t* header, int frameLength, int sampleRate, int channels, int profile) {
    int sampleRateIndex = getADTSSampleRateIndex(sampleRate);
    int channelConfig = channels;
    int fullLength = frameLength + 7; // ADTS header is 7 bytes

    // ADTS header structure (7 bytes = 56 bits)
    // Syncword: 12 bits = 0xFFF
    // ID: 1 bit = 0 (MPEG-4)
    // Layer: 2 bits = 0
    // Protection absent: 1 bit = 1 (no CRC)
    // Profile: 2 bits (0=Main, 1=LC, 2=SSR, 3=LTP) - we use profile-1 for ADTS
    // Sample rate index: 4 bits
    // Private: 1 bit = 0
    // Channel config: 3 bits
    // Original: 1 bit = 0
    // Home: 1 bit = 0
    // Copyright ID: 1 bit = 0
    // Copyright start: 1 bit = 0
    // Frame length: 13 bits (includes header)
    // Buffer fullness: 11 bits = 0x7FF (VBR)
    // Number of AAC frames - 1: 2 bits = 0

    int adtsProfile = (profile > 0) ? (profile - 1) : 1; // AAC-LC = 1, ADTS uses profile-1

    header[0] = 0xFF; // Syncword high
    header[1] = 0xF1; // Syncword low (4) + ID (0) + Layer (00) + Protection absent (1)
    header[2] = ((adtsProfile & 0x03) << 6) | ((sampleRateIndex & 0x0F) << 2) | ((channelConfig >> 2) & 0x01);
    header[3] = ((channelConfig & 0x03) << 6) | ((fullLength >> 11) & 0x03);
    header[4] = (fullLength >> 3) & 0xFF;
    header[5] = ((fullLength & 0x07) << 5) | 0x1F; // Buffer fullness high (0x7FF)
    header[6] = 0xFC; // Buffer fullness low + frames - 1
}

- (NSData*)extractAudioData {
    if (!_hasAudioTrack || !self.asset) {
        return nil;
    }

    NSArray* audioTracks = [self.asset tracksWithMediaType:AVMediaTypeAudio];
    if (audioTracks.count == 0) {
        return nil;
    }

    AVAssetTrack* audioTrack = audioTracks[0];

    // Check if this is AAC - we need to add ADTS headers for AAC
    BOOL isAAC = (_audioCodecFourCC == kAudioFormatMPEG4AAC ||
                  _audioCodecFourCC == kAudioFormatMPEG4AAC_HE ||
                  _audioCodecFourCC == kAudioFormatMPEG4AAC_HE_V2 ||
                  _audioCodecFourCC == kAudioFormatMPEG4AAC_LD ||
                  _audioCodecFourCC == kAudioFormatMPEG4AAC_ELD);

    // Create asset reader
    NSError* error = nil;
    AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:self.asset error:&error];
    if (!reader) {
        NSLog(@"TCVideoPlayer: Failed to create asset reader: %@", error);
        return nil;
    }

    // Create track output (passthrough - no decoding)
    AVAssetReaderTrackOutput* trackOutput = [[AVAssetReaderTrackOutput alloc]
                                              initWithTrack:audioTrack
                                              outputSettings:nil];  // nil = passthrough
    trackOutput.alwaysCopiesSampleData = NO;

    if (![reader canAddOutput:trackOutput]) {
        NSLog(@"TCVideoPlayer: Cannot add audio track output");
        return nil;
    }
    [reader addOutput:trackOutput];

    // Start reading
    if (![reader startReading]) {
        NSLog(@"TCVideoPlayer: Failed to start reading audio: %@", reader.error);
        return nil;
    }

    // Read all samples
    NSMutableData* audioData = [[NSMutableData alloc] init];
    CMSampleBufferRef sampleBuffer;

    // Get AAC profile from format description (for ADTS header)
    int aacProfile = 2; // Default to AAC-LC

    while ((sampleBuffer = [trackOutput copyNextSampleBuffer])) {
        CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
        if (blockBuffer) {
            if (isAAC) {
                // For AAC: add ADTS header to each packet
                // Get packet sizes
                size_t totalLength = 0;
                char* rawData = nullptr;
                OSStatus status = CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &totalLength, &rawData);

                if (status == noErr && rawData) {
                    // Get number of samples (packets) in this buffer
                    CMItemCount sampleCount = CMSampleBufferGetNumSamples(sampleBuffer);

                    if (sampleCount > 1) {
                        // Multiple packets - get size of each
                        size_t offset = 0;
                        for (CMItemIndex i = 0; i < sampleCount; i++) {
                            size_t packetSize = CMSampleBufferGetSampleSize(sampleBuffer, i);

                            // Create and write ADTS header
                            uint8_t adtsHeader[7];
                            createADTSHeader(adtsHeader, (int)packetSize, _audioSampleRate, _audioChannels, aacProfile);
                            [audioData appendBytes:adtsHeader length:7];

                            // Write AAC frame data
                            [audioData appendBytes:(rawData + offset) length:packetSize];
                            offset += packetSize;
                        }
                    } else {
                        // Single packet - treat entire buffer as one packet
                        uint8_t adtsHeader[7];
                        createADTSHeader(adtsHeader, (int)totalLength, _audioSampleRate, _audioChannels, aacProfile);
                        [audioData appendBytes:adtsHeader length:7];
                        [audioData appendBytes:rawData length:totalLength];
                    }
                }
            } else {
                // For non-AAC (MP3, etc.): direct copy
                size_t length = CMBlockBufferGetDataLength(blockBuffer);
                char* data = (char*)malloc(length);
                if (data) {
                    CMBlockBufferCopyDataBytes(blockBuffer, 0, length, data);
                    [audioData appendBytes:data length:length];
                    free(data);
                }
            }
        }
        CFRelease(sampleBuffer);
    }

    if (reader.status == AVAssetReaderStatusFailed) {
        NSLog(@"TCVideoPlayer: Audio extraction failed: %@", reader.error);
        return nil;
    }

    NSLog(@"TCVideoPlayer: Extracted %lu bytes of audio data (ADTS: %s)",
          (unsigned long)audioData.length, isAAC ? "yes" : "no");
    return audioData;
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

// Audio access platform methods
bool VideoPlayer::hasAudioPlatform() const {
    if (!platformHandle_) return false;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return impl.hasAudioTrack;
}

uint32_t VideoPlayer::getAudioCodecPlatform() const {
    if (!platformHandle_) return 0;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return impl.audioCodecFourCC;
}

int VideoPlayer::getAudioSampleRatePlatform() const {
    if (!platformHandle_) return 0;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return impl.audioSampleRate;
}

int VideoPlayer::getAudioChannelsPlatform() const {
    if (!platformHandle_) return 0;
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;
    return impl.audioChannels;
}

std::vector<uint8_t> VideoPlayer::getAudioDataPlatform() const {
    if (!platformHandle_) return {};
    TCVideoPlayerImpl* impl = (__bridge TCVideoPlayerImpl*)platformHandle_;

    NSData* audioData = [impl extractAudioData];
    if (!audioData) return {};

    std::vector<uint8_t> result(audioData.length);
    memcpy(result.data(), audioData.bytes, audioData.length);
    return result;
}

} // namespace trussc
