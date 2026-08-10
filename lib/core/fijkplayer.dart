//MIT License
//
//Copyright (c) [2019] [Befovy]
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

part of fijkplayer;

/// FijkPlayer present as a playback. It interacts with native object.
///
/// FijkPlayer invoke native method and receive native event.
class FijkPlayer extends ChangeNotifier implements ValueListenable<FijkValue> {
  static Map<int, FijkPlayer> _allInstance = HashMap();
  String? _dataSource;

  int _playerId = -1;
  int _callId = -1;
  late MethodChannel _channel;
  StreamSubscription<dynamic>? _nativeEventSubscription;

  bool _startAfterSetup = false;

  FijkValue _value;

  static Iterable<FijkPlayer> get all => _allInstance.values;

  /// Return the player unique id.
  ///
  /// Each public method in [FijkPlayer] `await` the id value firstly.
  Future<int> get id => _nativeSetup.future;

  /// Get is in sync, if the async [id] is not finished, idSync return -1;
  int get idSync => _playerId;

  /// return the current state
  FijkState get state => _value.state;

  @override
  FijkValue get value => _value;

  void _setValue(FijkValue newValue) {
    if (_value == newValue) return;
    _value = newValue;
    notifyListeners();
  }

  Duration _bufferPos = Duration();

  /// return the current buffered position
  Duration get bufferPos => _bufferPos;

  final StreamController<Duration> _bufferPosController =
      StreamController.broadcast();

  /// stream of [bufferPos].
  Stream<Duration> get onBufferPosUpdate => _bufferPosController.stream;

  int _bufferPercent = 0;

  /// return the buffer percent of water mark.
  ///
  /// If player is in [FijkState.started] state and is freezing ([isBuffering] is true),
  /// this value starts from 0, and when reaches or exceeds 100, the player start to play again.
  ///
  /// This is not the quotient of [bufferPos] / [value.duration]
  int get bufferPercent => _bufferPercent;

  final StreamController<int> _bufferPercentController =
      StreamController.broadcast();

  /// stream of [bufferPercent].
  Stream<int> get onBufferPercentUpdate => _bufferPercentController.stream;

  Duration _currentPos = Duration();

  /// return the current playing position
  Duration get currentPos => _currentPos;

  /// Query native player for actual current position.
  /// Unlike [currentPos], this queries the native side directly.
  /// Useful for detecting frame stalls (position stops changing when no frames are decoded).
  /// Returns -1 if the player is not in a playable state (e.g. after reset).
  Future<int> getCurrentPos() async {
    if (state == FijkState.idle || state == FijkState.end) {
      return -1;
    }
    await _nativeSetup.future;
    try {
      int pos = await _channel.invokeMethod("getCurrentPosition") ?? 0;
      return pos;
    } on MissingPluginException {
      return -1;
    }
  }

  final StreamController<Duration> _currentPosController =
      StreamController.broadcast();

  /// stream of [currentPos].
  Stream<Duration> get onCurrentPosUpdate => _currentPosController.stream;

  bool _buffering = false;
  bool _seeking = false;

  /// return true if the player is buffering
  bool get isBuffering => _buffering;

  final StreamController<bool> _bufferStateController =
      StreamController.broadcast();

  Stream<bool> get onBufferStateUpdate => _bufferStateController.stream;

  String? get dataSource => _dataSource;

  final Completer<int> _nativeSetup;
  Completer<Uint8List>? _snapShot;
  Completer<void>? _recording;
  bool _isRecording = false;
  Timer? _posTimer;

  FijkPlayer()
      : _nativeSetup = Completer(),
        _value = FijkValue.uninitialized(),
        super() {
    FijkLog.d("create new fijkplayer");
    _doNativeSetup();
  }

  Future<void> _startFromAnyState() async {
    await _nativeSetup.future;

    if (state == FijkState.error || state == FijkState.stopped) {
      await reset();
    }
    String? source = _dataSource;
    if (state == FijkState.idle && source != null) {
      await setDataSource(source);
    }
    if (state == FijkState.initialized) {
      await prepareAsync();
    }
    if (state == FijkState.asyncPreparing ||
        state == FijkState.prepared ||
        state == FijkState.completed ||
        state == FijkState.paused) {
      await start();
    }
  }

