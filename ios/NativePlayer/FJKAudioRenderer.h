// MIT License - FijkPlayer iOS Audio Renderer
// Renders PCM audio using AVAudioEngine

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface FJKAudioRenderer : NSObject

/**
 * Initialize with audio format
 * @param sampleRate Sample rate (e.g., 44100)
 * @param channels Number of channels (1=mono, 2=stereo)
 * @return Initialized renderer or nil on failure
 */
- (nullable instancetype)initWithSampleRate:(int)sampleRate channels:(int)channels;

/**
 * Start audio playback
 */
- (BOOL)start;

/**
 * Stop audio playback
 */
- (void)stop;

/**
 * Pause audio playback
 */
- (void)pause;

/**
 * Resume audio playback
 */
- (void)resume;

/**
 * Write PCM samples to audio buffer
 * @param samples PCM S16 interleaved samples
 * @param sampleCount Number of samples per channel
 * @return Number of samples written, or negative on error
 */
- (int)writeSamples:(const int16_t *)samples count:(int)sampleCount;

/**
 * Set playback volume (0.0 to 1.0)
 */
- (void)setVolume:(float)volume;

/**
 * Check if audio is currently playing
 */
- (BOOL)isPlaying;

@end

NS_ASSUME_NONNULL_END
