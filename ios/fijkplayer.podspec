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
  s.source_files = 'Classes/**/*', 'NativePlayer/**/*.{h,m,c}'
  s.public_header_files = 'Classes/**/*.h', 'NativePlayer/**/*.h'

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
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    'LIBRARY_SEARCH_PATHS[sdk=iphoneos*]' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/lib/arm64"',
    'LIBRARY_SEARCH_PATHS[sdk=iphonesimulator*]' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/lib/simulator"',
    'OTHER_LDFLAGS' => '-lavcodec -lavformat -lavutil'
  }
  
  # User target xcconfig - propagates to the main app
  s.user_target_xcconfig = {
    'LIBRARY_SEARCH_PATHS[sdk=iphoneos*]' => '"$(PODS_ROOT)/../.symlinks/plugins/fijkplayer/ios/FFmpeg/lib/arm64"',
    'LIBRARY_SEARCH_PATHS[sdk=iphonesimulator*]' => '"$(PODS_ROOT)/../.symlinks/plugins/fijkplayer/ios/FFmpeg/lib/simulator"',
    'OTHER_LDFLAGS' => '-lavcodec -lavformat -lavutil'
  }
  
  # FFmpeg header search paths
  s.xcconfig = { 
    'HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include"',
    'USER_HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include"',
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES'
  }
  
  # System frameworks needed for native player
  s.frameworks = 'VideoToolbox', 'CoreVideo', 'CoreMedia', 'CoreFoundation', 'AudioToolbox', 'AVFoundation', 'Accelerate'
  
  # Script phase to copy FFmpeg dylibs to app bundle
  s.script_phases = [
    {
      :name => 'Copy FFmpeg Libraries',
      :script => 'set -e; if [ "${PLATFORM_NAME}" = "iphoneos" ]; then LIB_DIR="${PODS_TARGET_SRCROOT}/FFmpeg/lib/arm64"; else LIB_DIR="${PODS_TARGET_SRCROOT}/FFmpeg/lib/simulator"; fi; mkdir -p "${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}"; for lib in libavcodec.dylib libavformat.dylib libavutil.dylib; do if [ -f "${LIB_DIR}/${lib}" ]; then cp "${LIB_DIR}/${lib}" "${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}/"; fi; done',
      :execution_position => :after_compile
    }
  ]
end

