import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// Test widget for NativePlayer - Direct validation
/// Bypasses fijkplayer plugin for quick testing
class NativePlayerTestPage extends StatefulWidget {
  @override
  _NativePlayerTestPageState createState() => _NativePlayerTestPageState();
}

class _NativePlayerTestPageState extends State<NativePlayerTestPage> {
  static const MethodChannel _channel = MethodChannel('befovy.com/fijk/native_player');
  
  int? _textureId;
  String _status = 'Not initialized';
  int _videoWidth = 0;
  int _videoHeight = 0;
  int _duration = 0;
  int _position = 0;
  bool _isPlaying = false;
  
  Timer? _positionTimer; // Use timer instead of recursive calls
  Timer? _refreshTimer; // Separate timer for UI refreshes
  
  // Adaptive timing variables
  bool _isLiveStream = false;
  double _videoFrameRate = 30.0; // Default frame rate
  int _refreshInterval = 33; // Default 30fps (1000ms/30 ≈ 33ms)
  
  // Test with network source (HTTPS MP4) - File stream with OpenSSL support
  final String _testUrl = 'https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4';
  
  // Alternative test URLs:
  
  // 📁 VIDEO FILES (will use actual fps, lower UI refresh):
  // 'http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4'
  // 'http://techslides.com/demos/sample-videos/small.mp4'  // Small test file
  // 'file:///storage/emulated/0/Movies/your_video.mp4'
  
  // 🔒 HTTPS VIDEO FILES (requires FFmpeg with TLS backend):
  // 'https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4'
  // 'https://sample-videos.com/zip/10/mp4/SampleVideo_1280x720_1mb.mp4'
  
  // 🔴 LIVE STREAMS (will use high refresh rate, low latency):
  // 'rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov'
  // 'rtsp://184.72.239.149/vod/mp4:BigBuckBunny_115k.mov'  // Alternative RTSP
  // 'http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/ElephantsDream.mp4'
  
  // 🎵 AUDIO STREAMS:
  // 'http://ice1.somafm.com/groovesalad-256-mp3'  // SomaFM Groove Salad
  // 'http://stream.live.vc.bbcmedia.co.uk/bbc_world_service'  // BBC World Service
  @override
  void initState() {
    super.initState();
    // Don't auto-create player - let user click "Create Player" button
    // This gives better control and clearer testing flow
  }
  
  @override
  void dispose() {
    _stopPositionTimer();
    _stopRefreshTimer();
    _releasePlayer();
    super.dispose();
  }
  
  Future<void> _createPlayer() async {
    try {
      setState(() => _status = 'Creating player...');
      
      final result = await _channel.invokeMethod('create');
      final textureId = result['textureId'] as int;
      
      setState(() {
        _textureId = textureId;
        _status = 'Player created (texture: $textureId)';
      });
      
      debugPrint('✅ Player created with texture ID: $textureId');
    } catch (e) {
      setState(() => _status = 'Create failed: $e');
      debugPrint('❌ Failed to create player: $e');
    }
  }
  
  Future<void> _setDataSource() async {
    try {
      setState(() => _status = 'Setting data source...');
      
      await _channel.invokeMethod('setDataSource', {'url': _testUrl});
      
      setState(() => _status = 'Data source set');
      debugPrint('✅ Data source set: $_testUrl');
    } catch (e) {
      setState(() => _status = 'Set data source failed: $e');
      debugPrint('❌ Failed to set data source: $e');
    }
  }
  
