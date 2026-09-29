// MIT License - FijkPlayer Native iOS Renderer

#import "FJKPixelBufferRenderer.h"
#import <Accelerate/Accelerate.h>

@interface FJKPixelBufferRenderer () {
    CVPixelBufferRef _outputBuffer;
    CVPixelBufferPoolRef _bufferPool;
    int _width;
    int _height;
    int64_t _frameCount;
    NSLock *_lock;
}
@end

@implementation FJKPixelBufferRenderer

- (instancetype)initWithWidth:(int)width height:(int)height {
    self = [super init];
    if (self) {
        _width = width;
        _height = height;
        _frameCount = 0;
        _lock = [[NSLock alloc] init];
        
        // Create pixel buffer pool for efficient buffer management
        NSDictionary *poolAttributes = @{
            (id)kCVPixelBufferPoolMinimumBufferCountKey: @3
        };
        
        NSDictionary *bufferAttributes = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
            (id)kCVPixelBufferWidthKey: @(width),
            (id)kCVPixelBufferHeightKey: @(height),
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{},
            (id)kCVPixelBufferMetalCompatibilityKey: @YES
        };
        
        CVReturn status = CVPixelBufferPoolCreate(
            kCFAllocatorDefault,
            (__bridge CFDictionaryRef)poolAttributes,
            (__bridge CFDictionaryRef)bufferAttributes,
            &_bufferPool
        );
        
        if (status != kCVReturnSuccess) {
            NSLog(@"[FJKRenderer] Failed to create pixel buffer pool: %d", status);
            return nil;
        }
        
        // Pre-create output buffer
        status = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, _bufferPool, &_outputBuffer);
        if (status != kCVReturnSuccess) {
            NSLog(@"[FJKRenderer] Failed to create output buffer: %d", status);
            return nil;
        }
        
        // Initialize to black in NV12 VideoRange (Y=0x10, UV=0x80) to avoid
        // the green flash caused by an uninitialized chroma plane (UV=0 → green).
        if (CVPixelBufferLockBaseAddress(_outputBuffer, 0) == kCVReturnSuccess) {
            size_t planeCount = CVPixelBufferGetPlaneCount(_outputBuffer);
            if (planeCount >= 2) {
                // Y plane: 0x10 (video-range black)
                void *yPlane = CVPixelBufferGetBaseAddressOfPlane(_outputBuffer, 0);
                size_t ySize = CVPixelBufferGetBytesPerRowOfPlane(_outputBuffer, 0)
                               * CVPixelBufferGetHeightOfPlane(_outputBuffer, 0);
                memset(yPlane, 0x10, ySize);
                // UV plane: 0x80 (neutral chroma)
                void *uvPlane = CVPixelBufferGetBaseAddressOfPlane(_outputBuffer, 1);
                size_t uvSize = CVPixelBufferGetBytesPerRowOfPlane(_outputBuffer, 1)
                                * CVPixelBufferGetHeightOfPlane(_outputBuffer, 1);
                memset(uvPlane, 0x80, uvSize);
            }
            CVPixelBufferUnlockBaseAddress(_outputBuffer, 0);
        }
        
        NSLog(@"[FJKRenderer] Initialized: %dx%d", width, height);
    }
    return self;
}

- (void)dealloc {
    [self cleanup];
}

