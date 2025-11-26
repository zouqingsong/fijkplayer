# Clean FFmpeg-Only Implementation Plan

## Current State Analysis

### iOS
- **FijkPlayer.h/.m** - Wrapper around IJKMediaPlayer (from BIJKPlayer)
- **FJKNativePlayer** - Pure FFmpeg implementation (NEW, working)
- **Dependency:** BIJKPlayer CocoaPod (~15MB)

### Android  
- **NativePlayer** - Pure FFmpeg + MediaCodec (NEW, working)
- **Old:** FijkPlayer/FijkEngine (to be removed)

## Clean Solution: Unified Architecture

### Phase 1: Replace iOS FijkPlayer Backend ✅ NEXT

**Goal:** Make FijkPlayer use FJKNativePlayer instead of IJKMediaPlayer

**Changes:**
```objc
// Old (FijkPlayer.m)
@implementation FijkPlayer {
    IJKFFMediaPlayer *_ijkMediaPlayer;  // ❌ Remove
}

// New (FijkPlayer.m)  
@implementation FijkPlayer {
    FJKNativePlayer *_nativePlayer;     // ✅ Add
    FJKPixelBufferRenderer *_renderer;  // ✅ Add
}
```

**API Preservation:**
- Keep all existing FijkPlayer methods
- Map to FJKNativePlayer internally
- Flutter code unchanged

**Benefits:**
- ❌ Remove BIJKPlayer dependency
- ✅ Reduce app size by ~15MB
- ✅ Unified FFmpeg implementation
- ✅ Same API for existing apps

### Phase 2: Unify Android Architecture ✅ DONE

**Status:** Already complete
- Native player working
- Old FijkPlayer/FijkEngine removed
- Using befovy.com/fijk/native_player_test channel

### Phase 3: Merge Channels (Optional)

**Goal:** Single channel for both platforms

**Options:**

**Option A: Keep Separate Channels** (Recommended)
```dart
// Legacy apps
const channel = MethodChannel('befovy.com/fijk');

// New native player apps  
const channel = MethodChannel('befovy.com/fijk/native_player_test');
```

**Option B: Unified Channel**
```dart
// Single channel, backend auto-selected
const channel = MethodChannel('befovy.com/fijk');
// Uses FJKNativePlayer on both platforms
```

### Phase 4: Feature Parity

**Current:**
- ✅ Video playback (H.264/H.265)
- ✅ Hardware decoding (VideoToolbox/MediaCodec)
- ✅ RTSP/HTTP/File support
- ✅ Seeking
- ⏳ Audio playback
- ⏳ Speed control
- ⏳ Subtitles

## Implementation Steps

### Step 1: Create FijkPlayer Bridge (iOS)

File: `ios/Classes/FijkPlayerNative.{h,m}`

```objc
// Drop-in replacement for IJKMediaPlayer
@interface FijkPlayerNative : NSObject

// Mirror IJKFFMediaPlayer API
- (void)setDataSource:(NSString *)url;
- (void)prepareAsync;
- (int)start;
- (void)pause;
- (void)stop;
- (long)getCurrentPosition;
- (long)getDuration;
// ... etc

// Internal: uses FJKNativePlayer
@property (strong) FJKNativePlayer *nativePlayer;

@end
```

### Step 2: Update FijkPlayer.m

Replace IJKFFMediaPlayer calls:
```objc
// Before
_ijkMediaPlayer = [[IJKFFMediaPlayer alloc] init];
[_ijkMediaPlayer setDataSource:url];

// After
_nativePlayer = [[FJKNativePlayer alloc] init];
[_nativePlayer setDataSource:url];
```

### Step 3: Update Podspec

```ruby
# Remove BIJKPlayer
# s.dependency 'BIJKPlayer', '~> 0.7.16'

# Use only FFmpeg frameworks
s.vendored_frameworks = 'Frameworks/libav*.framework'
```

### Step 4: Test Migration

```bash
cd example/ios
pod install  # BIJKPlayer should NOT install
flutter build ios --no-codesign
```

## Migration Guide for Apps

### Zero-Change Migration

**Before:**
```dart
final player = FijkPlayer();
await player.setDataSource(url);
await player.prepareAsync();
await player.start();
```

**After:** 
```dart
// SAME CODE - backend changed internally
final player = FijkPlayer();
await player.setDataSource(url);
await player.prepareAsync();
await player.start();
```

### Binary Size Comparison

| Configuration | iOS Size | Android Size |
|---------------|----------|--------------|
| **Old (BIJKPlayer)** | ~25MB | ~18MB |
| **New (FFmpeg-only)** | ~10MB | ~12MB |
| **Savings** | **-15MB** | **-6MB** |

## Risk Mitigation

### Compatibility Matrix

| Feature | BIJKPlayer | FJKNativePlayer | Status |
|---------|-----------|-----------------|--------|
| H.264 Video | ✅ | ✅ | ✅ Ready |
| H.265 Video | ✅ | ✅ | ✅ Ready |
| AAC Audio | ✅ | ⏳ | ⚠️ Phase 4 |
| RTSP | ✅ | ✅ | ✅ Ready |
| HTTP/HLS | ✅ | ✅ | ✅ Ready |
| Seeking | ✅ | ✅ | ✅ Ready |
| Speed Control | ✅ | ⏳ | ⚠️ Phase 4 |
| Recording | ✅ | ❌ | ℹ️ Not planned |

### Rollback Plan

If issues arise:
```ruby
# Revert podspec
s.dependency 'BIJKPlayer', '~> 0.7.16'
```

Then:
```bash
cd example/ios && pod install
```

## Next Actions

### Immediate (Today)
1. ✅ Create FijkPlayerNative wrapper
2. ✅ Update FijkPlayer.m to use native backend
3. ✅ Test compilation
4. ✅ Remove BIJKPlayer dependency

### Short Term (This Week)
1. Add audio playback to FJKNativePlayer
2. Add speed control
3. Performance testing
4. Memory leak checks

### Long Term (Next Month)
1. Subtitle support
2. Multiple audio tracks
3. Live streaming optimizations
4. Production deployment

## Success Criteria

- ✅ iOS builds without BIJKPlayer
- ✅ Android uses only native player
- ✅ Example app runs on both platforms
- ✅ Video playback smooth (30+ fps)
- ✅ Memory stable (no leaks)
- ✅ Binary size reduced by >10MB
- ✅ Existing API unchanged
- ✅ All unit tests pass

## Timeline

- **Phase 1 (iOS Backend):** 2-3 hours
- **Phase 2 (Android):** ✅ Complete
- **Phase 3 (Channel Merge):** 1-2 hours (optional)
- **Phase 4 (Audio):** 1-2 days
- **Total:** ~1 week for feature-complete

**Current Status:** Ready to start Phase 1
