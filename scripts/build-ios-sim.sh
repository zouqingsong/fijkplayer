#!/bin/bash
set -e

FFMPEG_SRC="/Users/Qing-song.Zou/Documents/GitHub/fijkplayer/ffmpeg/build/ffmpeg-6.1"
XCODE_PATH=$(xcode-select -p)
SDK_PATH="${XCODE_PATH}/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk"
INSTALL_PATH="/Users/Qing-song.Zou/Documents/GitHub/fijkplayer/ffmpeg/output/ios-arm64-simulator"
FIJKPLAYER_IOS="/Users/Qing-song.Zou/Documents/GitHub/fijkplayer/ios/FFmpeg"

CFLAGS="-arch arm64 -mios-simulator-version-min=12.0 -fembed-bitcode -isysroot ${SDK_PATH}"
LDFLAGS="-arch arm64 -mios-simulator-version-min=12.0 -fembed-bitcode -isysroot ${SDK_PATH}"

cd "$FFMPEG_SRC"

echo "=== Configuring FFmpeg for arm64 iOS Simulator ==="
./configure \
    --disable-programs --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages \
    --disable-static --enable-shared --enable-small --disable-debug \
    --disable-avdevice --disable-postproc \
    --enable-avfilter --enable-swscale --enable-swresample \
    --enable-securetransport \
    --enable-protocol=file,rtsp,rtp,tcp,udp,http,https,tls,crypto,pipe,concat \
    --enable-demuxer=rtsp,sdp,rtp,h264,hevc,aac,mov,mp4,flv,mpegts,concat,pcm_s16le,wav \
    --enable-parser=h264,hevc,aac,aac_latm \
    --enable-decoder=h264,hevc,aac,pcm_s16le \
    --enable-encoder=aac,h264_videotoolbox,hevc_videotoolbox,pcm_s16le \
    --enable-muxer=mp4,mov,mpegts,flv,wav,adts \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc \
    --enable-filter=concat,scale,null,anull,aresample,format,aformat \
    --disable-filter=scale_vt \
    --enable-videotoolbox \
    --disable-indevs --disable-outdevs --disable-hwaccels --disable-vulkan \
    --disable-v4l2-m2m --disable-vaapi --disable-vdpau \
    --disable-audiotoolbox --disable-appkit --disable-coreimage --disable-metal \
    --prefix="$INSTALL_PATH" \
    --enable-cross-compile --target-os=darwin --arch=arm64 \
    --cc=clang --cxx=clang++ \
    --extra-cflags="$CFLAGS" --extra-ldflags="$LDFLAGS" \
    --nm=nm --ar=ar --ranlib=ranlib --strip=strip

echo "=== Building ==="
make clean
make -j$(sysctl -n hw.ncpu)

echo "=== Installing ==="
make install

echo "=== arm64-simulator build complete ==="
ls -lh "$INSTALL_PATH/lib/"*.dylib

echo "=== Copying to fijkplayer iOS ==="
# Create directory structure
mkdir -p "$FIJKPLAYER_IOS/lib/arm64" "$FIJKPLAYER_IOS/lib/arm64-simulator" "$FIJKPLAYER_IOS/include"

# Copy arm64 device libs
ARM64_DIR="/Users/Qing-song.Zou/Documents/GitHub/fijkplayer/ffmpeg/output/ios-arm64"
if [ -d "$ARM64_DIR/lib" ]; then
    cp "$ARM64_DIR/lib/"*.dylib "$FIJKPLAYER_IOS/lib/arm64/"
    echo "Copied arm64 device libraries"
fi

# Copy arm64-simulator libs
cp "$INSTALL_PATH/lib/"*.dylib "$FIJKPLAYER_IOS/lib/arm64-simulator/"
echo "Copied arm64-simulator libraries"

# Copy headers (from device build)
cp -r "$ARM64_DIR/include/"* "$FIJKPLAYER_IOS/include/"
echo "Copied headers"

# Copy config.h for fftools
cp "$FFMPEG_SRC/config.h" "$FIJKPLAYER_IOS/include/"
echo "Copied config.h"

# Also need compat/ headers
mkdir -p "$FIJKPLAYER_IOS/include/compat"
if [ -d "$FFMPEG_SRC/compat" ]; then
    cp "$FFMPEG_SRC/compat/"*.h "$FIJKPLAYER_IOS/include/compat/" 2>/dev/null || true
    echo "Copied compat headers"
fi

# Need libavdevice headers (included unconditionally by fftools)
mkdir -p "$FIJKPLAYER_IOS/include/libavdevice"
if [ -d "$FFMPEG_SRC/libavdevice" ]; then
    cp "$FFMPEG_SRC/libavdevice/avdevice.h" "$FIJKPLAYER_IOS/include/libavdevice/" 2>/dev/null || true
    cp "$FFMPEG_SRC/libavdevice/version.h" "$FIJKPLAYER_IOS/include/libavdevice/" 2>/dev/null || true
    cp "$FFMPEG_SRC/libavdevice/version_major.h" "$FIJKPLAYER_IOS/include/libavdevice/" 2>/dev/null || true
    echo "Copied libavdevice headers"
fi

echo "=== Done ==="
ls -R "$FIJKPLAYER_IOS/include/" | head -30
echo "---"
ls "$FIJKPLAYER_IOS/lib/"
