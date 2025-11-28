#!/bin/bash

# FFmpeg build script for iOS with SecureTransport (HTTPS support)
# Builds minimal FFmpeg for arm64 (device) and x86_64 (simulator)
# For fijkplayer - RTSP/HTTP/HTTPS streaming with hardware decoding

set -e

# Configuration
FFMPEG_VERSION="6.1"
FFMPEG_SOURCE="ffmpeg-${FFMPEG_VERSION}"
FFMPEG_TARBALL="${FFMPEG_SOURCE}.tar.xz"
FFMPEG_URL="https://ffmpeg.org/releases/${FFMPEG_TARBALL}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT_DIR="${PROJECT_ROOT}/ffmpeg/output"
BUILD_DIR="${PROJECT_ROOT}/ffmpeg/build"
IOS_MIN_VERSION="12.0"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Detect Xcode
XCODE_PATH=$(xcode-select -p)
if [ ! -d "$XCODE_PATH" ]; then
    echo -e "${RED}Error: Xcode not found. Please install Xcode and run 'sudo xcode-select --switch /Applications/Xcode.app'${NC}"
    exit 1
fi

echo -e "${GREEN}Using Xcode at: ${XCODE_PATH}${NC}"

# Platform paths
PLATFORM_PATH_DEVICE="${XCODE_PATH}/Platforms/iPhoneOS.platform/Developer"
PLATFORM_PATH_SIMULATOR="${XCODE_PATH}/Platforms/iPhoneSimulator.platform/Developer"
SDK_PATH_DEVICE="${PLATFORM_PATH_DEVICE}/SDKs/iPhoneOS.sdk"
SDK_PATH_SIMULATOR="${PLATFORM_PATH_SIMULATOR}/SDKs/iPhoneSimulator.sdk"

# Verify SDK paths
if [ ! -d "$SDK_PATH_DEVICE" ]; then
    echo -e "${RED}Error: iOS SDK not found at ${SDK_PATH_DEVICE}${NC}"
    exit 1
fi

if [ ! -d "$SDK_PATH_SIMULATOR" ]; then
    echo -e "${YELLOW}Warning: iOS Simulator SDK not found at ${SDK_PATH_SIMULATOR}${NC}"
    echo -e "${YELLOW}Continuing with device-only build...${NC}"
fi

# Download FFmpeg source
download_ffmpeg() {
    # Ensure build directory exists
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    if [ -d "$FFMPEG_SOURCE" ]; then
        echo -e "${YELLOW}FFmpeg source already exists, skipping download${NC}"
        return
    fi

    echo -e "${GREEN}Downloading FFmpeg ${FFMPEG_VERSION}...${NC}"
    curl -L -o "$FFMPEG_TARBALL" "$FFMPEG_URL"
    
    echo -e "${GREEN}Extracting...${NC}"
    tar xf "$FFMPEG_TARBALL"
    
    echo -e "${GREEN}Download complete${NC}"
}

# Common configure flags
get_common_flags() {
    echo "\
        --disable-programs \
        --disable-doc \
        --disable-htmlpages \
        --disable-manpages \
        --disable-podpages \
        --disable-txtpages \
        --disable-static \
        --enable-shared \
        --enable-small \
        --disable-debug \
        --disable-avdevice \
        --disable-postproc \
        --disable-avfilter \
        --disable-swscale \
        --disable-swresample \
        --enable-protocol=file,rtsp,rtp,tcp,udp,http,https,tls,crypto \
        --enable-securetransport \
        --enable-demuxer=rtsp,sdp,rtp,h264,hevc,aac,mov,mp4,flv,mpegts \
        --enable-parser=h264,hevc,aac,aac_latm \
        --enable-decoder=h264,hevc,aac \
        --disable-encoders \
        --disable-muxers \
        --disable-filters \
        --disable-indevs \
        --disable-outdevs \
        --disable-hwaccels \
        --disable-vulkan \
        --disable-v4l2-m2m \
        --disable-vaapi \
        --disable-vdpau \
        --disable-videotoolbox \
        --disable-audiotoolbox \
        --disable-appkit \
        --disable-coreimage \
        --disable-metal"
}

