#!/bin/bash
# FFmpeg Build Script for Android
# This script compiles FFmpeg with minimal configuration for video streaming playback
# Target: arm64-v8a and armeabi-v7a
# Purpose: RTSP streaming with hardware-accelerated decoding via MediaCodec

set -e  # Exit on error

# Configuration
FFMPEG_VERSION="6.1"
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
OPENSSL_VERSION="3.0.12"
OPENSSL_URL="https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/ffmpeg/build"
SOURCE_DIR="${BUILD_DIR}/ffmpeg-${FFMPEG_VERSION}"
OPENSSL_SOURCE_DIR="${BUILD_DIR}/openssl-${OPENSSL_VERSION}"
OUTPUT_DIR="${PROJECT_ROOT}/ffmpeg/output"

# Android NDK Configuration
if [ -n "$ANDROID_NDK_ROOT" ] && [ -d "$ANDROID_NDK_ROOT" ]; then
    NDK_ROOT="$ANDROID_NDK_ROOT"
elif [ -n "$ANDROID_HOME" ] && [ -d "$ANDROID_HOME/ndk" ]; then
    # Find latest NDK version
    NDK_VERSION=$(ls "$ANDROID_HOME/ndk" | grep -E '^[0-9]+\.' | sort -V | tail -n1)
    if [ -z "$NDK_VERSION" ]; then
        echo "Error: No NDK found in $ANDROID_HOME/ndk"
        exit 1
    fi
    NDK_ROOT="$ANDROID_HOME/ndk/$NDK_VERSION"
else
    echo "Error: Android NDK not found. Set ANDROID_NDK_ROOT or ANDROID_HOME"
    exit 1
fi

echo "Using NDK: $NDK_ROOT"

# API Level (minimum Android version)  
API_LEVEL=28  # Android 9.0 (required for OpenSSL getentropy)

# Toolchain
TOOLCHAIN="${NDK_ROOT}/toolchains/llvm/prebuilt/darwin-x86_64"
if [ ! -d "$TOOLCHAIN" ]; then
    # Try linux
    TOOLCHAIN="${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64"
    if [ ! -d "$TOOLCHAIN" ]; then
        echo "Error: Toolchain not found in $NDK_ROOT"
        exit 1
    fi
fi

# Download FFmpeg source
download_ffmpeg() {
    echo "Downloading FFmpeg ${FFMPEG_VERSION}..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    if [ ! -f "ffmpeg-${FFMPEG_VERSION}.tar.xz" ]; then
        curl -L -o "ffmpeg-${FFMPEG_VERSION}.tar.xz" "$FFMPEG_URL"
    fi
    
    if [ ! -d "$SOURCE_DIR" ]; then
        echo "Extracting FFmpeg source..."
        tar -xf "ffmpeg-${FFMPEG_VERSION}.tar.xz"
    fi
    
    echo "FFmpeg source ready at: $SOURCE_DIR"
}

# Download OpenSSL source
download_openssl() {
    echo "Downloading OpenSSL ${OPENSSL_VERSION}..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    if [ ! -f "openssl-${OPENSSL_VERSION}.tar.gz" ]; then
        curl -L -o "openssl-${OPENSSL_VERSION}.tar.gz" "$OPENSSL_URL"
    fi
    
    if [ ! -d "$OPENSSL_SOURCE_DIR" ]; then
        echo "Extracting OpenSSL source..."
        tar -xzf "openssl-${OPENSSL_VERSION}.tar.gz"
    fi
    
    echo "OpenSSL source ready at: $OPENSSL_SOURCE_DIR"
}

# Build OpenSSL for specific architecture
build_openssl_arch() {
    local ARCH=$1
    local OPENSSL_ARCH=$2
    local CROSS_PREFIX=$3
    
    echo ""
    echo "=========================================="
    echo "Building OpenSSL for $ARCH"
    echo "=========================================="
    
    local ARCH_OUTPUT="${OUTPUT_DIR}/${ARCH}"
    mkdir -p "$ARCH_OUTPUT"
    
    cd "$OPENSSL_SOURCE_DIR"
    
    # Clean previous build
    make clean 2>/dev/null || true
    
    # Configure OpenSSL 3.x
    export ANDROID_NDK_ROOT="$NDK_ROOT"
    export PATH="${TOOLCHAIN}/bin:$PATH"
    
    ./Configure ${OPENSSL_ARCH} \
        -D__ANDROID_API__=${API_LEVEL} \
        --prefix="$ARCH_OUTPUT" \
        --openssldir="$ARCH_OUTPUT/ssl" \
        no-shared \
        no-tests \
        no-ui-console \
        no-asm
    
    echo "Compiling OpenSSL..."
    make -j$(nproc)
    
    echo "Installing OpenSSL to $ARCH_OUTPUT..."
    make install_sw
    
    echo "OpenSSL built successfully for $ARCH"
}