- (BOOL)renderFrame:(CVPixelBufferRef)sourceBuffer {
    if (!sourceBuffer) {
        return NO;
    }
    
    // Hold a reference to the pool across the copy: a frame can still be in
    // flight from the decoder thread when -cleanup runs, and using a released
    // pool would be a use-after-free.
    [_lock lock];
    CVPixelBufferPoolRef pool = _bufferPool;
    if (pool) {
        CVPixelBufferPoolRetain(pool);
    }
    [_lock unlock];
    if (!pool) {
        return NO;
    }
    
    // Copy into a *fresh* pool buffer instead of into the frame already handed
    // out to Flutter. Flutter's texture contract requires the buffer returned
    // by -copyPixelBuffer to stay byte-for-byte identical until the engine
    // releases it; overwriting one shared buffer lets the raster thread sample
    // a half-written frame (tearing) or a different frame than the one it was
    // asked for, which is what makes a smoothly moving picture look like it
    // jumps back and forth on every frame.
    CVPixelBufferRef destBuffer = NULL;
    if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool,
                                           &destBuffer) != kCVReturnSuccess ||
        destBuffer == NULL) {
        CVPixelBufferPoolRelease(pool);
        return NO;
    }
    CVPixelBufferPoolRelease(pool);
    
    // Lock both buffers: source read-only, destination writable.
    CVPixelBufferLockBaseAddress(sourceBuffer, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferLockBaseAddress(destBuffer, 0);
    
    // Get format info
    OSType srcFormat = CVPixelBufferGetPixelFormatType(sourceBuffer);
    OSType dstFormat = CVPixelBufferGetPixelFormatType(destBuffer);
    
    size_t srcWidth = CVPixelBufferGetWidth(sourceBuffer);
    size_t srcHeight = CVPixelBufferGetHeight(sourceBuffer);
    size_t dstWidth = CVPixelBufferGetWidth(destBuffer);
    size_t dstHeight = CVPixelBufferGetHeight(destBuffer);
    
    BOOL success = NO;
    
    // Fast path: same format and size - direct copy
    if (srcFormat == dstFormat && srcWidth == dstWidth && srcHeight == dstHeight) {
        success = [self copyBufferDirect:sourceBuffer to:destBuffer];
    } else {
        // Slower path: format conversion needed
        success = [self convertBuffer:sourceBuffer to:destBuffer];
    }
    
    CVPixelBufferUnlockBaseAddress(destBuffer, 0);
    CVPixelBufferUnlockBaseAddress(sourceBuffer, kCVPixelBufferLock_ReadOnly);
    
    if (!success) {
        CVPixelBufferRelease(destBuffer);
        return NO;
    }
    
    // Publish the finished frame. Published frames are never written to again,
    // so a frame the engine still holds stays intact, and the pool grows or
    // recycles buffers as the compositor gives them back.
    [_lock lock];
    CVPixelBufferRef previous = _outputBuffer;
    _outputBuffer = destBuffer;
    _frameCount++;
    [_lock unlock];
    
    if (previous) {
        CVPixelBufferRelease(previous);
    }
    return YES;
}

- (BOOL)copyBufferDirect:(CVPixelBufferRef)src to:(CVPixelBufferRef)dst {
    size_t planeCount = CVPixelBufferGetPlaneCount(src);
    
    if (planeCount == 0) {
        // Interleaved format (rare for video)
        void *srcData = CVPixelBufferGetBaseAddress(src);
        void *dstData = CVPixelBufferGetBaseAddress(dst);
        size_t srcSize = CVPixelBufferGetDataSize(src);
        size_t dstSize = CVPixelBufferGetDataSize(dst);
        
        if (srcSize <= dstSize) {
            memcpy(dstData, srcData, srcSize);
            return YES;
        }
    } else {
        // Planar format (YUV420 - most common)
        for (size_t i = 0; i < planeCount; i++) {
            void *srcPlane = CVPixelBufferGetBaseAddressOfPlane(src, i);
            void *dstPlane = CVPixelBufferGetBaseAddressOfPlane(dst, i);
            size_t srcBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(src, i);
            size_t dstBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(dst, i);
            size_t height = CVPixelBufferGetHeightOfPlane(src, i);
            
            if (srcBytesPerRow == dstBytesPerRow) {
                // Fast copy
                memcpy(dstPlane, srcPlane, srcBytesPerRow * height);
            } else {
                // Row-by-row copy (different stride)
                size_t copyWidth = MIN(srcBytesPerRow, dstBytesPerRow);
                for (size_t row = 0; row < height; row++) {
                    memcpy(dstPlane + row * dstBytesPerRow,
                           srcPlane + row * srcBytesPerRow,
                           copyWidth);
                }
            }
        }
        return YES;
    }
    
    return NO;
}

- (BOOL)convertBuffer:(CVPixelBufferRef)src to:(CVPixelBufferRef)dst {
    // Use vImage for format conversion if needed
    // For now, log and return NO (will need vImageConvert if formats differ)
    NSLog(@"[FJKRenderer] Format conversion not yet implemented");
    return NO;
}

/// Latest finished frame, with a +1 retain count the caller owns, or NULL when
/// no frame has been rendered yet. Retaining under the lock keeps the frame
/// alive even if -renderFrame: publishes a newer one immediately afterwards.
- (CVPixelBufferRef)getOutputBuffer {
    [_lock lock];
    CVPixelBufferRef buffer = _outputBuffer;
    if (buffer) {
        CVPixelBufferRetain(buffer);
    }
    [_lock unlock];
    return buffer;
}

- (CVPixelBufferRef)outputBuffer {
    return [self getOutputBuffer];
}

- (int64_t)frameCount {
    [_lock lock];
    int64_t count = _frameCount;
    [_lock unlock];
    return count;
}

- (void)cleanup {
    [_lock lock];
    
    if (_outputBuffer) {
        CVPixelBufferRelease(_outputBuffer);
        _outputBuffer = NULL;
    }
    
    if (_bufferPool) {
        CVPixelBufferPoolRelease(_bufferPool);
        _bufferPool = NULL;
    }
    
    [_lock unlock];
    NSLog(@"[FJKRenderer] Released");
}

@end