  Future<dynamic> _handler(MethodCall call) {
    switch (call.method) {
      case "_onSnapshot":
        var img = call.arguments;
        var snapShot = _snapShot;
        if (snapShot != null) {
          if (img is Map) {
            final data = img['data'];
            if (data is Uint8List) {
              snapShot.complete(data);
            } else {
              snapShot.completeError(StateError("snapshot data unavailable"));
            }
          } else {
            snapShot.completeError(UnsupportedError("snapshot"));
          }
        }
        _snapShot = null;
        break;
      case "_onRecordingStarted":
        var recording = _recording;
        if (recording != null && !recording.isCompleted) {
          _isRecording = true;
          recording.complete();
        }
        break;
      case "_onRecordingStopped":
        _isRecording = false;
        break;
      case "_onRecordingError":
        var recording = _recording;
        if (recording != null && !recording.isCompleted) {
          var error = call.arguments;
          recording.completeError(Exception("Recording error: $error"));
        }
        _isRecording = false;
        break;
      default:
        break;
    }
    return Future.value(0);
  }

  Future<void> _doNativeSetup() async {
    _playerId = -1;
    _callId = 0;
    _playerId = await FijkPlugin._createPlayer();
    if (_playerId < 0) {
      _setValue(value.copyWith(state: FijkState.error));
      return;
    }
    FijkLog.i("create player id:$_playerId");

    _allInstance[_playerId] = this;
    _channel = MethodChannel('befovy.com/fijkplayer/' + _playerId.toString());
    _nativeEventSubscription =
        EventChannel('befovy.com/fijkplayer/event/' + _playerId.toString())
            .receiveBroadcastStream()
            .listen(_eventListener, onError: _errorListener);
    _nativeSetup.complete(_playerId);

    _channel.setMethodCallHandler(_handler);
    if (_startAfterSetup) {
      FijkLog.i("player id:$_playerId, start after setup");
      await _startFromAnyState();
    }
  }

  /// Check if player is playable
  ///
  /// Only the four state [FijkState.prepared] \ [FijkState.started] \
  /// [FijkState.paused] \ [FijkState.completed] are playable
  bool isPlayable() {
    FijkState current = value.state;
    return FijkState.prepared == current ||
        FijkState.started == current ||
        FijkState.paused == current ||
        FijkState.completed == current;
  }

  /// Start position timer to update position stream and trigger texture updates
  void _startPosTimer() {
    _posTimer?.cancel();
    // Timer serves two purposes:
    // 1. Update position stream with actual player position
    // 2. Call notifyListeners() to trigger FijkView texture rebuilds (critical for smooth playback)
    _posTimer = Timer.periodic(Duration(milliseconds: 200), (timer) async {
      if (state == FijkState.started) {
        // Query actual position from native player
        try {
          int pos = await _channel.invokeMethod("getCurrentPosition") ?? 0;
          _currentPos = Duration(milliseconds: pos);
          if (!_seeking) {
            _currentPosController.add(_currentPos);
          }
        } catch (_) {}
        // Trigger FijkView updates for texture rendering
        notifyListeners();
      }
    });
    FijkLog.d("$this position timer started");
  }

  /// Stop position timer
  void _stopPosTimer() {
    _posTimer?.cancel();
    _posTimer = null;
    FijkLog.d("$this position timer stopped");
  }

  /// set option
  /// [value] must be int or String
  Future<void> setOption(int category, String key, dynamic value) async {
    await _nativeSetup.future;
    if (value is String) {
      FijkLog.i("$this setOption k:$key, v:$value");
      return _channel.invokeMethod("setOption",
          <String, dynamic>{"cat": category, "key": key, "str": value});
    } else if (value is int) {
      FijkLog.i("$this setOption k:$key, v:$value");
      return _channel.invokeMethod("setOption",
          <String, dynamic>{"cat": category, "key": key, "long": value});
    } else {
      FijkLog.e("$this setOption invalid value: $value");
      return Future.error(
          ArgumentError.value(value, "value", "Must be int or String"));
    }
  }

