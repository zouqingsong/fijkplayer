# fijkplayer - Flutter Media Player Plugin

[![pub package](https://img.shields.io/pub/v/fijkplayer.svg)](https://pub.dartlang.org/packages/fijkplayer) &nbsp; &nbsp;
[![GitHub](https://img.shields.io/github/stars/zouqingsong/fijkplayer?style=social)](https://github.com/zouqingsong/fijkplayer) &nbsp; &nbsp;

A high-performance Flutter media player plugin with native FFmpeg-based implementation for iOS and Android.

**✨ Latest Updates (v0.11.0+):** 
- ✅ Native FFmpeg 6.1 player with hardware acceleration
- ✅ MediaCodec (Android) / VideoToolbox (iOS) video decoding
- ✅ ~15MB smaller binary size vs ijkplayer
- ✅ Fixed Android video display (texture ID 0 handling)
- ✅ Native recording support (Android)

[Feedback welcome](https://github.com/zouqingsong/fijkplayer/issues) and
[Pull Requests](https://github.com/zouqingsong/fijkplayer/pulls) are most welcome!

## Documentation 文档

* dart api https://pub.dev/documentation/fijkplayer/ detail API and argument explaination
* Release Notes https://github.com/zouqingsong/fijkplayer/releases and [CHANGELOG.md](./CHANGELOG.md)

## Installation 安装

Add `fijkplayer` as a [dependency in your pubspec.yaml file](https://flutter.io/using-packages/). 

[![pub package](https://img.shields.io/pub/v/fijkplayer.svg)](https://pub.dartlang.org/packages/fijkplayer)

```yaml
dependencies:
  fijkplayer: ^{{latest version}}
```

Replace `{{latest version}}` with the version number in badge above.

Use git branch which not published to pub.
```yaml
dependencies:
  fijkplayer:
    git:
      url: https://github.com/befovy/fijkplayer.git
      ref: develop # can be replaced to branch or tag name
```

## Example 示例

```dart
import 'package:fijkplayer/fijkplayer.dart';
import 'package:flutter/material.dart';

class VideoScreen extends StatefulWidget {
  final String url;

  VideoScreen({@required this.url});

  @override
  _VideoScreenState createState() => _VideoScreenState();
}

class _VideoScreenState extends State<VideoScreen> {
  final FijkPlayer player = FijkPlayer();

  _VideoScreenState();

  @override
  void initState() {
    super.initState();
    player.setDataSource(widget.url, autoPlay: true);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
        appBar: AppBar(title: Text("Fijkplayer Example")),
        body: Container(
          alignment: Alignment.center,
          child: FijkView(
            player: player,
          ),
        ));
  }

  @override
  void dispose() {
    super.dispose();
    player.release();
  }
}

```

## Contributors 贡献者 ✨

Thanks goes to [these wonderful people](./CONTRIBUTORS.md) ([emoji key](https://allcontributors.org/docs/en/emoji-key))

This project follows the [all-contributors](https://github.com/all-contributors/all-contributors) specification. Contributions of any kind welcome!

## Architecture 架构

### Native Player Architecture (v0.11.0+)

```
Flutter App (Dart)
    ↓ MethodChannel
    ├─ Android: NativePlayer (FFmpeg 6.1 + MediaCodec)
    │   ├─ FFmpeg demuxing (RTSP/HTTP/HTTPS/HLS)
    │   ├─ Hardware H.264 decoding (MediaCodec)
    │   ├─ Software AAC decoding (FFmpeg)
    │   ├─ Android AudioTrack playback
    │   ├─ Per-player channel architecture
    │   └─ Native recording with FFmpeg muxing
    │
    └─ iOS: FJKNativePlayer (FFmpeg 6.1 + VideoToolbox)
        ├─ FFmpeg demuxing (RTSP/HTTP/HTTPS/HLS)
        ├─ Hardware H.264 decoding (VideoToolbox)
        ├─ Hardware AAC decoding (AudioToolbox)
        ├─ AVAudioEngine playback with precise timing
        └─ CVPixelBuffer rendering with automatic vsync

Key Components:
• Audio/Video synchronization using ijkplayer's proven algorithm
• Frame-accurate A/V sync with drift correction
• Texture-based video rendering (Flutter Texture widget)
• Per-player isolated channels (supports multiple simultaneous players)
```

### Supported Formats
- **Video**: H.264 (AVC), HEVC/H.265 (experimental)
- **Audio**: AAC (LC/HE-AAC), MP3
- **Protocols**: RTSP, RTP, HTTP, HTTPS, TLS, file://
- **Containers**: MP4, FLV, TS, M3U8 (HLS), MOV

### Platform Support

**iOS**
- ✅ Physical devices (arm64)
- ✅ iOS Simulator (arm64)
- ✅ Hardware acceleration (VideoToolbox + AudioToolbox)
- ✅ HTTPS/TLS support (SecureTransport)

**Android**
- ✅ Physical devices (arm64-v8a)
- ✅ Android Emulator
- ✅ Hardware acceleration (MediaCodec)
- ✅ HTTPS/TLS support (OpenSSL 3.0)
- ✅ Native recording (FFmpeg muxing)

## Building FFmpeg

See [scripts/README.md](./scripts/README.md) for detailed instructions on building FFmpeg for iOS and Android.

Quick build commands:
```bash
# iOS (requires Xcode)
./scripts/build-ios.sh

# Android (requires Android NDK)
./scripts/build-android.sh
```

## Next Steps / Roadmap

### Short Term
- [ ] Fix video aspect ratio (currently displays as square)
- [ ] Improve texture scaling and fit modes
- [ ] Add configuration options for decoder selection
- [ ] Enhanced error handling and reconnection logic

### Medium Term
- [ ] iOS recording support (FFmpeg-based like Android)
- [ ] HEVC/H.265 hardware decoding optimization
- [ ] Multiple audio track support
- [ ] Subtitle/caption support
- [ ] Adaptive bitrate streaming (HLS variants)

### Long Term
- [ ] Live streaming with ultra-low latency
- [ ] 4K/8K video support optimization
- [ ] Hardware encoding for recording
- [ ] WebRTC integration
- [ ] Cross-platform desktop support (macOS/Windows/Linux)

## Known Issues

1. **Video Aspect Ratio**: Video currently renders as square instead of preserving HD aspect ratio - fix in progress
2. **Texture Scaling**: Default fit mode may need adjustment for different video dimensions

## Join Ding Talk Group 加入钉钉群

<div>
  <table>
    <thead><tr>
      <th>加入钉钉群</th>
      <th>微信赞赏码</th>
      <th>支付宝</th>
    </tr></thead>
    <tbody><tr>
      <td>
        <img width="200" height="200" src="https://cdn.jsdelivr.net/gh/befovy/fijkplayer@master/docs/images/dingtalk.jpg" alt="加入钉钉群" />
      </td>
      <td>
        <img width="200" height="200" src="https://cdn.jsdelivr.net/gh/befovy/images@master/assets/wechat-qr-code.jpeg" alt="微信赞赏码" />
      </td>
      <td>
        <img width="200" height="200" src="https://cdn.jsdelivr.net/gh/befovy/images@master/assets/alipay-qr-code.jpeg" alt="支付宝二维码" />
      </td>
    </tr></tbody>
  </table>
</div>
