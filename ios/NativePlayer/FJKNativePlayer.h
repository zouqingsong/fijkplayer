// MIT License - FijkPlayer Native iOS Implementation
//
// Native player for iOS matching Android architecture:
// - FFmpeg demuxer (reuse existing)
// - VideoToolbox H.264 decoder
// - Metal/OpenGL renderer
// - Objective-C bridge

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

NS_ASSUME_NONNULL_BEGIN

// Player states
typedef NS_ENUM(NSInteger, FJKPlayerState) {
    FJKPlayerStateIdle = 0,
    FJKPlayerStateInitialized = 1,
    FJKPlayerStatePreparing = 2,
    FJKPlayerStatePrepared = 3,
    FJKPlayerStatePlaying = 4,
    FJKPlayerStatePaused = 5,
    FJKPlayerStateStopped = 6,
    FJKPlayerStateError = 7
};

// Player events
typedef NS_ENUM(NSInteger, FJKPlayerEvent) {
    FJKPlayerEventPrepared = 0,
    FJKPlayerEventStarted = 1,
    FJKPlayerEventPaused = 2,
    FJKPlayerEventCompleted = 3,
    FJKPlayerEventError = 4,
    FJKPlayerEventBuffering = 5,
    FJKPlayerEventVideoSizeChanged = 6,
    FJKPlayerEventSeekComplete = 7
};

// Event callback block
typedef void (^FJKPlayerEventCallback)(FJKPlayerEvent event, NSInteger arg1, NSInteger arg2);

// Frame callback block (called when new frame is rendered)
typedef void (^FJKPlayerFrameCallback)(void);

/**
 * Native iOS Player
 * 
 * Architecture mirrors Android implementation:
 * - FFmpeg for demuxing (RTSP/HTTP/File)
 * - VideoToolbox for H.264/H.265 decoding
 * - CVPixelBuffer for frame output
 * - Metal/OpenGL for rendering
 */
@interface FJKNativePlayer : NSObject

/// Current player state
@property (nonatomic, readonly) FJKPlayerState state;

/// Video dimensions
@property (nonatomic, readonly) CGSize videoSize;

/// Video duration in milliseconds
@property (nonatomic, readonly) int64_t duration;

/// Current playback position in milliseconds
@property (nonatomic, readonly) int64_t currentPosition;

/// Whether player is currently playing
@property (nonatomic, readonly) BOOL isPlaying;

/// Event callback
@property (nonatomic, copy, nullable) FJKPlayerEventCallback eventCallback;

/// Frame callback (called when new frame is available)
@property (nonatomic, copy, nullable) FJKPlayerFrameCallback frameCallback;

/**
 * Initialize player
 */
- (instancetype)init;

/**
 * Set data source URL
 * @param url Media URL (file://, http://, rtsp://)
 * @return 0 on success, negative on error
 */
- (int)setDataSource:(NSString *)url;

/**
 * Set playback mode for different scenarios
 * Configure the player for optimal performance based on use case.
 * Should be called after setDataSource and before prepareAsync.
 *
 * @param mode Playback mode (0=LIVE_LOW_LATENCY, 1=LIVE_WITH_AUDIO, 2=VOD_OPTIMIZED)
 * @param bufferMs Custom buffer size in milliseconds (-1 for mode default)
 * @param enableAudio Enable audio decoding/playback (-1 for mode default)
 * @param maxLatencyMs Maximum acceptable latency in ms (-1 for mode default)
 * @param enableFrameDrop Enable frame dropping when behind (-1 for mode default)
 * @return 0 on success, negative on error
 */
- (int)setPlaybackMode:(NSInteger)mode
              bufferMs:(NSInteger)bufferMs
           enableAudio:(NSInteger)enableAudio
         maxLatencyMs:(NSInteger)maxLatencyMs
       enableFrameDrop:(NSInteger)enableFrameDrop;

/**
 * Set output pixel buffer for rendering
 * Used for Flutter texture integration
 * @param pixelBuffer CVPixelBuffer to render into
 */
- (void)setPixelBuffer:(CVPixelBufferRef)pixelBuffer;

/**
 * Prepare player asynchronously
 * Calls eventCallback with FJKPlayerEventPrepared when done
 */
- (void)prepareAsync;

/**
 * Start playback
 * @return 0 on success, negative on error
 */
- (int)start;

/**
 * Pause playback
 */
- (void)pause;

/**
 * Resume playback
 */
- (void)resume;

/**
 * Stop playback
 */
- (void)stop;

/**
 * Seek to position
 * @param positionMs Position in milliseconds
 */
- (void)seekTo:(int64_t)positionMs;

/**
 * Set playback volume
 * @param volume Volume level from 0.0 (mute) to 1.0 (max)
 */
- (void)setVolume:(float)volume;

/**
 * Cleanup and release resources
 */
- (void)cleanup;

/**
 * Get the current video frame as CVPixelBuffer
 * For Flutter texture integration
 * @return Current frame buffer (do NOT release - internally managed)
 */
- (CVPixelBufferRef _Nullable)copyPixelBuffer;

@end

NS_ASSUME_NONNULL_END