  Future<void> applyOptions(FijkOption fijkOption) async {
    await _nativeSetup.future;
    return _channel.invokeMethod("applyOptions", fijkOption.data);
  }

  Future<int?> setupSurface() async {
    await _nativeSetup.future;
    FijkLog.i("$this setupSurface");
    return _channel.invokeMethod("setupSurface");
  }

  /// Take snapshot (screen shot) of current playing video
  ///
  /// If you want to use [takeSnapshot], you must call
  /// `player.setOption(FijkOption.hostCategory, "enable-snapshot", 1);`
  /// after you create a [FijkPlayer].
  /// Or else this method returns error.
  ///
  /// Example:
  /// ```
  /// var imageData = await player.takeSnapShot();
  /// var provider = MemoryImage(v);
  /// Widget image = Image(image: provider)
  /// ```
  Future<Uint8List> takeSnapShot() async {
    await _nativeSetup.future;
    FijkLog.i("$this takeSnapShot");
    var snapShot = _snapShot;
    if (snapShot != null && !snapShot.isCompleted) {
      return Future.error(StateError("last snapShot is not finished"));
    }
    snapShot = Completer<Uint8List>();
    _snapShot = snapShot;
    try {
      await _channel.invokeMethod("snapshot");
    } catch (e) {
      if (!snapShot.isCompleted) {
        snapShot.completeError(e);
      }
      _snapShot = null;
    }
    return snapShot.future;
  }

  /// Get current recording status
  bool get isRecording => _isRecording;

  /// Start video recording to file
  ///
  /// [path] is the output file path where the video will be saved
  /// The video will be recorded in MP4/H.264 format
  ///
  /// If you want to use [startRecording], you must ensure the player is in a playable state.
  /// The recording will capture the same video content that is being displayed.
  ///
  /// Example:
  /// ```
  /// await player.startRecording('/path/to/output.mp4');
  /// // ... recording in progress
  /// await player.stopRecording();
  /// ```
  Future<void> startRecording(String path) async {
    // Keep legacy API name but route to FFmpeg implementation.
    await startFFmpegRecording(path);
  }

  /// Stop video recording
  ///
  /// This method stops the current recording session and finalizes the output file.
  /// The recorded video file will be available at the path specified in [startRecording].
  ///
  /// Example:
  /// ```
  /// await player.stopRecording();
  /// ```
  Future<void> stopRecording() async {
    // Keep legacy API name but route to FFmpeg implementation.
    await stopFFmpegRecording();
  }

  /// Start FFmpeg-based video recording to file
  ///
  /// This method uses FFmpeg to record the RTSP stream directly to a file,
  /// which avoids the EGL context issues with the MediaCodec approach.
  ///
  /// [path] is the output file path where the video will be saved (MP4 format)
  /// The video will be recorded without audio (video only)
  ///
  /// Example:
  /// ```
  /// await player.startFFmpegRecording('/path/to/output.mp4');
  /// // ... recording in progress
  /// await player.stopFFmpegRecording();
  /// ```
  Future<void> startFFmpegRecording(String path) async {
    await _nativeSetup.future;
    
    // Check if already recording using the FFmpeg method
    bool isAlreadyRecording = await _channel.invokeMethod("isFFmpegRecording");
    if (isAlreadyRecording) {
      return Future.error(StateError("FFmpeg recording is already in progress"));
    }
    
    if (!isPlayable()) {
      return Future.error(StateError("Player must be in playable state to start recording"));
    }
    
    FijkLog.i("$this startFFmpegRecording to $path");
    
    var recording = Completer<void>();
    _recording = recording;
    
    try {
      await _channel.invokeMethod("startFFmpegRecording", <String, dynamic>{'path': path});
      await recording.future;
      FijkLog.i("$this FFmpeg recording started successfully");
    } catch (e) {
      _recording = null;
      _isRecording = false;
      rethrow;
    }
  }

