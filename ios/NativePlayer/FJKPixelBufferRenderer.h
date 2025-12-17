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
 * Copies from VideoToolbox output to Flutter texture buffer
 * @param sourceBuffer CVPixelBuffer from VideoToolbox
 * @return YES on success
 */
- (BOOL)renderFrame:(CVPixelBufferRef)sourceBuffer;

/**
 * Get the current output buffer for Flutter
 * Caller should NOT release this - it's managed internally
 */
- (CVPixelBufferRef _Nullable)getOutputBuffer;

/**
 * Release resources (for ARC compatibility)
 */
- (void)cleanup;

@end

NS_ASSUME_NONNULL_END
