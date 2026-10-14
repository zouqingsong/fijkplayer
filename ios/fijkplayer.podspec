#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html
#
Pod::Spec.new do |s|
  s.name             = 'fijkplayer'
  s.version          = '0.11.3'
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
  
  s.libraries = "bz2", "z", "iconv", "stdc++", "c++"
  s.dependency 'Flutter'

  # BIJKPlayer dependency REMOVED
  # Now using native FFmpeg-based player exclusively (NativePlayer/)
  # Legacy ijkplayer support discontinued as of v0.11.0

  s.ios.deployment_target = '12.0'
  
  # Preserve binaries - don't strip symbols
  s.preserve_paths = 'FFmpeg/lib/**/*', 'FFmpeg/include/**/*'
  
  # Configure for both device and simulator
  #
  # Link the SHARED FFmpeg builds under arm64 / arm64-simulator. Do NOT add the
  # arm64-static / simulator-static archives to the search path: the iOS build
  # scripts configure FFmpeg with --disable-static, so those .a files are
  # leftovers from an older build. They are found first, they predate the
  # current FFmpeg, and they were built without the mp4 muxer - which is what
  # silently broke RTSP and file recording on iOS (the recorder asks for the
  # "mp4" muxer by name, so the output file's extension cannot work around it).
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

  # NOTE: FFmpeg dylibs are copied by the app's Podfile (Runner target context),
  # not here, because pod target script phases can't access the Runner app's Frameworks folder.
end