  /// Stop FFmpeg-based video recording
  ///
  /// This method stops the current FFmpeg recording session and finalizes the output file.
  /// The recorded video file will be available at the path specified in [startFFmpegRecording].
  ///
  /// Example:
  /// ```
  /// await player.stopFFmpegRecording();
  /// ```
  Future<void> stopFFmpegRecording() async {
    await _nativeSetup.future;
    
    // Check if there's an active FFmpeg recording
    bool isCurrentlyRecording = await _channel.invokeMethod("isFFmpegRecording");
    if (!isCurrentlyRecording) {
      return Future.error(StateError("No FFmpeg recording in progress"));
    }
    
    FijkLog.i("$this stopFFmpegRecording");
    await _channel.invokeMethod("stopFFmpegRecording");
    _recording = null;
    _isRecording = false;
  }

  /// Check if FFmpeg recording is currently active
  ///
  /// Returns true if FFmpeg recording is in progress, false otherwise
  Future<bool> isFFmpegRecording() async {
    await _nativeSetup.future;
    return await _channel.invokeMethod("isFFmpegRecording");
  }

  /// Set data source for this player
  ///
  /// [path] must be a valid uri, otherwise this method return ArgumentError
  ///
  /// set assets as data source
  /// first add assets in app's pubspec.yml
  ///   assets:
  ///     - assets/butterfly.mp4
  ///
  /// pass "asset:///assets/butterfly.mp4" to [path]
  /// scheme is `asset`, `://` is scheme's separator， `/` is path's separator.
  ///
  /// If set [autoPlay] true, player will stat to play.
  /// The behavior of [setDataSource(url, autoPlay: true)] is like
  ///    await setDataSource(url);
  ///    await setOption(FijkOption.playerCategory, "start-on-prepared", 1);
  ///    await prepareAsync();
  ///
  /// If set [showCover] true, player will display the first video frame and then enter [FijkState.paused] state.
  /// The behavior of [setDataSource(url, showCover: true)] is like
  ///    await setDataSource(url);
  ///    await setOption(FijkOption.playerCategory, "cover-after-prepared", 1);
  ///    await prepareAsync();
  ///
  /// If both [autoPlay] and [showCover] are true, [showCover] will be ignored.
  Future<void> setDataSource(
    String path, {
    bool autoPlay = false,
    bool showCover = false,
  }) async {
    if (path.length == 0 || Uri.tryParse(path) == null) {
      FijkLog.e("$this setDataSource invalid path:$path");
      return Future.error(
          ArgumentError.value(path, "path must be a valid url"));
    }
    if (autoPlay == true && showCover == true) {
      FijkLog.w(
          "call setDataSource with both autoPlay and showCover true, showCover will be ignored");
    }
    await _nativeSetup.future;
    if (state == FijkState.idle || state == FijkState.initialized) {
      try {
        FijkLog.i("$this invoke setDataSource $path");
        _dataSource = path;
        await _channel
            .invokeMethod("setDataSource", <String, dynamic>{'url': path});
      } on PlatformException catch (e) {
        return _errorListener(e);
      }
      if (autoPlay == true) {
        await start();
      } else if (showCover == true) {
        await setOption(FijkOption.playerCategory, "cover-after-prepared", 1);
        await prepareAsync();
      }
    } else {
      FijkLog.e("$this setDataSource invalid state:$state");
      return Future.error(StateError("setDataSource on invalid state $state"));
    }
  }

  /// start the async preparing tasks
  ///
  /// see [fijkstate zh](https://fijkplayer.befovy.com/docs/zh/fijkstate.html) or
  /// [fijkstate en](https://fijkplayer.befovy.com/docs/en/fijkstate.html) for details
  Future<void> prepareAsync() async {
    await _nativeSetup.future;
    if (state == FijkState.initialized) {
      FijkLog.i("$this invoke prepareAsync");
      await _channel.invokeMethod("prepareAsync");
    } else {
      FijkLog.e("$this prepareAsync invalid state:$state");
      return Future.error(StateError("prepareAsync on invalid state $state"));
    }
  }

