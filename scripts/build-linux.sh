#!/bin/bash

# FFmpeg build/install script for Linux
# On Linux, FFmpeg can be installed via system package manager
# or built from source for more control.

set -e

echo "=========================================="
echo "FFmpeg Linux Setup"
echo "=========================================="
echo ""

# Option 1: System package manager (recommended for development)
install_system_ffmpeg() {
    echo "Installing FFmpeg development libraries..."

    if command -v apt-get &>/dev/null; then
        echo "Using apt-get..."
        sudo apt-get update
        sudo apt-get install -y \
            libavformat-dev \
            libavcodec-dev \
            libavutil-dev \
            libswscale-dev \
            libswresample-dev \
            libavfilter-dev \
            pkg-config
    elif command -v dnf &>/dev/null; then
        echo "Using dnf..."
        sudo dnf install -y \
            ffmpeg-devel \
            pkg-config
    elif command -v pacman &>/dev/null; then
        echo "Using pacman..."
        sudo pacman -S --needed \
            ffmpeg \
            pkg-config
    else
        echo "Error: No supported package manager found."
        echo "Please install FFmpeg development libraries manually."
        exit 1
    fi

    echo ""
    echo "Verifying installation..."
    pkg-config --modversion libavformat libavcodec libavutil libswscale libswresample libavfilter
    echo "FFmpeg development libraries installed successfully."
}

# Option 2: Build from source
build_from_source() {
    local FFMPEG_VERSION="6.1"
    local BUILD_DIR="/tmp/ffmpeg-build"
    local PREFIX="/usr/local"

    echo "Building FFmpeg ${FFMPEG_VERSION} from source..."

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    if [ ! -d "ffmpeg-${FFMPEG_VERSION}" ]; then
        echo "Downloading FFmpeg ${FFMPEG_VERSION}..."
        curl -L -o "ffmpeg-${FFMPEG_VERSION}.tar.xz" \
            "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
        tar xf "ffmpeg-${FFMPEG_VERSION}.tar.xz"
    fi

    cd "ffmpeg-${FFMPEG_VERSION}"

    echo "Configuring..."
    ./configure \
        --prefix="$PREFIX" \
        --enable-shared \
        --disable-static \
        --disable-programs \
        --disable-doc \
        --enable-swscale \
        --enable-swresample \
        --enable-avfilter \
        --enable-protocol=file,http,https,tcp,udp,rtsp,rtp \
        --enable-demuxer=mov,mp4,avi,flv,mpegts,hls,rtsp,rtp,sdp,concat \
        --enable-muxer=mov,mp4,mpegts,flv,null \
        --enable-decoder=h264,hevc,mpeg4,aac,mp3,pcm_s16le \
        --enable-encoder=aac \
        --enable-parser=h264,hevc,mpeg4video,aac \
        --enable-filter=aresample,scale,format,concat,anull \
        --enable-pthreads \
        --disable-debug

    echo "Building..."
    make -j$(nproc)

    echo "Installing..."
    sudo make install
    sudo ldconfig

    echo "FFmpeg ${FFMPEG_VERSION} installed to ${PREFIX}"
}

case "${1:-}" in
    --system|-s)
        install_system_ffmpeg
        ;;
    --source|-b)
        build_from_source
        ;;
    *)
        echo "Usage: $0 [--system | --source]"
        echo ""
        echo "  --system (-s)  Install FFmpeg from system package manager (recommended)"
        echo "  --source (-b)  Build FFmpeg 6.1 from source"
        echo ""
        echo "The plugin uses pkg-config to find FFmpeg libraries at build time."
        ;;
esac
