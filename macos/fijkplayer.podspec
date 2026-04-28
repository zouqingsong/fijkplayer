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

  # Source files: macOS Classes + shared sources copied from ios/ by script phase
  s.source_files = 'Classes/**/*', 'SharedSources/NativePlayer/FJKNativePlayer.{h,m}', 'SharedSources/NativePlayer/FJKPixelBufferRenderer.{h,m}', 'SharedSources/NativePlayer/FJKAudioDecoder.{h,m}', 'SharedSources/NativePlayer/FJKAudioQueue.{h,c}', 'SharedSources/NativePlayer/FJKAudioRenderer.{h,m}', 'SharedSources/NativePlayer/ffmpeg_demuxer.{h,c}', 'SharedSources/NativePlayer/async_io_protocol.{h,c}', 'SharedSources/Classes/FFmpegRecorder.{h,m}', 'SharedSources/Classes/FijkQueuingEventSink.{h,m}', 'SharedSources/Classes/FijkHostOption.{h,m}', 'SharedSources/FFmpegKit/**/*.{h,c}'
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
    'HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include" "$(PODS_TARGET_SRCROOT)/SharedSources/NativePlayer" "$(PODS_TARGET_SRCROOT)/SharedSources/Classes" "$(PODS_TARGET_SRCROOT)/SharedSources/FFmpegKit"',
    'USER_HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include" "$(PODS_TARGET_SRCROOT)/SharedSources/NativePlayer" "$(PODS_TARGET_SRCROOT)/SharedSources/Classes" "$(PODS_TARGET_SRCROOT)/SharedSources/FFmpegKit"',
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES',
    'GCC_WARN_INHIBIT_ALL_WARNINGS' => 'YES'
  }
  
  # User target xcconfig - propagates to the main app
  s.user_target_xcconfig = {
    'LIBRARY_SEARCH_PATHS' => '$(inherited) "$(PODS_ROOT)/../Flutter/ephemeral/.symlinks/plugins/fijkplayer/macos/FFmpeg/lib"',
    'OTHER_LDFLAGS' => '$(inherited) -lavcodec -lavformat -lavutil -lswscale -lswresample -lavfilter'
  }
  
  # System frameworks needed for native player
  s.frameworks = 'VideoToolbox', 'CoreVideo', 'CoreMedia', 'CoreFoundation', 'AudioToolbox', 'AVFoundation', 'Accelerate', 'Security'
  
  # Script phases: 1) keep shared sources in sync with ios/ before compile, 2) copy FFmpeg dylibs after compile
  s.script_phases = [
    {
      :name => 'Sync Shared Sources from iOS',
      :script => 'set -e; SRC="${PODS_TARGET_SRCROOT}/../ios"; DEST="${PODS_TARGET_SRCROOT}/SharedSources"; mkdir -p "${DEST}/NativePlayer" "${DEST}/Classes" "${DEST}/FFmpegKit"; for f in FJKNativePlayer FJKPixelBufferRenderer FJKAudioDecoder FJKAudioRenderer; do cp "${SRC}/NativePlayer/${f}.h" "${SRC}/NativePlayer/${f}.m" "${DEST}/NativePlayer/"; done; cp "${SRC}/NativePlayer/FJKAudioQueue.h" "${SRC}/NativePlayer/FJKAudioQueue.c" "${DEST}/NativePlayer/"; cp "${SRC}/NativePlayer/ffmpeg_demuxer.h" "${SRC}/NativePlayer/ffmpeg_demuxer.c" "${DEST}/NativePlayer/"; cp "${SRC}/NativePlayer/async_io_protocol.h" "${SRC}/NativePlayer/async_io_protocol.c" "${DEST}/NativePlayer/"; for f in FFmpegRecorder FijkQueuingEventSink FijkHostOption; do cp "${SRC}/Classes/${f}.h" "${SRC}/Classes/${f}.m" "${DEST}/Classes/"; done; cp -R "${SRC}/FFmpegKit/"* "${DEST}/FFmpegKit/"',
      :execution_position => :before_compile
    },
    {
      :name => 'Copy FFmpeg Libraries',
      :script => 'set -e; LIB_DIR="${PODS_TARGET_SRCROOT}/FFmpeg/lib"; mkdir -p "${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}"; for lib in libavcodec.dylib libavformat.dylib libavutil.dylib libswscale.dylib libswresample.dylib libavfilter.dylib; do if [ -f "${LIB_DIR}/${lib}" ]; then cp "${LIB_DIR}/${lib}" "${TARGET_BUILD_DIR}/${FRAMEWORKS_FOLDER_PATH}/"; fi; done',
      :execution_position => :after_compile
    }
  ]
end
