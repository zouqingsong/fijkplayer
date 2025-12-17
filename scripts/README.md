# FFmpeg Build Scripts

This directory contains the build scripts for compiling FFmpeg with HTTPS support for both Android and iOS platforms.

## Scripts

### `build-android.sh`
Builds FFmpeg with OpenSSL 3.0.12 for Android platforms (arm64-v8a and armeabi-v7a).

**Features:**
- OpenSSL 3.0.12 integration for HTTPS support
- Hardware acceleration via MediaCodec
- Optimized for streaming (RTSP, HTTP, HTTPS)
- Automatic library copying to Android project

**Usage:**
```bash
cd scripts
./build-android.sh
```

**Requirements:**
- Android NDK (set `ANDROID_NDK_ROOT` or `ANDROID_HOME`)
- API Level 28+ (Android 9.0)

### `build-ios.sh`
Builds FFmpeg with SecureTransport for iOS platforms (arm64, x86_64 simulator).

**Features:**
- SecureTransport integration for HTTPS support
- iOS 12.0+ compatibility
- Universal framework generation
- Automatic library copying to iOS project

**Usage:**
```bash
cd scripts
./build-ios.sh
```

**Requirements:**
- Xcode with command line tools
- iOS 12.0+ deployment target

## Output

Both scripts create:
- Compiled FFmpeg libraries in `../ffmpeg/output/`
- Automatically copy libraries to their respective platform directories
- Android: `../android/src/main/jniLibs/`
- iOS: `../ios/FFmpeg/` or `../ios/Frameworks/`

## HTTPS Support

- **Android**: Uses OpenSSL 3.0.12 for TLS/SSL connections
- **iOS**: Uses Apple's SecureTransport for native SSL support

Both implementations provide secure HTTPS video streaming capabilities.