  Future<void> _prepare() async {
    try {
      setState(() => _status = 'Preparing...');
      
      final info = await _channel.invokeMethod('prepare');
      final width = info['width'] as int;
      final height = info['height'] as int;
      final duration = info['duration'] as int;
      
      // Detect stream type and adapt timing
      _isLiveStream = _isLiveStreamUrl(_testUrl) || (duration == 0);
      
      // Get actual frame rate for video files
      if (_isLiveStream) {
        _videoFrameRate = 30.0; // Common for live streams
        _refreshInterval = 16;  // High refresh rate for live (~60fps UI)
      } else {
        // For video files, get the actual frame rate
        double actualFps = await _getActualFrameRate();
        if (actualFps > 0) {
          _videoFrameRate = actualFps;
          debugPrint('🎬 Detected actual frame rate: ${actualFps}fps');
        } else {
          // Fallback to conservative estimates
          if (height >= 1080) {
            _videoFrameRate = 25.0; // Common for HD content
          } else if (height >= 720) {
            _videoFrameRate = 24.0; // Standard cinema rate
          } else {
            _videoFrameRate = 23.976; // True cinema rate for SD
          }
          debugPrint('📐 Using estimated frame rate: ${_videoFrameRate}fps');
        }
        // Use much slower refresh timing for video files to prevent fast playback
        // Video files don't need fast UI refresh - they need proper pacing
        int calculatedInterval = (1000 / _videoFrameRate * 2.5).round();
        _refreshInterval = calculatedInterval < 100 ? 100 : calculatedInterval; // Minimum 100ms, 2.5x slower than video fps
        debugPrint('⏱️ Video refresh interval: ${_refreshInterval}ms (${(1000/_refreshInterval).toStringAsFixed(1)}fps UI refresh)');
      }
      
      setState(() {
        _videoWidth = width;
        _videoHeight = height;
        _duration = duration;
        _status = 'Prepared: ${width}x$height, ${_formatDuration(duration)}';
      });
      
      debugPrint('✅ Player prepared: ${width}x$height, duration: $duration ms');
      debugPrint('📺 Stream detected: Live=$_isLiveStream, FPS=$_videoFrameRate, RefreshInterval=${_refreshInterval}ms');
    } catch (e) {
      setState(() => _status = 'Prepare failed: $e');
      debugPrint('❌ Failed to prepare: $e');
    }
  }
  
  Future<void> _start() async {
    try {
      await _channel.invokeMethod('start');
      
      setState(() {
        _isPlaying = true;
        _status = 'Playing';
      });
      
      // Start position updates
      _startPositionTimer();
      
      // Start UI refresh timer for smooth video display
      _startRefreshTimer();
      
      debugPrint('✅ Playback started');
    } catch (e) {
      setState(() => _status = 'Start failed: $e');
      debugPrint('❌ Failed to start: $e');
    }
  }
  
  Future<void> _pause() async {
    try {
      await _channel.invokeMethod('pause');
      
      setState(() {
        _isPlaying = false;
        _status = 'Paused';
      });
      
      _stopPositionTimer(); // Stop position updates
      _stopRefreshTimer(); // Stop UI refresh timer
      
      debugPrint('✅ Playback paused');
    } catch (e) {
      setState(() => _status = 'Pause failed: $e');
      debugPrint('❌ Failed to pause: $e');
    }
  }
  
  Future<void> _stop() async {
    try {
      await _channel.invokeMethod('stop');
      
      setState(() {
        _isPlaying = false;
        _position = 0;
        _status = 'Stopped';
      });
      
      _stopPositionTimer(); // Stop position updates
      _stopRefreshTimer(); // Stop UI refresh timer
      
      debugPrint('✅ Playback stopped');
    } catch (e) {
      setState(() => _status = 'Stop failed: $e');
      debugPrint('❌ Failed to stop: $e');
    }
  }
  
  Future<void> _seekTo(int position) async {
    try {
      await _channel.invokeMethod('seekTo', {'position': position});
      
      setState(() => _position = position);
      debugPrint('✅ Seeked to: $position ms');
    } catch (e) {
      debugPrint('❌ Failed to seek: $e');
    }
  }
  
  void _startPositionTimer() {
    _positionTimer?.cancel();
    _positionTimer = Timer.periodic(Duration(seconds: 2), (timer) async {
      if (!_isPlaying) {
        timer.cancel();
        return;
      }
      
      try {
        final position = await _channel.invokeMethod('getPosition') as int;
        if (mounted) {
          setState(() => _position = position);
        }
      } catch (e) {
        debugPrint('Failed to get position: $e');
      }
    });
  }
  
  void _stopPositionTimer() {
    _positionTimer?.cancel();
    _positionTimer = null;
  }
  
  void _startRefreshTimer() {
    _refreshTimer?.cancel();
    
    // Adaptive refresh rate based on stream type
    int interval;
    if (_isLiveStream) {
      // Live streams: high refresh rate for low latency
      interval = 16; // ~60fps for responsiveness
    } else {
      // Video files: match video frame rate
      interval = _refreshInterval;
    }
    
    _refreshTimer = Timer.periodic(Duration(milliseconds: interval), (timer) {
      // Trigger UI refresh for smooth video display
      if (!_isPlaying) {
        timer.cancel();
        return;
      }
      
      if (mounted) {
        // Force UI rebuild to check for texture updates
        setState(() {});
      }
    });
    
    debugPrint('🔄 Refresh timer started: ${interval}ms (${(1000/interval).toStringAsFixed(1)}fps) - Live: $_isLiveStream');
  }
  
  void _stopRefreshTimer() {
    _refreshTimer?.cancel();
    _refreshTimer = null;
  }
  
