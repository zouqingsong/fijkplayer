#!/bin/bash

# FFmpeg build script for macOS
# Builds FFmpeg for arm64 (Apple Silicon) and x86_64 (Intel)
# For fijkplayer - RTSP/HTTP/HTTPS streaming with hardware decoding + encoding

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
MACOS_MIN_VERSION="10.14"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}FFmpeg macOS Build Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "FFmpeg Version: ${FFMPEG_VERSION}"
echo -e "macOS Min Version: ${MACOS_MIN_VERSION}"
echo -e "Output Directory: ${OUTPUT_DIR}"
echo ""

# Download FFmpeg source
download_ffmpeg() {
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
        --enable-avfilter \
        --enable-swscale \
        --enable-swresample \
        --enable-protocol=file,rtsp,rtp,tcp,udp,http,https,tls,crypto,pipe,concat \
        --enable-securetransport \
        --enable-demuxer=rtsp,sdp,rtp,h264,hevc,aac,mov,mp4,flv,mpegts,concat,pcm_s16le,wav \
        --enable-parser=h264,hevc,aac,aac_latm \
        --enable-decoder=h264,hevc,aac,pcm_s16le \
        --enable-encoder=aac,h264_videotoolbox,hevc_videotoolbox,pcm_s16le \
        --enable-muxer=mp4,mov,mpegts,flv,wav,adts \
        --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc \
        --enable-filter=concat,scale,null,anull,aresample,format,aformat \
        --enable-videotoolbox \
        --disable-indevs \
        --disable-outdevs \
        --disable-vulkan \
        --disable-v4l2-m2m \
        --disable-vaapi \
        --disable-vdpau \
        --disable-audiotoolbox \
        --disable-coreimage \
        --disable-metal"
}