# Build for specific architecture
build_arch() {
    local ARCH=$1
    local CPU=$2
    local CROSS_PREFIX=$3
    local ARCH_CFLAGS=$4
    
    echo ""
    echo "=========================================="
    echo "Building FFmpeg for $ARCH"
    echo "=========================================="
    
    local ARCH_OUTPUT="${OUTPUT_DIR}/${ARCH}"
    mkdir -p "$ARCH_OUTPUT"
    
    cd "$SOURCE_DIR"
    
    # Clean previous build
    make clean 2>/dev/null || true
    
    # Configure flags for minimal build
    # We only need: demuxing (RTSP, file), parsing (H.264, AAC), minimal decoding
    # Native decoders (MediaCodec) will be used instead of FFmpeg decoders
    
    ./configure \
        --prefix="$ARCH_OUTPUT" \
        --enable-cross-compile \
        --target-os=android \
        --arch="$ARCH" \
        --cpu="$CPU" \
        --cross-prefix="${TOOLCHAIN}/bin/llvm-" \
        --cc="${TOOLCHAIN}/bin/${CROSS_PREFIX}${API_LEVEL}-clang" \
        --cxx="${TOOLCHAIN}/bin/${CROSS_PREFIX}${API_LEVEL}-clang++" \
        --nm="${TOOLCHAIN}/bin/llvm-nm" \
        --ar="${TOOLCHAIN}/bin/llvm-ar" \
        --ranlib="${TOOLCHAIN}/bin/llvm-ranlib" \
        --strip="${TOOLCHAIN}/bin/llvm-strip" \
        --extra-cflags="-O3 -fPIC ${ARCH_CFLAGS} -I${ARCH_OUTPUT}/include" \
        --extra-ldflags="-L${ARCH_OUTPUT}/lib -lssl -lcrypto -lm" \
        --enable-shared \
        --disable-static \
        --disable-symver \
        --disable-doc \
        --disable-htmlpages \
        --disable-manpages \
        --disable-podpages \
        --disable-txtpages \
        --disable-programs \
        --disable-ffmpeg \
        --disable-ffplay \
        --disable-ffprobe \
        --disable-avdevice \
        --disable-postproc \
        --disable-swscale \
        --disable-swresample \
        --disable-avfilter \
        --enable-protocol=file,rtsp,rtp,tcp,udp,http,https,tls,crypto \
        --enable-demuxer=rtsp,sdp,rtp,h264,hevc,aac,mov,mp4,flv,mpegts \
        --enable-parser=h264,hevc,aac,aac_latm \
        --enable-decoder=h264,aac \
        --disable-encoders \
        --disable-muxers \
        --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb \
        --enable-network \
        --enable-openssl \
        --enable-small \
        --enable-optimizations \
        --disable-debug \
        --disable-vulkan \
        --disable-v4l2-m2m \
        --disable-vaapi \
        --disable-vdpau
    
    echo "Compiling FFmpeg..."
    make -j$(nproc)
    
    echo "Installing to $ARCH_OUTPUT..."
    make install
    
    echo "FFmpeg built successfully for $ARCH"
    echo "Libraries:"
    ls -lh "$ARCH_OUTPUT/lib/"
}

# Build for arm64-v8a (64-bit ARM)
build_arm64() {
    build_openssl_arch "arm64" "android-arm64" "aarch64-linux-android"
    build_arch "arm64" "armv8-a" "aarch64-linux-android" ""
}

# Build for armeabi-v7a (32-bit ARM)
build_armv7a() {
    build_openssl_arch "arm" "android-arm" "armv7a-linux-androideabi"
    build_arch "arm" "armv7-a" "armv7a-linux-androideabi" "-march=armv7-a -mfloat-abi=softfp -mfpu=neon"
}