  /// Set playback mode for different scenarios
  ///
  /// Configure the player for optimal performance based on the use case:
  /// - [FijkPlaybackMode.liveLowLatency]: Real-time live video, no audio, minimum latency
  /// - [FijkPlaybackMode.liveWithAudio]: Live video with synchronized audio
  /// - [FijkPlaybackMode.vodOptimized]: Video-on-demand with smooth buffering
  ///
  /// This method should be called after creating the player and before setting the data source.
  ///
  /// Example:
  /// ```dart
  /// // For security camera live feed
  /// await player.setPlaybackMode(FijkPlaybackConfig.liveLowLatency());
  /// await player.setDataSource(rtspUrl);
  ///
  /// // For live streaming with audio
  /// await player.setPlaybackMode(FijkPlaybackConfig.liveWithAudio());
  /// await player.setDataSource(rtspUrl);
  ///
  /// // For recorded video playback
  /// await player.setPlaybackMode(FijkPlaybackConfig.vodOptimized());
  /// await player.setDataSource(videoUrl);
  /// ```
  Future<void> setPlaybackMode(FijkPlaybackConfig config) async {
    await _nativeSetup.future;
    FijkLog.i("$this setPlaybackMode ${config.mode}");
    return _channel.invokeMethod("setPlaybackMode", config.toMap());
  }

  /// set volume of this player audio track
  ///
  /// This dose not change system volume.
  /// Default value of audio track is 1.0,
  /// [volume] must be greater or equals to 0.0
  Future<void> setVolume(double volume) async {
    if (volume < 0) {
      FijkLog.e("$this invoke seekTo invalid volume:$volume");
      return Future.error(
          ArgumentError.value(volume, "setVolume invalid volume"));
    } else {
      await _nativeSetup.future;
      FijkLog.i("$this invoke setVolume $volume");
      return _channel
          .invokeMethod("setVolume", <String, dynamic>{"volume": volume});
    }
  }

  /// enter full screen mode, set [FijkValue.fullScreen] to true
  void enterFullScreen() {
    FijkLog.i("$this enterFullScreen");
    _setValue(value.copyWith(fullScreen: true));
  }

  /// exit full screen mode, set [FijkValue.fullScreen] to false
  void exitFullScreen() {
    FijkLog.i("$this exitFullScreen");
    _setValue(value.copyWith(fullScreen: false));
  }

  /// change player's state to [FijkState.started]
  ///
  /// throw [StateError] if call this method on invalid state.
  /// see [fijkstate zh](https://fijkplayer.befovy.com/docs/zh/fijkstate.html) or
  /// [fijkstate en](https://fijkplayer.befovy.com/docs/en/fijkstate.html) for details
  Future<void> start() async {
    await _nativeSetup.future;
    try {
      if (state == FijkState.initialized) {
        _callId += 1;
        int cid = _callId;
        FijkLog.i("$this invoke prepareAsync and start #$cid");
        await setOption(FijkOption.playerCategory, "start-on-prepared", 1);
        await _channel.invokeMethod("prepareAsync");
        // Native backend currently treats setOption as a compatibility no-op,
        // so start-on-prepared is not guaranteed. Explicitly start once
        // preparation reaches a playable state.
        int waitCount = 0;
        while (waitCount < 300 &&
            state != FijkState.prepared &&
            state != FijkState.started &&
            state != FijkState.error &&
            state != FijkState.end) {
          await Future.delayed(Duration(milliseconds: 20));
          waitCount++;
        }
        if (state == FijkState.prepared) {
          await _channel.invokeMethod("start");
        }
        FijkLog.i("$this invoke prepareAsync and start #$cid -> done");
      } else if (state == FijkState.asyncPreparing ||
          state == FijkState.prepared ||
          state == FijkState.paused ||
          state == FijkState.started ||
          value.state == FijkState.completed) {
        FijkLog.i("$this invoke start");
        await _channel.invokeMethod("start");
      } else {
        FijkLog.e("$this invoke start invalid state:$state");
        return Future.error(StateError("call start on invalid state $state"));
      }
    } on PlatformException catch (e) {
      // During fast widget teardown/recreate, stale async calls may arrive after
      // native player release. Ignore this race instead of crashing app flow.
      if (e.code == "PLAYER_NOT_FOUND") {
        FijkLog.w("$this start ignored on released native player");
        return;
      }
      rethrow;
    }
  }