# Build for specific architecture
build_arch() {
    local ARCH=$1
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Building FFmpeg for macOS ${ARCH}${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    local INSTALL_PATH="${OUTPUT_DIR}/macos-${ARCH}"
    
    # Clean previous build
    rm -rf "$INSTALL_PATH"
    mkdir -p "$INSTALL_PATH"
    
    cd "${BUILD_DIR}/${FFMPEG_SOURCE}"
    
    # Set compiler flags
    local CFLAGS="-arch ${ARCH} -mmacosx-version-min=${MACOS_MIN_VERSION}"
    local LDFLAGS="-arch ${ARCH} -mmacosx-version-min=${MACOS_MIN_VERSION}"
    
    # Configure
    echo -e "${GREEN}Configuring FFmpeg for macOS ${ARCH}...${NC}"
    make clean 2>/dev/null || true
    ./configure \
        $(get_common_flags) \
        --prefix="$INSTALL_PATH" \
        --target-os=darwin \
        --arch="$ARCH" \
        --cc="clang" \
        --cxx="clang++" \
        --extra-cflags="$CFLAGS" \
        --extra-ldflags="$LDFLAGS" \
        --nm="nm" \
        --ar="ar" \
        --ranlib="ranlib" \
        --strip="strip"
    
    # Build
    echo -e "${GREEN}Building...${NC}"
    make -j$(sysctl -n hw.ncpu)
    
    # Install
    echo -e "${GREEN}Installing to ${INSTALL_PATH}...${NC}"
    make install
    
    echo -e "${GREEN}FFmpeg built successfully for macOS ${ARCH}${NC}"
    echo -e "${GREEN}Libraries:${NC}"
    ls -lh "${INSTALL_PATH}/lib/"*.dylib
    echo ""
}

# Create universal (fat) binaries
create_universal() {
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Creating universal macOS binaries${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    local ARM64_LIB="${OUTPUT_DIR}/macos-arm64/lib"
    local X86_64_LIB="${OUTPUT_DIR}/macos-x86_64/lib"
    local UNIVERSAL_DIR="${OUTPUT_DIR}/macos-universal/lib"
    local HEADERS_DIR="${OUTPUT_DIR}/macos-universal/include"
    
    mkdir -p "$UNIVERSAL_DIR"
    mkdir -p "$HEADERS_DIR"
    
    # Copy headers from arm64 build
    cp -r "${OUTPUT_DIR}/macos-arm64/include/"* "$HEADERS_DIR/"
    
    if [ -d "$ARM64_LIB" ] && [ -d "$X86_64_LIB" ]; then
        echo -e "${GREEN}Creating universal binaries with lipo...${NC}"
        for lib in libavcodec libavformat libavutil libswscale libswresample libavfilter; do
            # Find the actual dylib (may have version suffix)
            ARM64_DYLIB=$(find "$ARM64_LIB" -name "${lib}*.dylib" -not -type l | head -1)
            X86_64_DYLIB=$(find "$X86_64_LIB" -name "${lib}*.dylib" -not -type l | head -1)
            
            if [ -f "$ARM64_DYLIB" ] && [ -f "$X86_64_DYLIB" ]; then
                lipo -create "$ARM64_DYLIB" "$X86_64_DYLIB" -output "${UNIVERSAL_DIR}/${lib}.dylib"
                echo -e "${GREEN}  Created universal ${lib}.dylib${NC}"
            fi
        done
    elif [ -d "$ARM64_LIB" ]; then
        echo -e "${YELLOW}Only arm64 available, copying...${NC}"
        for lib in libavcodec libavformat libavutil libswscale libswresample libavfilter; do
            DYLIB=$(find "$ARM64_LIB" -name "${lib}*.dylib" -not -type l | head -1)
            if [ -f "$DYLIB" ]; then
                cp "$DYLIB" "${UNIVERSAL_DIR}/${lib}.dylib"
            fi
        done
    fi
    
    echo -e "${GREEN}Universal libraries:${NC}"
    ls -lh "${UNIVERSAL_DIR}/"*.dylib 2>/dev/null || echo "No libraries found"
}

# Copy to fijkplayer macOS module
copy_to_fijkplayer() {
    local FIJKPLAYER_MACOS="${PROJECT_ROOT}/macos/FFmpeg"
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Copying to fijkplayer macOS module${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    mkdir -p "$FIJKPLAYER_MACOS/lib"
    mkdir -p "$FIJKPLAYER_MACOS/include"
    
    # Use universal if available, else arm64
    local LIB_SOURCE="${OUTPUT_DIR}/macos-universal/lib"
    local HEADER_SOURCE="${OUTPUT_DIR}/macos-universal/include"
    
    if [ ! -d "$LIB_SOURCE" ]; then
        LIB_SOURCE="${OUTPUT_DIR}/macos-arm64/lib"
        HEADER_SOURCE="${OUTPUT_DIR}/macos-arm64/include"
    fi
    
    # Copy libraries
    echo -e "${GREEN}Copying libraries...${NC}"
    for lib in libavcodec libavformat libavutil libswscale libswresample libavfilter; do
        DYLIB=$(find "$LIB_SOURCE" -name "${lib}*.dylib" -not -type l | head -1)
        if [ -f "$DYLIB" ]; then
            cp "$DYLIB" "${FIJKPLAYER_MACOS}/lib/${lib}.dylib"
            echo "  Copied ${lib}.dylib"
        fi
    done
    
    # Copy headers
    echo -e "${GREEN}Copying headers...${NC}"
    cp -r "${HEADER_SOURCE}/"* "${FIJKPLAYER_MACOS}/include/"
    
    # Fix dylib install names for bundling
    echo -e "${GREEN}Fixing dylib install names...${NC}"
    for lib in libavcodec libavformat libavutil libswscale libswresample libavfilter; do
        DYLIB="${FIJKPLAYER_MACOS}/lib/${lib}.dylib"
        if [ -f "$DYLIB" ]; then
            install_name_tool -id "@rpath/${lib}.dylib" "$DYLIB" 2>/dev/null || true
        fi
    done
    
    echo -e "${GREEN}Files copied to: ${FIJKPLAYER_MACOS}${NC}"
    echo -e "${GREEN}Libraries:${NC}"
    ls -lh "${FIJKPLAYER_MACOS}/lib/"
}

# Main execution
main() {
    # Download FFmpeg
    download_ffmpeg
    
    # Detect architecture
    CURRENT_ARCH=$(uname -m)
    echo -e "${GREEN}Current architecture: ${CURRENT_ARCH}${NC}"
    echo ""
    
    # Build arm64 (Apple Silicon)
    read -p "Build arm64 (Apple Silicon)? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        build_arch "arm64"
    fi
    
    # Build x86_64 (Intel)
    read -p "Build x86_64 (Intel)? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        build_arch "x86_64"
    fi
    
    # Create universal binary
    if [ -d "${OUTPUT_DIR}/macos-arm64" ] && [ -d "${OUTPUT_DIR}/macos-x86_64" ]; then
        read -p "Create universal binary? (y/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            create_universal
        fi
    fi
    
    # Copy to fijkplayer
    read -p "Copy libraries to fijkplayer macOS module? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        copy_to_fijkplayer
    fi
    
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Build Complete!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo -e "Output: ${OUTPUT_DIR}"
}

main
