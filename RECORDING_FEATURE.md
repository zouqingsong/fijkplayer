## FijkPlayer Video Recording Feature - Implementation Status

## Overview
The FijkPlayer has been extended to support video recording functionality for RTSP streams and other video content to MP4/H.264 format on both Android and iOS. However, the implementation has some critical gaps that need to be addressed for full functionality.

## ✅ Completed Components

### Dart API (Complete)
- `Future<void> startRecording(String path)` - Start recording video to the specified file path
- `Future<void> stopRecording()` - Stop the current recording
- `bool get isRecording` - Check if recording is currently in progress
- Method channel handlers for recording events (`_onRecordingStarted`, `_onRecordingStopped`, `_onRecordingError`)

### iOS Implementation (Mostly Complete)
- ✅ AVAssetWriter setup with H.264/MP4 output
- ✅ Pixel buffer capture from IJKPlayer via `display_pixelbuffer:` method
- ✅ Frame writing to video file using `AVAssetWriterInputPixelBufferAdaptor`
- ✅ Proper resource cleanup and error handling
- ✅ Method call handlers integrated

### Android Implementation (Needs Completion)
- ✅ MediaMuxer + MediaCodec framework setup
- ✅ H.264 encoding configuration
- ⚠️ **CRITICAL MISSING**: Surface connection between IJKPlayer and MediaCodec
- ⚠️ **CRITICAL MISSING**: Frame capture mechanism from IJKPlayer's surface

## 🚨 Critical Issues That Need Resolution

### Android Surface Recording Problem
The current Android implementation creates a MediaCodec with an input surface but doesn't connect it to IJKPlayer's output. This means no video frames will be captured for recording.

**Possible Solutions:**
1. **Surface-to-Surface Copy**: Use OpenGL to copy frames from IJKPlayer's surface to MediaCodec's input surface
2. **Dual Surface Setup**: Configure IJKPlayer to render to both display surface and recording surface
3. **TextureView Recording**: Use TextureView.getBitmap() to capture frames and feed them to MediaCodec
4. **Screen Recording API**: Use MediaProjection API to record the screen area containing the video

### iOS Frame Timing Issues
The iOS implementation captures frames but may have timing synchronization issues between the video playback and recording timestamps.

## 🛠️ Required Fixes for Production Ready

### Android Fix (Recommended Approach)
The most reliable approach for Android is to implement a TextureView-based recording system:

```java
// 1. Add texture capture in setupSurface()
private void setupTextureRecording() {
    if (mSurfaceTexture != null) {
        mSurfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                if (mIsRecording && mRecordingSurface != null) {
                    // Copy frame to recording surface using OpenGL
                    copyFrameToRecordingSurface();
                }
            }
        });
    }
}

// 2. Implement OpenGL frame copying
private void copyFrameToRecordingSurface() {
    // Use OpenGL ES to copy texture from SurfaceTexture to MediaCodec input surface
    // This requires EGL context setup and texture rendering
}
```

### iOS Enhancement
Add proper timing synchronization:

```objectivec
// In display_pixelbuffer method, use proper presentation time
CMTime presentationTime = CMTimeMake([_ijkMediaPlayer getCurrentPosition], 1000);
// Ensure timestamps are monotonically increasing
```

## 📋 Completion Checklist

### To Make Android Recording Work:
- [ ] Implement OpenGL texture copying from SurfaceTexture to MediaCodec surface
- [ ] Add EGL context management for texture operations
- [ ] Test with RTSP streams to ensure frame capture works
- [ ] Add proper error handling for OpenGL operations

### To Enhance iOS Recording:
- [ ] Improve timestamp synchronization
- [ ] Add frame rate limiting to match video playback
- [ ] Test recording quality and performance

### General Improvements:
- [ ] Add audio track recording support
- [ ] Implement recording progress callbacks
- [ ] Add configurable quality settings
- [ ] Performance optimization and memory management

### Usage Example

