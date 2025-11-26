import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// Test widget for NativePlayer - Direct validation
/// Bypasses fijkplayer plugin for quick testing
class NativePlayerTestPage extends StatefulWidget {
  @override
  _NativePlayerTestPageState createState() => _NativePlayerTestPageState();
}

class _NativePlayerTestPageState extends State<NativePlayerTestPage> {
  static const MethodChannel _channel = MethodChannel('befovy.com/fijk/native_player_test');
  
  int? _textureId;
  String _status = 'Not initialized';
  int _videoWidth = 0;
  int _videoHeight = 0;
  int _duration = 0;
  int _position = 0;
  bool _isPlaying = false;
  
  // Test with local file - focus on FFmpeg integration first
  final String _testUrl = 'file:///storage/emulated/0/Movies/bbb_sunflower_1080p_60fps_normal.mp4';
  
  // Network streams for future testing:
  // RTSP: 'rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov' (connection timeout)
  // HTTP MP4: 'http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4'
  
  // For HLS testing:
  // final String _testUrl = 'https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8';
  @override
  void initState() {
    super.initState();
    // Don't auto-create player - let user click "Create Player" button
    // This gives better control and clearer testing flow
  }
  
  @override
  void dispose() {
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
      
      setState(() {
        _videoWidth = width;
        _videoHeight = height;
        _duration = duration;
        _status = 'Prepared: ${width}x$height, ${_formatDuration(duration)}';
      });
      
      debugPrint('✅ Player prepared: ${width}x$height, duration: $duration ms');
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
      _updatePosition();
      
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
  
  Future<void> _updatePosition() async {
    if (!_isPlaying) return;
    
    try {
      final position = await _channel.invokeMethod('getPosition') as int;
      setState(() => _position = position);
    } catch (e) {
      debugPrint('Failed to get position: $e');
    }
    
    // Update every 500ms
    if (_isPlaying) {
      await Future.delayed(Duration(milliseconds: 500));
      _updatePosition();
    }
  }
  
  Future<void> _releasePlayer() async {
    try {
      await _channel.invokeMethod('release');
      debugPrint('✅ Player released');
    } catch (e) {
      debugPrint('❌ Failed to release: $e');
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
              ],
            ),
          ),
          
          // Test URL info
          Padding(
            padding: EdgeInsets.all(8),
            child: Text(
              'Test: Big Buck Bunny (596s, 1280x720, local file)',
              style: TextStyle(fontSize: 12, color: Colors.grey),
            ),
          ),
        ],
      ),
    );
  }
}
