// MIT License - FijkPlayer Native iOS Renderer
//
// CVPixelBuffer renderer for Flutter texture integration

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * Pixel buffer renderer
 * Manages CVPixelBuffer pool and copies decoded frames
 * for Flutter texture consumption
 */
@interface FJKPixelBufferRenderer : NSObject

/// Output pixel buffer for Flutter
@property (nonatomic, readonly, nullable) CVPixelBufferRef outputBuffer;

/// Frame count
@property (nonatomic, readonly) int64_t frameCount;

/**
 * Initialize renderer with video dimensions
 */
- (instancetype)initWithWidth:(int)width height:(int)height;

/**
 * Render a decoded frame
 * Copies from VideoToolbox output into a fresh pooled buffer, publishes it as
 * the current frame and leaves every previously published frame untouched
 * @param sourceBuffer CVPixelBuffer from VideoToolbox
 * @return YES on success
 */
- (BOOL)renderFrame:(CVPixelBufferRef)sourceBuffer;

/**
 * Get the current output buffer for Flutter
 * @return the latest finished frame, which the caller owns and must release
 *         after Flutter is done with it, or NULL before the first frame
 */
- (CVPixelBufferRef _Nullable)getOutputBuffer;

/**
 * Release resources (for ARC compatibility)
 */
- (void)cleanup;

@end

NS_ASSUME_NONNULL_END