```dart
import 'package:fijkplayer/fijkplayer.dart';

class VideoRecordingExample {
  late FijkPlayer player;

  void initPlayer() {
    player = FijkPlayer();
    
    // Configure for iOS Simulator if needed (for development/testing)
    FijkOption option = FijkOption();
    option.configureForIOSSimulator(); // Only call this on iOS Simulator
    await player.applyOptions(option);
    
    // Set up the player for RTSP stream
    player.setDataSource("rtsp://your-stream-url");
    
    // Enable snapshot functionality if needed for recording
    player.setOption(FijkOption.hostCategory, "enable-snapshot", 1);
  }

  Future<void> startRecording() async {
    try {
      // Ensure player is in playable state
      if (!player.isPlayable()) {
        print("Player must be in playable state to start recording");
        return;
      }

      // Check if already recording
      if (player.isRecording) {
        print("Recording is already in progress");
        return;
      }

      // Define output path
      String outputPath = "/path/to/your/output/video.mp4";
      
      // Start recording
      await player.startRecording(outputPath);
      print("Recording started successfully");
      
    } catch (e) {
      print("Failed to start recording: $e");
    }
  }

  Future<void> stopRecording() async {
    try {
      if (!player.isRecording) {
        print("No recording in progress");
        return;
      }

      await player.stopRecording();
      print("Recording stopped successfully");
      
    } catch (e) {
      print("Failed to stop recording: $e");
    }
  }

  void dispose() {
    player.release();
  }
}
```

### Platform-Specific Implementation

#### Android Implementation
- Uses `MediaRecorder` with H.264 video encoder
- Records in MPEG-4 container format
- Configurable bitrate (default: 1Mbps) and frame rate (default: 30fps)
- Automatically handles surface recording from IJKPlayer

#### iOS Implementation
- Uses `AVAssetWriter` with H.264 video codec
- Records in MP4 container format
- Uses `AVAssetWriterInputPixelBufferAdaptor` for pixel buffer handling
- Configurable video dimensions and compression settings

#### iOS Simulator Compatibility
iOS Simulator has limitations with hardware video acceleration. If you're experiencing issues where you can hear audio but can't see video, use the simulator-friendly configuration:

```dart
import 'dart:io' show Platform;

// Configure player for iOS Simulator
if (Platform.isIOS) {
  FijkOption option = FijkOption();
  option.configureForIOSSimulator();
  await player.applyOptions(option);
}
```

The `configureForIOSSimulator()` method automatically:
- Disables hardware acceleration (videotoolbox)
- Enables software decoding
- Sets optimal pixel format for simulator
- Configures frame handling for better compatibility

**Note**: Only use simulator configuration during development. Real iOS devices should use hardware acceleration for better performance.

### Error Handling

Common error scenarios and their handling:

1. **Recording already in progress**: Check `player.isRecording` before starting
2. **Invalid output path**: Ensure the path is writable and the directory exists
3. **Player not in playable state**: Ensure the video is loaded and playing
4. **File system errors**: Handle permissions and storage space issues
5. **Recording interruption**: The recording will be cleaned up automatically

### Performance Considerations

1. **Storage Space**: Recording consumes significant storage space
2. **Processing Power**: Video encoding is CPU-intensive
3. **Battery Usage**: Extended recording will drain battery faster
4. **Network Bandwidth**: For RTSP streams, ensure stable network connection

### Limitations

1. **Format Support**: Currently limited to MP4/H.264 output format
2. **Audio Recording**: Current implementation focuses on video; audio recording may need additional configuration
3. **Real-time Performance**: Recording performance depends on device capabilities
4. **Concurrent Operations**: Recording while performing other intensive operations may affect performance

### Future Enhancements

Potential improvements that could be added:
1. Audio track recording support
2. Multiple output format support (WebM, AVI, etc.)
3. Recording quality presets (Low, Medium, High)
4. Progress callbacks during recording
5. Recording pause/resume functionality
6. Custom encoder settings configuration