  bool _isLiveStreamUrl(String url) {
    // Check for live streaming protocols
    if (url.startsWith('rtsp://') || 
        url.startsWith('rtmp://') ||
        url.startsWith('rtp://')) {
      return true;
    }
    
    // Check for live streaming formats in HTTP URLs
    if (url.startsWith('http://') || url.startsWith('https://')) {
      if (url.contains('.m3u8') || // HLS
          url.contains('/live/') ||
          url.contains('live=')) {
        return true;
      }
    }
    
    return false;
  }
  
  Future<void> _releasePlayer() async {
    try {
      await _channel.invokeMethod('release');
      debugPrint('✅ Player released');
    } catch (e) {
      debugPrint('❌ Failed to release: $e');
    }
  }
  
  Future<double> _getActualFrameRate() async {
    try {
      final result = await _channel.invokeMethod('getFrameRate');
      if (result != null && result > 0) {
        return result.toDouble();
      }
    } catch (e) {
      debugPrint('⚠️ Could not get frame rate: $e');
    }
    return 0.0; // Unknown frame rate
  }

  Future<bool> _testHttpsConnectivity() async {
    try {
      final result = await _channel.invokeMethod('testHttpsConnectivity', {
        'url': 'https://httpbin.org/get'
      });
      return result == true;
    } catch (e) {
      debugPrint('🔒 HTTPS connectivity test failed: $e');
      return false;
    }
  }

  String _formatDuration(int ms) {
    final seconds = ms ~/ 1000;
    final minutes = seconds ~/ 60;
    final secs = seconds % 60;
    return '${minutes.toString().padLeft(2, '0')}:${secs.toString().padLeft(2, '0')}';
  }
  
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text('Native Player Test'),
        backgroundColor: Colors.blue,
      ),
      body: Column(
        children: [
          // Status
          Container(
            width: double.infinity,
            padding: EdgeInsets.all(16),
            color: Colors.grey[200],
            child: Text(
              _status,
              style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold),
            ),
          ),
          
          // Video display
          Expanded(
            child: Container(
              color: Colors.black,
              child: _textureId != null
                  ? Texture(textureId: _textureId!)
                  : Center(
                      child: Text(
                        'No video',
                        style: TextStyle(color: Colors.white),
                      ),
                    ),
            ),
          ),
          
          // Video info
          Container(
            padding: EdgeInsets.all(8),
            child: Column(
              children: [
                Text('Size: ${_videoWidth}x$_videoHeight'),
                Text(
                  'Position: ${_formatDuration(_position)} / ${_formatDuration(_duration)}',
                ),
              ],
            ),
          ),
          
          // Seek bar
          if (_duration > 0)
            Slider(
              value: _position.toDouble(),
              max: _duration.toDouble(),
              onChanged: (value) {
                _seekTo(value.toInt());
              },
            ),
          
          // Control buttons
          // Control buttons
          Padding(
            padding: EdgeInsets.all(16),
            child: Column(
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                  children: [
                    ElevatedButton(
                      onPressed: _textureId == null ? _createPlayer : null,
                      child: Text('Create Player'),
                    ),
                    ElevatedButton(
                      onPressed: _textureId != null ? _setDataSource : null,
                      child: Text('Set URL'),
                    ),
                  ],
                ),
                SizedBox(height: 8),
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                  children: [
                    ElevatedButton(
                      onPressed: _textureId != null ? _prepare : null,
                      child: Text('Prepare'),
                    ),
                  ],
                ),
                SizedBox(height: 8),
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                  children: [
                    ElevatedButton(
                      onPressed: _isPlaying ? null : _start,
                      child: Text('Play'),
                    ),
                    ElevatedButton(
                      onPressed: _isPlaying ? _pause : null,
                      child: Text('Pause'),
                    ),
                    ElevatedButton(
                      onPressed: _stop,
                      child: Text('Stop'),
                    ),
                  ],
                ),
                SizedBox(height: 8),
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                  children: [
                    ElevatedButton(
                      onPressed: () async {
                        debugPrint('🔒 Testing HTTPS connectivity...');
                        bool result = await _testHttpsConnectivity();
                        setState(() => _status = result ? '🔒 HTTPS: OK' : '❌ HTTPS: Failed');
                      },
                      child: Text('Test HTTPS'),
                    ),
                  ],
                ),
              ],
            ),
          ),
          
          // Test URL info
          Padding(
            padding: EdgeInsets.all(8),
            child: Text(
              'Test: Big Buck Bunny (596s, 1280x720, HTTPS/OpenSSL)',
              style: TextStyle(fontSize: 12, color: Colors.grey),
            ),
          ),
        ],
      ),
    );
  }
}