  Future<void> pause() async {
    await _nativeSetup.future;
    if (isPlayable()) {
      FijkLog.i("$this invoke pause");
      await _channel.invokeMethod("pause");
    } else if (state == FijkState.idle || 
               state == FijkState.initialized || 
               state == FijkState.stopped ||
               state == FijkState.completed) {
      // Gracefully handle pause() calls on non-playable states
      FijkLog.w("$this invoke pause on state:$state, ignoring (this is safe)");
      return; // No-op, don't throw error
    } else {
      FijkLog.e("$this invoke pause invalid state:$state");
      return Future.error(StateError("call pause on invalid state $state"));
    }
  }

  Future<void> stop() async {
    await _nativeSetup.future;
    if (state == FijkState.end) {
      FijkLog.e("$this invoke stop invalid state:$state");
      return Future.error(StateError("call stop on invalid state $state"));
    } else if (state == FijkState.idle || state == FijkState.initialized) {
      // Gracefully handle stop() calls on idle/initialized states
      // These are common when user calls stop() multiple times or before playing
      FijkLog.w("$this invoke stop on state:$state, ignoring (this is safe)");
      return; // No-op, don't throw error
    } else {
      FijkLog.i("$this invoke stop");
      await _channel.invokeMethod("stop");
    }
  }

  Future<void> reset() async {
    await _nativeSetup.future;
    if (state == FijkState.end) {
      FijkLog.e("$this invoke reset invalid state:$state");
      return Future.error(StateError("call reset on invalid state $state"));
    } else {
      _callId += 1;
      int cid = _callId;
      FijkLog.i("$this invoke reset #$cid");
      await _channel.invokeMethod("reset").then((_) {
        FijkLog.i("$this invoke reset #$cid -> done");
      });
      _setValue(
          FijkValue.uninitialized().copyWith(fullScreen: value.fullScreen));
    }
  }

  Future<void> seekTo(int msec) async {
    await _nativeSetup.future;
    if (msec < 0) {
      FijkLog.e("$this invoke seekTo invalid msec:$msec");
      return Future.error(
          ArgumentError.value(msec, "speed must be not null and >= 0"));
    } else if (!isPlayable()) {
      FijkLog.e("$this invoke seekTo invalid state:$state");
      return Future.error(StateError("Non playable state $state"));
    } else {
      FijkLog.i("$this invoke seekTo msec:$msec");
      _seeking = true;
      _channel.invokeMethod("seekTo", <String, dynamic>{"msec": msec});
    }
  }

  /// Release native player. Release memory and resource
  Future<void> release() async {
    await _nativeSetup.future;
    _callId += 1;
    int cid = _callId;
    FijkLog.i("$this invoke release #$cid");
    if (isPlayable()) await stop();
    _stopPosTimer(); // Stop position timer before release
    _setValue(value.copyWith(state: FijkState.end));
    await _nativeEventSubscription?.cancel();
    _nativeEventSubscription = null;
    _allInstance.remove(_playerId);
    await FijkPlugin._releasePlayer(_playerId).then((_) {
      FijkLog.i("$this invoke release #$cid -> done");
    });
  }

  /// Set player loop count
  ///
  /// [loopCount] must not null and greater than or equal to 0.
  /// Default loopCount of player is 1, which also means no loop.
  /// A positive value of [loopCount] means special repeat times.
  /// If [loopCount] is 0, is means infinite repeat.
  Future<void> setLoop(int loopCount) async {
    await _nativeSetup.future;
    if (loopCount < 0) {
      FijkLog.e("$this invoke setLoop invalid loopCount:$loopCount");
      return Future.error(ArgumentError.value(
          loopCount, "loopCount must not be null and >= 0"));
    } else {
      FijkLog.i("$this invoke setLoop $loopCount");
      return _channel
          .invokeMethod("setLoop", <String, dynamic>{"loop": loopCount});
    }
  }

