#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html
#
Pod::Spec.new do |s|
  s.name             = 'fijkplayer'
  s.version          = '0.11.0'
  s.summary          = 'Flutter plugin for ijkplayer'
  s.description      = <<-DESC
Flutter plugin for ijkplayer
                       DESC
  s.homepage         = 'http://github.com/befovy/fijkplayer'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'befovy' => 'befovy@gmail.com' }
  s.source           = { :path => '.' }
  s.source_files = 'Classes/**/*', 'NativePlayer/**/*'
  s.public_header_files = 'Classes/**/*.h', 'NativePlayer/**/*.h'

  s.static_framework = true

  # @ uncomment next 3 lines to debug or use your custom ijkplayer build
  # 去除下面 3 行代码开头的注释 #，以便于进行调试或者使用自定义构建的 ijkplayer 产物
  # s.preserve_paths = 'Frameworks/*.framework'
  # s.vendored_frameworks = 'Frameworks/IJKMediaPlayer.framework'
  # s.xcconfig = { 'LD_RUNPATH_SEARCH_PATHS' => '"$(PODS_ROOT)/Frameworks/"' }

  s.libraries = "bz2", "z", "stdc++", "c++"
  s.dependency 'Flutter'

  # Use vendored FFmpeg frameworks for native player
  s.vendored_frameworks = 'Frameworks/libavcodec.framework', 'Frameworks/libavformat.framework', 'Frameworks/libavutil.framework', 'Frameworks/libswscale.framework', 'Frameworks/libswresample.framework'

  # BIJKPlayer dependency (OPTIONAL)
  # Comment out to use ONLY the new FFmpeg-based native player
  # Uncomment if you need the legacy FijkPlayer class (befovy.com/fijk channel)
  # Note: Adds ~15MB to app size
  # s.dependency 'BIJKPlayer', '~> 0.7.16'

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

