import 'package:fijkplayer/fijkplayer.dart';
import 'package:flutter/material.dart';

/// Example demonstrating the three playback modes
/// 
/// This shows how to configure FijkPlayer for different scenarios:
/// 1. Live video with no audio (security cameras, monitoring)
/// 2. Live video with audio synchronization (live streaming)
/// 3. Recorded video playback (VOD)
class PlaybackModeExample extends StatefulWidget {
  @override
  _PlaybackModeExampleState createState() => _PlaybackModeExampleState();
}

class _PlaybackModeExampleState extends State<PlaybackModeExample> {
  final FijkPlayer player = FijkPlayer();
  
  @override
  void initState() {
    super.initState();
  }
  
  @override
  void dispose() {
    player.release();
    super.dispose();
  }
  
  /// Example 1: Live video - no audio, minimum latency
  /// Use case: Security cameras, live monitoring, real-time surveillance
  Future<void> playLiveLowLatency(String rtspUrl) async {
    // Configure for low-latency live video
    await player.setPlaybackMode(FijkPlaybackConfig.liveLowLatency(
      bufferMs: 100,        // Minimal buffer (100ms)
      maxLatencyMs: 200,    // Drop frames if latency exceeds 200ms
    ));
    
    // Set data source and start
    await player.setDataSource(rtspUrl, autoPlay: true);
  }
  
  /// Example 2: Live video with audio synchronization
  /// Use case: Live streaming, video calls, interactive broadcasts
  Future<void> playLiveWithAudio(String rtspUrl) async {
    // Configure for live video with audio sync
    await player.setPlaybackMode(FijkPlaybackConfig.liveWithAudio(
      bufferMs: 500,        // Small buffer for low latency
      maxLatencyMs: 1000,   // Accept up to 1 second latency for sync
    ));
    
    // Set data source and start
    await player.setDataSource(rtspUrl, autoPlay: true);
  }
  
  /// Example 3: Recorded video playback
  /// Use case: Playing recorded videos, VOD content, smooth playback
  Future<void> playRecordedVideo(String videoUrl) async {
    // Configure for smooth playback
    await player.setPlaybackMode(FijkPlaybackConfig.vodOptimized(
      bufferMs: 5000,       // Large buffer (5 seconds) for smooth playback
    ));
    
    // Set data source and start
    await player.setDataSource(videoUrl, autoPlay: true);
  }
  
  /// Advanced example: Custom configuration
  Future<void> playCustomMode(String url) async {
    // Create custom playback configuration
    await player.setPlaybackMode(FijkPlaybackConfig(
      mode: FijkPlaybackMode.liveWithAudio,
      customBufferMs: 300,      // Custom buffer size
      enableAudio: true,        // Override audio setting
      maxLatencyMs: 800,        // Custom max latency
      enableFrameDrop: true,    // Override frame drop setting
    ));
    
    await player.setDataSource(url, autoPlay: true);
  }
  
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text('Playback Mode Examples')),
      body: Column(
        children: [
          // Video display
          Expanded(
            child: FijkView(
              player: player,
              color: Colors.black,
            ),
          ),
          
          // Control buttons
          Padding(
            padding: EdgeInsets.all(16),
            child: Column(
              children: [
                ElevatedButton(
                  onPressed: () => playLiveLowLatency('rtsp://example.com/live'),
                  child: Text('Live Low Latency (Security Camera)'),
                ),
                SizedBox(height: 8),
                ElevatedButton(
                  onPressed: () => playLiveWithAudio('rtsp://example.com/live'),
                  child: Text('Live With Audio (Live Streaming)'),
                ),
                SizedBox(height: 8),
                ElevatedButton(
                  onPressed: () => playRecordedVideo('https://example.com/video.mp4'),
                  child: Text('Recorded Video (VOD)'),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// Comparison of the three modes:
/// 
/// | Feature              | Live Low Latency | Live With Audio | VOD Optimized |
/// |---------------------|------------------|-----------------|---------------|
/// | Buffer Size         | 100ms (1-2 frames)| 500ms (5 frames)| 5000ms (10s)  |
/// | Audio               | Disabled         | Enabled         | Enabled       |
/// | Frame Dropping      | Aggressive       | Moderate        | None          |
/// | Max Latency         | 200ms            | 1000ms          | N/A           |
/// | Use Case            | Security, Monitor| Live Stream     | Playback      |
/// | Quality Priority    | Latency          | Balanced        | Quality       |
/// 
/// Performance characteristics:
/// 
/// Live Low Latency:
/// - Latency: 50-200ms
/// - CPU: Low (no audio processing)
/// - Network: Tolerates jitter (drops frames)
/// - Quality: May skip frames, prioritizes real-time
/// 
/// Live With Audio:
/// - Latency: 200-500ms
/// - CPU: Medium (audio + video)
/// - Network: Needs stable connection
/// - Quality: Good, maintains A/V sync
/// 
/// VOD Optimized:
/// - Latency: N/A (buffered)
/// - CPU: Medium
/// - Network: Can buffer ahead
/// - Quality: Best, smooth playback
