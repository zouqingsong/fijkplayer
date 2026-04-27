#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html
#
Pod::Spec.new do |s|
  s.name             = 'fijkplayer'
  s.version          = '0.11.0'
  s.summary          = 'Flutter media player plugin with native FFmpeg support for macOS'
  s.description      = <<-DESC
Flutter media player plugin with native FFmpeg-based player implementation.
Supports RTSP, HTTP, HLS streaming with hardware-accelerated video decoding on macOS.
                       DESC
  s.homepage         = 'https://github.com/zouqingsong/fijkplayer'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'zouqingsong' => 'zouqingsong@gmail.com' }
  s.source           = { :path => '.' }

  # Source files: macOS Classes + shared NativePlayer from iOS (exclude iOS-specific handler)
  s.source_files = 'Classes/**/*', '../ios/NativePlayer/FJKNativePlayer.{h,m}', '../ios/NativePlayer/FJKPixelBufferRenderer.{h,m}', '../ios/NativePlayer/FJKAudioDecoder.{h,m}', '../ios/NativePlayer/FJKAudioQueue.{h,c}', '../ios/NativePlayer/FJKAudioRenderer.{h,m}', '../ios/NativePlayer/ffmpeg_demuxer.{h,c}', '../ios/NativePlayer/async_io_protocol.{h,c}', '../ios/Classes/FFmpegRecorder.{h,m}', '../ios/Classes/FijkQueuingEventSink.{h,m}', '../ios/Classes/FijkHostOption.{h,m}', '../ios/FFmpegKit/**/*.{h,c}'
  s.public_header_files = 'Classes/**/*.h'

  s.static_framework = true
  
  # Ensure C files are compiled with proper flags
  s.compiler_flags = '-DHAVE_PTHREADS -DTARGET_OS_MAC=1'

  s.libraries = "bz2", "z", "stdc++", "c++"
  s.dependency 'FlutterMacOS'

  s.osx.deployment_target = '10.14'
  
  # Preserve FFmpeg binaries
  s.preserve_paths = 'FFmpeg/lib/**/*', 'FFmpeg/include/**/*'
  
  # Configure library and header search paths
  s.pod_target_xcconfig = {
    'LIBRARY_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/lib"',
    'OTHER_LDFLAGS' => '-lavcodec -lavformat -lavutil -lswscale -lswresample -lavfilter',
    'HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include" "$(PODS_TARGET_SRCROOT)/../ios/NativePlayer" "$(PODS_TARGET_SRCROOT)/../ios/Classes" "$(PODS_TARGET_SRCROOT)/../ios/FFmpegKit"',
    'USER_HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include" "$(PODS_TARGET_SRCROOT)/../ios/NativePlayer" "$(PODS_TARGET_SRCROOT)/../ios/Classes" "$(PODS_TARGET_SRCROOT)/../ios/FFmpegKit"',
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES'
  }
  
  # User target xcconfig - propagates to the main app
  s.user_target_xcconfig = {
    'LIBRARY_SEARCH_PATHS' => '"$(PODS_ROOT)/../.symlinks/plugins/fijkplayer/macos/FFmpeg/lib"',
    'OTHER_LDFLAGS' => '-lavcodec -lavformat -lavutil -lswscale -lswresample -lavfilter'
  }
  
  # System frameworks needed for native player
  s.frameworks = 'VideoToolbox', 'CoreVideo', 'CoreMedia', 'CoreFoundation', 'AudioToolbox', 'AVFoundation', 'Accelerate', 'Security'
  
  # Script phase to copy FFmpeg dylibs to app bundle
  s.script_phases = [
    {
      :name => 'Copy FFmpeg Libraries',
      :script => 'set -e; LIB_DIR="${PODS_TARGET_SRCROOT}/FFmpeg/lib"; mkdir -p "${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}"; for lib in libavcodec.dylib libavformat.dylib libavutil.dylib libswscale.dylib libswresample.dylib libavfilter.dylib; do if [ -f "${LIB_DIR}/${lib}" ]; then cp "${LIB_DIR}/${lib}" "${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}/"; fi; done',
      :execution_position => :after_compile
    }
  ]
end
