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
//

import 'package:fijkplayer/fijkplayer.dart';
import 'package:flutter_test/flutter_test.dart';

/// What actually crosses the platform channel when a playback mode is set.
///
/// The map is the whole contract with four native implementations, and two things
/// about it have already gone wrong once: a field sent as `null` arrives on the
/// Apple platforms as `NSNull` — an object, not a nil — so the native
/// `integerValue` it is sent throws and terminates the app; and the buffer was
/// sent under a name no Apple or Android backend read, so the setting silently
/// did nothing. These tests hold both.
void main() {
  group('FijkPlaybackConfig', () {
    test('never sends a null: absent fields are left out', () {
      final config = FijkPlaybackConfig(
        mode: FijkPlaybackMode.liveWithAudio,
        customBufferMs: 500,
      );

      final map = config.toMap();

      expect(map.containsKey('customBufferMs'), isTrue);
      expect(map.containsKey('enableAudio'), isFalse);
      expect(map.containsKey('maxLatencyMs'), isFalse);
      expect(map.containsKey('enableFrameDrop'), isFalse);
      expect(map.values.where((value) => value == null), isEmpty);
    });

    test('carries every field a caller did name', () {
      final map = FijkPlaybackConfig(
        mode: FijkPlaybackMode.liveLowLatency,
        customBufferMs: 150,
        maxLatencyMs: 150,
        enableAudio: false,
        enableFrameDrop: true,
      ).toMap();

      expect(map['mode'], FijkPlaybackMode.liveLowLatency.index);
      // The name the native backends read the buffer from.
      expect(map['customBufferMs'], 150);
      expect(map['maxLatencyMs'], 150);
      expect(map['enableAudio'], isFalse);
      expect(map['enableFrameDrop'], isTrue);
      expect(map.values.where((value) => value == null), isEmpty);
    });

    test('the factories send a complete map', () {
      for (final config in [
        FijkPlaybackConfig.liveLowLatency(),
        FijkPlaybackConfig.liveWithAudio(),
        FijkPlaybackConfig.vodOptimized(),
      ]) {
        final map = config.toMap();
        expect(map['mode'], isNotNull);
        expect(map['customBufferMs'], isNotNull);
        expect(map['enableAudio'], isNotNull);
        expect(map['enableFrameDrop'], isNotNull);
        expect(map.values.where((value) => value == null), isEmpty);
      }
    });
  });
}
