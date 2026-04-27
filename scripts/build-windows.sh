#!/bin/bash

# FFmpeg build script for Windows (cross-compile from macOS/Linux using mingw-w64)
# Or download pre-built shared libraries from https://github.com/BtbN/FFmpeg-Builds
#
# For cross-compilation:
#   brew install mingw-w64  (on macOS)
#   apt-get install mingw-w64  (on Linux)
#
# For pre-built libraries (RECOMMENDED):
#   Download from: https://github.com/BtbN/FFmpeg-Builds/releases
#   Choose: ffmpeg-n6.1-latest-win64-lgpl-shared-6.1.tar.xz
#   Extract and copy to fijkplayer/windows/FFmpeg/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
FFMPEG_DIR="${PROJECT_ROOT}/windows/FFmpeg"

echo "=========================================="
echo "FFmpeg Windows Setup"
echo "=========================================="

# Option 1: Download pre-built (recommended)
download_prebuilt() {
    local FFMPEG_VERSION="6.1"
    local URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n${FFMPEG_VERSION}-latest-win64-lgpl-shared-${FFMPEG_VERSION}.tar.xz"
    local BUILD_DIR="${PROJECT_ROOT}/ffmpeg/build"
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    echo "Downloading pre-built FFmpeg for Windows..."
    curl -L -o "ffmpeg-win64.tar.xz" "$URL"
    
    echo "Extracting..."
    tar xf "ffmpeg-win64.tar.xz"
    
    # Find extracted directory
    local EXTRACTED=$(ls -d ffmpeg-n* 2>/dev/null | head -1)
    if [ -z "$EXTRACTED" ]; then
        echo "Error: Could not find extracted FFmpeg directory"
        exit 1
    fi
    
    # Copy to plugin directory
    mkdir -p "$FFMPEG_DIR/bin" "$FFMPEG_DIR/lib" "$FFMPEG_DIR/include"
    
    cp "$EXTRACTED/bin/"*.dll "$FFMPEG_DIR/bin/"
    cp "$EXTRACTED/lib/"*.lib "$FFMPEG_DIR/lib/" 2>/dev/null || \
    cp "$EXTRACTED/lib/"*.dll.a "$FFMPEG_DIR/lib/" 2>/dev/null || true
    cp -r "$EXTRACTED/include/"* "$FFMPEG_DIR/include/"
    
    echo "FFmpeg copied to: $FFMPEG_DIR"
    echo "DLLs:"
    ls -lh "$FFMPEG_DIR/bin/"*.dll
}

# Option 2: Cross-compile with mingw-w64
cross_compile() {
    echo "Cross-compilation not yet implemented."
    echo "Please use the pre-built option: $0 --prebuilt"
    exit 1
}

case "${1:-}" in
    --prebuilt|-p)
        download_prebuilt
        ;;
    --cross|-c)
        cross_compile
        ;;
    *)
        echo "Usage: $0 [--prebuilt | --cross]"
        echo ""
        echo "  --prebuilt (-p)  Download pre-built FFmpeg for Windows (recommended)"
        echo "  --cross (-c)     Cross-compile FFmpeg with mingw-w64"
        echo ""
        echo "Or manually download FFmpeg shared libraries and place in:"
        echo "  ${FFMPEG_DIR}/bin/   (DLL files)"
        echo "  ${FFMPEG_DIR}/lib/   (LIB/import files)"
        echo "  ${FFMPEG_DIR}/include/ (header files)"
        ;;
esac