# Copy to fijkplayer android module
copy_to_fijkplayer() {
    echo ""
    echo "Copying libraries to fijkplayer module..."
    
    local FIJKPLAYER_LIBS="${PROJECT_ROOT}/android/src/main/jniLibs"
    mkdir -p "$FIJKPLAYER_LIBS"
    
    # Copy arm64-v8a
    if [ -d "${OUTPUT_DIR}/arm64/lib" ]; then
        mkdir -p "${FIJKPLAYER_LIBS}/arm64-v8a"
        cp -v "${OUTPUT_DIR}/arm64/lib"/*.so "${FIJKPLAYER_LIBS}/arm64-v8a/"
        # Copy OpenSSL static libraries (they'll be linked into FFmpeg)
        if [ -f "${OUTPUT_DIR}/arm64/lib/libssl.a" ]; then
            echo "OpenSSL static libraries found for arm64-v8a"
        fi
        echo "Copied arm64-v8a libraries"
    fi
    
    # Copy armeabi-v7a
    if [ -d "${OUTPUT_DIR}/arm/lib" ]; then
        mkdir -p "${FIJKPLAYER_LIBS}/armeabi-v7a"
        cp -v "${OUTPUT_DIR}/arm/lib"/*.so "${FIJKPLAYER_LIBS}/armeabi-v7a/"
        # Copy OpenSSL static libraries (they'll be linked into FFmpeg)
        if [ -f "${OUTPUT_DIR}/arm/lib/libssl.a" ]; then
            echo "OpenSSL static libraries found for armeabi-v7a"
        fi
        echo "Copied armeabi-v7a libraries"
    fi
    
    # Copy headers
    local FIJKPLAYER_HEADERS="${SCRIPT_DIR}/../android/src/main/cpp/ffmpeg"
    mkdir -p "$FIJKPLAYER_HEADERS"
    
    if [ -d "${OUTPUT_DIR}/arm64/include" ]; then
        cp -r "${OUTPUT_DIR}/arm64/include"/* "$FIJKPLAYER_HEADERS/"
        echo "Copied FFmpeg headers"
    fi
    
    echo "FFmpeg libraries and headers copied to fijkplayer module"
    echo ""
    echo "Library sizes:"
    du -sh "${FIJKPLAYER_LIBS}"/*/*.so 2>/dev/null || true
}

# Generate pkg-config files
generate_pkgconfig() {
    echo ""
    echo "Generating pkg-config files..."
    
    for arch_dir in "${OUTPUT_DIR}"/*; do
        if [ -d "$arch_dir/lib/pkgconfig" ]; then
            echo "pkg-config files for $(basename $arch_dir):"
            ls "$arch_dir/lib/pkgconfig"
        fi
    done
}

# Main build process
main() {
    echo "=========================================="
    echo "FFmpeg Build Script for Android with OpenSSL"
    echo "=========================================="
    echo "FFmpeg Version: $FFMPEG_VERSION"
    echo "OpenSSL Version: $OPENSSL_VERSION"
    echo "Target API Level: $API_LEVEL"
    echo "Architectures: arm64-v8a, armeabi-v7a"
    echo "Build Directory: $BUILD_DIR"
    echo "Output Directory: $OUTPUT_DIR"
    echo ""
    
    # Download sources if needed
    download_ffmpeg
    download_openssl
    
    # Build for both architectures
    echo ""
    read -p "Build arm64-v8a? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        build_arm64
    fi
    
    echo ""
    read -p "Build armeabi-v7a? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        build_armv7a
    fi
    
    # Generate pkg-config
    generate_pkgconfig
    
    # Copy to fijkplayer module
    echo ""
    read -p "Copy libraries to fijkplayer module? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        copy_to_fijkplayer
    fi
    
    echo ""
    echo "=========================================="
    echo "Build Complete!"
    echo "=========================================="
    echo "Output: $OUTPUT_DIR"
    echo ""
    echo "Next steps:"
    echo "1. Update fijkplayer CMakeLists.txt to link against these libraries"
    echo "2. Implement FFmpeg demuxer wrapper in C/C++"
    echo "3. Test RTSP stream opening and packet extraction"
}

# Run main
main