  /// Set playback speed
  ///
  /// [speed] must not null and greater than 0.
  /// Default speed is 1
  Future<void> setSpeed(double speed) async {
    await _nativeSetup.future;
    if (speed <= 0) {
      FijkLog.e("$this invoke setSpeed invalid speed:$speed");
      return Future.error(ArgumentError.value(
          speed, "speed must be not null and greater than 0"));
    } else {
      FijkLog.i("$this invoke setSpeed $speed");
      _channel.invokeMethod("setSpeed", <String, dynamic>{"speed": speed});
    }
  }

  void _eventListener(dynamic event) {
    final Map<dynamic, dynamic> map = event;
    switch (map['event']) {
      case 'prepared':
        int duration = map['duration'] ?? 0;
        Duration dur = Duration(milliseconds: duration);
        _setValue(value.copyWith(duration: dur, prepared: true));
        FijkLog.i("$this prepared duration $dur");
        break;
      case 'rotate':
        int degree = map['degree'] ?? 0;
        _setValue(value.copyWith(rotate: degree));
        FijkLog.i("$this rotate degree $degree");
        break;
      case 'state_change':
        int newStateId = map['new'] ?? 0;
        int _oldState = map['old'] ?? 0;
        FijkState fpState = FijkState.values[newStateId];
        FijkState oldState =
            (_oldState >= 0 && _oldState < FijkState.values.length)
                ? FijkState.values[_oldState]
                : state;

        if (fpState != oldState) {
          FijkLog.i("$this state changed to $fpState <= $oldState");
          FijkException? fijkException =
              (fpState != FijkState.error) ? FijkException.noException : null;
          if (newStateId == FijkState.prepared.index) {
            _setValue(value.copyWith(
                prepared: true, state: fpState, exception: fijkException));
          } else if (newStateId < FijkState.prepared.index) {
            _setValue(value.copyWith(
                prepared: false, state: fpState, exception: fijkException));
          } else {
            _setValue(value.copyWith(state: fpState, exception: fijkException));
          }
          
          // Start/stop position timer based on playback state
          if (fpState == FijkState.started) {
            _startPosTimer();
          } else {
            _stopPosTimer();
          }
        }
        break;
      case 'rendering_start':
        String type = map['type'] ?? "none";
        if (type == "video") {
          _setValue(value.copyWith(videoRenderStart: true));
          FijkLog.i("$this video rendering started");
        } else if (type == "audio") {
          _setValue(value.copyWith(audioRenderStart: true));
          FijkLog.i("$this audio rendering started");
        }
        break;
      case 'freeze':
        bool value = map['value'] ?? false;
        _buffering = value;
        _bufferStateController.add(value);
        FijkLog.d("$this freeze ${value ? "start" : "end"}");
        break;
      case 'buffering':
        int head = map['head'] ?? 0;
        int percent = map['percent'] ?? 0;
        _bufferPos = Duration(milliseconds: head);
        _bufferPosController.add(_bufferPos);
        _bufferPercent = percent;
        _bufferPercentController.add(percent);
        break;
      case 'pos':
        int pos = map['pos'];
        _currentPos = Duration(milliseconds: pos);
        if (!_seeking) {
          _currentPosController.add(_currentPos);
        }
        break;
      case 'size_changed':
        double width = map['width'].toDouble();
        double height = map['height'].toDouble();
        FijkLog.i("$this size changed ($width, $height)");
        _setValue(value.copyWith(size: Size(width, height)));
        break;
      case 'seek_complete':
        _seeking = false;
        break;
      case 'native_error':
        String msg = map['message'] ?? 'Unknown native error';
        FijkLog.e("$this NATIVE ERROR: $msg");
        break;
      case 'error':
        String msg = map['message']?.toString() ?? 'Unknown error';
        FijkLog.e("$this error event: code=${map['code']}, message=$msg");
        break;
      default:
        break;
    }
  }

  void _errorListener(Object obj) {
    final PlatformException e = obj as PlatformException;
    FijkException exception = FijkException.fromPlatformException(e);
    FijkLog.e("$this errorListener: $exception");
    _setValue(value.copyWith(exception: exception));
  }

  @override
  String toString() {
    return 'FijkPlayer{id:$_playerId}';
  }
}
