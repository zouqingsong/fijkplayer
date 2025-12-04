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

  # Use vendored FFmpeg frameworks for native player
  s.vendored_frameworks = 'Frameworks/libavcodec.framework', 'Frameworks/libavformat.framework', 'Frameworks/libavutil.framework'

  # BIJKPlayer dependency REMOVED
  # Now using native FFmpeg-based player exclusively (NativePlayer/)
  # Legacy ijkplayer support discontinued as of v0.11.0

  s.ios.deployment_target = '9.0'
  
  # FFmpeg header search paths for native player
  s.xcconfig = { 
    'HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include" "$(PODS_TARGET_SRCROOT)/Frameworks/libavcodec.framework/Headers" "$(PODS_TARGET_SRCROOT)/Frameworks/libavformat.framework/Headers" "$(PODS_TARGET_SRCROOT)/Frameworks/libavutil.framework/Headers"',
    'USER_HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/FFmpeg/include"',
    'CLANG_ALLOW_NON_MODULAR_INCLUDES_IN_FRAMEWORK_MODULES' => 'YES'
  }
  
  # System frameworks needed for native player
  s.frameworks = 'VideoToolbox', 'CoreVideo', 'CoreMedia', 'CoreFoundation', 'AudioToolbox', 'AVFoundation', 'Accelerate'
end

