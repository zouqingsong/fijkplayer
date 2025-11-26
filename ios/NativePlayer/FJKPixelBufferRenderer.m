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
        
        NSLog(@"[FJKRenderer] Initialized: %dx%d", width, height);
    }
    return self;
}

- (void)dealloc {
    [self cleanup];
}

- (BOOL)renderFrame:(CVPixelBufferRef)sourceBuffer {
    if (!sourceBuffer || !_outputBuffer) {
        return NO;
    }
    
    [_lock lock];
    
    // Lock both buffers
    CVPixelBufferLockBaseAddress(sourceBuffer, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferLockBaseAddress(_outputBuffer, 0);
    
    // Get format info
    OSType srcFormat = CVPixelBufferGetPixelFormatType(sourceBuffer);
    OSType dstFormat = CVPixelBufferGetPixelFormatType(_outputBuffer);
    
    size_t srcWidth = CVPixelBufferGetWidth(sourceBuffer);
    size_t srcHeight = CVPixelBufferGetHeight(sourceBuffer);
    size_t dstWidth = CVPixelBufferGetWidth(_outputBuffer);
    size_t dstHeight = CVPixelBufferGetHeight(_outputBuffer);
    
    BOOL success = NO;
    
    // Fast path: same format and size - direct copy
    if (srcFormat == dstFormat && srcWidth == dstWidth && srcHeight == dstHeight) {
        success = [self copyBufferDirect:sourceBuffer to:_outputBuffer];
    } else {
        // Slower path: format conversion needed
        success = [self convertBuffer:sourceBuffer to:_outputBuffer];
    }
    
    CVPixelBufferUnlockBaseAddress(_outputBuffer, 0);
    CVPixelBufferUnlockBaseAddress(sourceBuffer, kCVPixelBufferLock_ReadOnly);
    
    if (success) {
        _frameCount++;
        if (_frameCount % 120 == 0) {
            NSLog(@"[FJKRenderer] Rendered frame #%lld", _frameCount);
        }
    }
    
    [_lock unlock];
    return success;
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

- (CVPixelBufferRef)getOutputBuffer {
    [_lock lock];
    CVPixelBufferRef buffer = _outputBuffer;
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