# Build for specific architecture
build_arch() {
    local ARCH=$1
    local SDK_PATH=$2
    local PLATFORM_NAME=$3
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Building FFmpeg for iOS ${ARCH}${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    local BUILD_PATH="${BUILD_DIR}/ios-${ARCH}"
    local INSTALL_PATH="${OUTPUT_DIR}/ios-${ARCH}"
    
    # Clean previous build
    rm -rf "$BUILD_PATH"
    mkdir -p "$BUILD_PATH"
    
    cd "${BUILD_DIR}/${FFMPEG_SOURCE}"
    
    # Set compiler flags - use different version flags for device vs simulator
    local TARGET_OS="darwin"
    if [ "$PLATFORM_NAME" = "simulator" ]; then
        local CFLAGS="-arch ${ARCH} -mios-simulator-version-min=${IOS_MIN_VERSION} -fembed-bitcode -isysroot ${SDK_PATH}"
        local LDFLAGS="-arch ${ARCH} -mios-simulator-version-min=${IOS_MIN_VERSION} -fembed-bitcode -isysroot ${SDK_PATH}"
    else
        local CFLAGS="-arch ${ARCH} -mios-version-min=${IOS_MIN_VERSION} -fembed-bitcode -isysroot ${SDK_PATH}"
        local LDFLAGS="-arch ${ARCH} -mios-version-min=${IOS_MIN_VERSION} -fembed-bitcode -isysroot ${SDK_PATH}"
    fi
    
    # Configure
    echo -e "${GREEN}Configuring FFmpeg...${NC}"
    ./configure \
        $(get_common_flags) \
        --prefix="$INSTALL_PATH" \
        --enable-cross-compile \
        --target-os="$TARGET_OS" \
        --arch="$ARCH" \
        --cc="clang" \
        --cxx="clang++" \
        --as="gas-preprocessor.pl -arch $ARCH -- clang" \
        --extra-cflags="$CFLAGS" \
        --extra-ldflags="$LDFLAGS" \
        --nm="nm" \
        --ar="ar" \
        --ranlib="ranlib" \
        --strip="strip"
    
    # Build
    echo -e "${GREEN}Building...${NC}"
    make clean
    make -j$(sysctl -n hw.ncpu)
    
    # Install
    echo -e "${GREEN}Installing to ${INSTALL_PATH}...${NC}"
    make install
    
    cd ..
    
    echo -e "${GREEN}FFmpeg built successfully for ${ARCH}${NC}"
    echo -e "${GREEN}Libraries:${NC}"
    ls -lh "${INSTALL_PATH}/lib"
    echo ""
}

# Build for arm64 (device)
build_arm64() {
    build_arch "arm64" "$SDK_PATH_DEVICE" "device"
}

# Build for x86_64 (simulator)
build_x86_64() {
    if [ ! -d "$SDK_PATH_SIMULATOR" ]; then
        echo -e "${YELLOW}Skipping x86_64 simulator build (SDK not found)${NC}"
        return
    fi
    build_arch "x86_64" "$SDK_PATH_SIMULATOR" "simulator"
}

# Build for arm64 (simulator for Apple Silicon Macs)
build_arm64_simulator() {
    if [ ! -d "$SDK_PATH_SIMULATOR" ]; then
        echo -e "${YELLOW}Skipping arm64 simulator build (SDK not found)${NC}"
        return
    fi
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Building FFmpeg for iOS arm64 simulator${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    local BUILD_PATH="${BUILD_DIR}/ios-arm64-simulator"
    local INSTALL_PATH="${OUTPUT_DIR}/ios-arm64-simulator"
    
    # Clean previous build
    rm -rf "$BUILD_PATH"
    mkdir -p "$BUILD_PATH"
    
    cd "${BUILD_DIR}/${FFMPEG_SOURCE}"
    
    # Set compiler flags for simulator
    local CFLAGS="-arch arm64 -mios-simulator-version-min=${IOS_MIN_VERSION} -fembed-bitcode -isysroot ${SDK_PATH_SIMULATOR}"
    local LDFLAGS="-arch arm64 -mios-simulator-version-min=${IOS_MIN_VERSION} -fembed-bitcode -isysroot ${SDK_PATH_SIMULATOR}"
    
    # Configure
    echo -e "${GREEN}Configuring FFmpeg...${NC}"
    ./configure \
        $(get_common_flags) \
        --prefix="$INSTALL_PATH" \
        --enable-cross-compile \
        --target-os="darwin" \
        --arch="arm64" \
        --cc="clang" \
        --cxx="clang++" \
        --as="gas-preprocessor.pl -arch arm64 -- clang" \
        --extra-cflags="$CFLAGS" \
        --extra-ldflags="$LDFLAGS" \
        --nm="nm" \
        --ar="ar" \
        --ranlib="ranlib" \
        --strip="strip"
    
    # Build
    echo -e "${GREEN}Building...${NC}"
    make clean
    make -j$(sysctl -n hw.ncpu)
    
    # Install
    echo -e "${GREEN}Installing to ${INSTALL_PATH}...${NC}"
    make install
    
    cd ..
    
    echo -e "${GREEN}FFmpeg built successfully for arm64 simulator${NC}"
    echo -e "${GREEN}Libraries:${NC}"
    ls -lh "${INSTALL_PATH}/lib"
    echo ""
}

# Create universal framework
create_framework() {
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Creating FFmpeg framework${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    local FRAMEWORK_DIR="${OUTPUT_DIR}/FFmpeg.framework"
    local HEADERS_DIR="${FRAMEWORK_DIR}/Headers"
    
    rm -rf "$FRAMEWORK_DIR"
    mkdir -p "$HEADERS_DIR"
    
    # Copy headers from arm64 build
    cp -r "${OUTPUT_DIR}/ios-arm64/include/"* "$HEADERS_DIR/"
    
    # Create universal binary using lipo
    local ARM64_LIB="${OUTPUT_DIR}/ios-arm64/lib"
    local X86_64_LIB="${OUTPUT_DIR}/ios-x86_64/lib"
    
    if [ -d "$X86_64_LIB" ]; then
        echo -e "${GREEN}Creating universal binaries...${NC}"
        for lib in libavcodec libavformat libavutil; do
            lipo -create \
                "${ARM64_LIB}/${lib}.dylib" \
                "${X86_64_LIB}/${lib}.dylib" \
                -output "${FRAMEWORK_DIR}/${lib}.dylib"
        done
    else
        echo -e "${YELLOW}Simulator build not available, using arm64 only${NC}"
        for lib in libavcodec libavformat libavutil; do
            cp "${ARM64_LIB}/${lib}.dylib" "${FRAMEWORK_DIR}/"
        done
    fi
    
    # Create Info.plist
    cat > "${FRAMEWORK_DIR}/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>FFmpeg</string>
    <key>CFBundleIdentifier</key>
    <string>org.ffmpeg.FFmpeg</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>FFmpeg</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleShortVersionString</key>
    <string>${FFMPEG_VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${FFMPEG_VERSION}</string>
    <key>MinimumOSVersion</key>
    <string>${IOS_MIN_VERSION}</string>
</dict>
</plist>
EOF
    
    echo -e "${GREEN}Framework created at: ${FRAMEWORK_DIR}${NC}"
}

# Copy to fijkplayer
copy_to_fijkplayer() {
    local FIJKPLAYER_IOS="${PROJECT_ROOT}/ios/FFmpeg"
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Copying to fijkplayer iOS module${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    # Create iOS FFmpeg directory structure
    mkdir -p "$FIJKPLAYER_IOS/lib"
    mkdir -p "$FIJKPLAYER_IOS/include"
    
    # Copy libraries (separate by architecture for manual linking)
    echo -e "${GREEN}Copying arm64 (device) libraries...${NC}"
    mkdir -p "${FIJKPLAYER_IOS}/lib/arm64"
    cp "${OUTPUT_DIR}/ios-arm64/lib/"*.dylib "${FIJKPLAYER_IOS}/lib/arm64/"
    
    if [ -d "${OUTPUT_DIR}/ios-arm64-simulator" ]; then
        echo -e "${GREEN}Copying arm64 (simulator) libraries...${NC}"
        mkdir -p "${FIJKPLAYER_IOS}/lib/arm64-simulator"
        cp "${OUTPUT_DIR}/ios-arm64-simulator/lib/"*.dylib "${FIJKPLAYER_IOS}/lib/arm64-simulator/"
    fi
    
    if [ -d "${OUTPUT_DIR}/ios-x86_64" ]; then
        echo -e "${GREEN}Copying x86_64 (simulator) libraries...${NC}"
        mkdir -p "${FIJKPLAYER_IOS}/lib/x86_64"
        cp "${OUTPUT_DIR}/ios-x86_64/lib/"*.dylib "${FIJKPLAYER_IOS}/lib/x86_64/"
    fi
    
    # Copy headers
    echo -e "${GREEN}Copying headers...${NC}"
    cp -r "${OUTPUT_DIR}/ios-arm64/include/"* "${FIJKPLAYER_IOS}/include/"
    
    echo -e "${GREEN}Files copied to: ${FIJKPLAYER_IOS}${NC}"
    echo -e "${GREEN}Libraries:${NC}"
    ls -lh "${FIJKPLAYER_IOS}/lib/arm64"
    
    echo -e "${GREEN}Headers:${NC}"
    ls "${FIJKPLAYER_IOS}/include"
}

# Main execution
main() {
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}FFmpeg iOS Build Script${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo -e "FFmpeg Version: ${FFMPEG_VERSION}"
    echo -e "iOS Min Version: ${IOS_MIN_VERSION}"
    echo -e "Output Directory: ${OUTPUT_DIR}"
    echo ""
    
    # Download FFmpeg
    download_ffmpeg
    
    # Build architectures
    echo ""
    read -p "Build arm64 (device)? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        build_arm64
    fi
    
    if [ -d "$SDK_PATH_SIMULATOR" ]; then
        echo ""
        read -p "Build arm64 (Apple Silicon simulator)? (y/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            build_arm64_simulator
        fi
        
        echo ""
        read -p "Build x86_64 (Intel simulator)? (y/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            build_x86_64
        fi
    fi
    
    # Create framework (optional)
    echo ""
    read -p "Create FFmpeg.framework? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        create_framework
    fi
    
    # Copy to fijkplayer
    echo ""
    read -p "Copy libraries to fijkplayer module? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        copy_to_fijkplayer
    fi
    
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Build Complete!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo -e "Output: ${OUTPUT_DIR}"
    echo ""
    echo -e "${GREEN}Next steps:${NC}"
    echo -e "1. Update fijkplayer iOS podspec to link against these libraries"
    echo -e "2. Implement FFmpeg demuxer wrapper in Objective-C/C++"
    echo -e "3. Test RTSP stream opening and packet extraction"
}

# Run main
main
