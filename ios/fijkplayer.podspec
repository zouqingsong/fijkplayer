#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html
#
Pod::Spec.new do |s|
  s.name             = 'fijkplayer'
  s.version          = '0.11.0'
  s.summary          = 'Flutter media player plugin with native FFmpeg support'
  s.description      = <<-DESC
Flutter media player plugin with native FFmpeg-based player implementation.
Supports RTSP, HTTP, HLS streaming with hardware-accelerated video decoding.
                       DESC
  s.homepage         = 'https://github.com/zouqingsong/fijkplayer'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'zouqingsong' => 'zouqingsong@gmail.com' }
  s.source           = { :path => '.' }
  s.source_files = 'Classes/**/*', 'NativePlayer/**/*.{h,m,c}', 'FFmpegKit/**/*.{h,c}'
  s.public_header_files = 'Classes/FijkPlugin.h'

  s.static_framework = true
  
  # Ensure C files are compiled with proper flags
  s.compiler_flags = '-DHAVE_PTHREADS'

  # Legacy ijkplayer support removed - now using native FFmpeg-based player
  # See NativePlayer/ directory for implementation
  
  s.libraries = "bz2", "z", "stdc++", "c++"
  s.dependency 'Flutter'

  # BIJKPlayer dependency REMOVED
  # Now using native FFmpeg-based player exclusively (NativePlayer/)
  # Legacy ijkplayer support discontinued as of v0.11.0

  s.ios.deployment_target = '9.0'
  
  # Preserve binaries - don't strip symbols
  s.preserve_paths = 'FFmpeg/lib/**/*', 'FFmpeg/include/**/*'
  
  # Configure for both device and simulator
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386 x86_64',
    'LIBRARY_SEARCH_PATHS[sdk=iphoneos*]' => '$(inherited) "$(PODS_TARGET_SRCROOT)/FFmpeg/lib/arm64"',
    'LIBRARY_SEARCH_PATHS[sdk=iphonesimulator*]' => '$(inherited) "$(PODS_TARGET_SRCROOT)/FFmpeg/lib/arm64-simulator"',
    'OTHER_LDFLAGS' => '$(inherited) -lavcodec -lavformat -lavutil -lswscale -lswresample -lavfilter'
  }
  
  # User target xcconfig - propagates to the main app
  s.user_target_xcconfig = {
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386 x86_64',
    'LIBRARY_SEARCH_PATHS[sdk=iphoneos*]' => '$(inherited) "$(PODS_ROOT)/../.symlinks/plugins/fijkplayer/ios/FFmpeg/lib/arm64"',
    'LIBRARY_SEARCH_PATHS[sdk=iphonesimulator*]' => '$(inherited) "$(PODS_ROOT)/../.symlinks/plugins/fijkplayer/ios/FFmpeg/lib/arm64-simulator"',
    'OTHER_LDFLAGS' => '$(inherited) -lavcodec -lavformat -lavutil -lswscale -lswresample -lavfilter'
  }
  
  # FFmpeg header search paths
  s.xcconfig = { 
    'HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include" "$(PODS_TARGET_SRCROOT)/FFmpegKit"',
    'USER_HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include" "$(PODS_TARGET_SRCROOT)/FFmpegKit"',
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES'
  }
  
  # System frameworks needed for native player
  s.frameworks = 'VideoToolbox', 'CoreVideo', 'CoreMedia', 'CoreFoundation', 'AudioToolbox', 'AVFoundation', 'Accelerate'

  # Script phase to copy FFmpeg dylibs and codesign them
  # Release/Profile: wraps in .framework bundles (App Store requirement)
  # Debug: copies bare .dylib files (works with simulator)
  s.script_phases = [
    {
      :name => 'Copy FFmpeg Libraries',
      :script => 'set -e
if [ "${PLATFORM_NAME}" = "iphoneos" ]; then
  LIB_DIR="${PODS_TARGET_SRCROOT}/FFmpeg/lib/arm64"
else
  LIB_DIR="${PODS_TARGET_SRCROOT}/FFmpeg/lib/arm64-simulator"
fi
DEST="${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}"
mkdir -p "$DEST"
FFMPEG_LIBS="libavcodec libavfilter libavformat libavutil libswresample libswscale"

if [ "${CONFIGURATION}" = "Release" ] || [ "${CONFIGURATION}" = "Profile" ]; then
  # Release/Profile: wrap dylibs in .framework bundles (required for App Store)
  for libbase in $FFMPEG_LIBS; do
    dylib="${libbase}.dylib"
    fw_dir="${DEST}/${libbase}.framework"
    if [ -f "${LIB_DIR}/${dylib}" ]; then
      rm -f "${DEST}/${dylib}"
      mkdir -p "$fw_dir"
      cp "${LIB_DIR}/${dylib}" "${fw_dir}/${libbase}"
      install_name_tool -id "@rpath/${libbase}.framework/${libbase}" "${fw_dir}/${libbase}" 2>/dev/null || true
      cat > "${fw_dir}/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>${libbase}</string>
  <key>CFBundleIdentifier</key>
  <string>com.ffmpeg.${libbase}</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>${libbase}</string>
  <key>CFBundlePackageType</key>
  <string>FMWK</string>
  <key>CFBundleVersion</key>
  <string>1.0</string>
  <key>CFBundleShortVersionString</key>
  <string>1.0</string>
  <key>MinimumOSVersion</key>
  <string>15.0</string>
</dict>
</plist>
PLIST
    fi
  done

  # Fix cross-references between FFmpeg framework libraries
  for libbase in $FFMPEG_LIBS; do
    fw_binary="${DEST}/${libbase}.framework/${libbase}"
    if [ -f "$fw_binary" ]; then
      for ref_lib in $FFMPEG_LIBS; do
        if [ "$ref_lib" != "$libbase" ]; then
          install_name_tool -change "@rpath/${ref_lib}.dylib" "@rpath/${ref_lib}.framework/${ref_lib}" "$fw_binary" 2>/dev/null || true
        fi
      done
    fi
  done

  # Fix the main app binary to reference framework paths
  RUNNER_BINARY="${BUILT_PRODUCTS_DIR}/${EXECUTABLE_PATH}"
  if [ -f "$RUNNER_BINARY" ]; then
    for libbase in $FFMPEG_LIBS; do
      install_name_tool -change "@rpath/${libbase}.dylib" "@rpath/${libbase}.framework/${libbase}" "$RUNNER_BINARY" 2>/dev/null || true
    done
  fi

  # Code-sign each framework bundle
  if [ "${CODE_SIGNING_REQUIRED}" = "YES" ] && [ -n "${EXPANDED_CODE_SIGN_IDENTITY}" ]; then
    for libbase in $FFMPEG_LIBS; do
      fw_dir="${DEST}/${libbase}.framework"
      if [ -d "$fw_dir" ]; then
        codesign --force --sign "${EXPANDED_CODE_SIGN_IDENTITY}" "$fw_dir"
      fi
    done
  fi
else
  # Debug: copy bare dylibs (original behavior, works with simulator)
  for libbase in $FFMPEG_LIBS; do
    dylib="${libbase}.dylib"
    if [ -f "${LIB_DIR}/${dylib}" ]; then
      rm -rf "${DEST}/${libbase}.framework"
      cp "${LIB_DIR}/${dylib}" "${DEST}/${dylib}"
      install_name_tool -id "@rpath/${dylib}" "${DEST}/${dylib}" 2>/dev/null || true
    fi
  done
  # Code-sign bare dylibs
  if [ "${CODE_SIGNING_REQUIRED}" = "YES" ] && [ -n "${EXPANDED_CODE_SIGN_IDENTITY}" ]; then
    for libbase in $FFMPEG_LIBS; do
      dylib="${DEST}/${libbase}.dylib"
      if [ -f "$dylib" ]; then
        codesign --force --sign "${EXPANDED_CODE_SIGN_IDENTITY}" "$dylib"
      fi
    done
  fi
fi',
      :execution_position => :after_compile
    }
  ]
end

