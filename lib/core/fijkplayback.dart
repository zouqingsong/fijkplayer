part of fijkplayer;

/// Playback mode for different use cases
///
/// Choose the appropriate mode based on your application requirements:
/// - [liveLowLatency]: Real-time live video with minimum latency, no audio
/// - [liveWithAudio]: Real-time live video with synchronized audio
/// - [vodOptimized]: Video-on-demand playback with smooth buffering
enum FijkPlaybackMode {
  /// Real-time live video playback mode
  /// 
  /// Optimized for:
  /// - Minimum latency (50-200ms)
  /// - Aggressive frame dropping
  /// - No audio playback
  /// - Small buffer (1-3 frames)
  /// - Fast rendering
  /// 
  /// Use for: Security cameras, live monitoring, real-time surveillance
  liveLowLatency,

  /// Live video with audio synchronization
  /// 
  /// Optimized for:
  /// - Low latency (200-500ms)
  /// - Audio/video synchronization
  /// - Moderate frame dropping when needed
  /// - Small audio/video buffers
  /// - Balanced quality and latency
  /// 
  /// Use for: Live streaming, video calls, interactive live broadcasts
  liveWithAudio,

  /// Video-on-demand optimized playback
  /// 
  /// Optimized for:
  /// - Smooth playback
  /// - Larger buffer (5-10 seconds)
  /// - No frame dropping
  /// - Quality over latency
  /// - Seek support
  /// 
  /// Use for: Recorded videos, playback from storage, VOD content
  vodOptimized,
}

/// Configuration for playback mode
class FijkPlaybackConfig {
  /// Playback mode
  final FijkPlaybackMode mode;

  /// Custom buffer size in milliseconds (overrides mode default)
  final int? customBufferMs;

  /// Enable/disable audio (overrides mode default)
  final bool? enableAudio;

  /// Maximum acceptable latency in milliseconds
  final int? maxLatencyMs;

  /// Enable frame dropping when behind schedule
  final bool? enableFrameDrop;

  const FijkPlaybackConfig({
    required this.mode,
    this.customBufferMs,
    this.enableAudio,
    this.maxLatencyMs,
    this.enableFrameDrop,
  });

  /// Create live low-latency config
  factory FijkPlaybackConfig.liveLowLatency({
    int bufferMs = 100,
    int maxLatencyMs = 200,
  }) {
    return FijkPlaybackConfig(
      mode: FijkPlaybackMode.liveLowLatency,
      customBufferMs: bufferMs,
      enableAudio: false,
      maxLatencyMs: maxLatencyMs,
      enableFrameDrop: true,
    );
  }

  /// Create live with audio config
  factory FijkPlaybackConfig.liveWithAudio({
    int bufferMs = 500,
    int maxLatencyMs = 1000,
  }) {
    return FijkPlaybackConfig(
      mode: FijkPlaybackMode.liveWithAudio,
      customBufferMs: bufferMs,
      enableAudio: true,
      maxLatencyMs: maxLatencyMs,
      enableFrameDrop: true,
    );
  }

  /// Create VOD optimized config
  factory FijkPlaybackConfig.vodOptimized({
    int bufferMs = 5000,
  }) {
    return FijkPlaybackConfig(
      mode: FijkPlaybackMode.vodOptimized,
      customBufferMs: bufferMs,
      enableAudio: true,
      maxLatencyMs: null,
      enableFrameDrop: false,
    );
  }

  Map<String, dynamic> toMap() {
    return {
      'mode': mode.index,
      'customBufferMs': customBufferMs,
      'enableAudio': enableAudio,
      'maxLatencyMs': maxLatencyMs,
      'enableFrameDrop': enableFrameDrop,
    };
  }
}
