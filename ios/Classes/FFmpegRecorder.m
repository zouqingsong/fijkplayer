// MIT License
//
// Copyright (c) [2024] [Befovy]
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#import "FFmpegRecorder.h"
#import <libavformat/avformat.h>
#import <libavcodec/avcodec.h>
#import <libavutil/avutil.h>
#import <libavutil/time.h>
#import <libavutil/opt.h>

@interface FFmpegRecorder ()

@property (nonatomic, strong) dispatch_queue_t recordingQueue;
@property (atomic, assign) BOOL isRecording;
@property (nonatomic, strong) NSString *rtspUrl;
@property (nonatomic, strong) NSString *outputPath;

@property (atomic, assign) AVFormatContext *inputContext;
@property (atomic, assign) AVFormatContext *outputContext;
@property (atomic, assign) int videoStreamIndex;
@property (atomic, assign) BOOL stopRequested;

@end

@implementation FFmpegRecorder

+ (instancetype)sharedInstance {
    static FFmpegRecorder *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[FFmpegRecorder alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _recordingQueue = dispatch_queue_create("com.befovy.fijkplayer.ffmpeg_recorder", DISPATCH_QUEUE_SERIAL);
        _isRecording = NO;
        _inputContext = NULL;
        _outputContext = NULL;
        _videoStreamIndex = -1;
        _stopRequested = NO;
    }
    return self;
}

- (BOOL)startRecordingWithRtspUrl:(NSString *)rtspUrl 
                       outputPath:(NSString *)outputPath 
                            error:(NSError **)error {
    @synchronized (self) {
        if (_isRecording) {
            if (error) {
                *error = [NSError errorWithDomain:@"FFmpegRecorder" 
                                             code:-1 
                                         userInfo:@{NSLocalizedDescriptionKey: @"Recording already in progress"}];
            }
            return NO;
        }
        
        _rtspUrl = rtspUrl;
        _outputPath = outputPath;
        _stopRequested = NO;
        _isRecording = YES;
    }
    
    dispatch_async(_recordingQueue, ^{
        [self performRecording];
    });
    
    return YES;
}

- (void)performRecording {
    int ret;
    AVPacket pkt;
    int64_t startPts = -1;
    int64_t startDts = -1;
    BOOL gotKeyframe = NO;
    
    NSLog(@"[FFmpegRecorder] Starting recording from %@ to %@", _rtspUrl, _outputPath);
    
    // Open input (RTSP stream)
    AVDictionary *options = NULL;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "max_delay", "500000", 0);
    
    ret = avformat_open_input(&_inputContext, [_rtspUrl UTF8String], NULL, &options);
    av_dict_free(&options);
    
    if (ret < 0) {
        NSLog(@"[FFmpegRecorder] Failed to open input: %s", av_err2str(ret));
        [self cleanup];
        return;
    }
    
    // Find stream info
    ret = avformat_find_stream_info(_inputContext, NULL);
    if (ret < 0) {
        NSLog(@"[FFmpegRecorder] Failed to find stream info: %s", av_err2str(ret));
        [self cleanup];
        return;
    }
    
    // Find video stream
    _videoStreamIndex = -1;
    for (unsigned int i = 0; i < _inputContext->nb_streams; i++) {
        if (_inputContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            _videoStreamIndex = i;
            break;
        }
    }
    
    if (_videoStreamIndex == -1) {
        NSLog(@"[FFmpegRecorder] No video stream found");
        [self cleanup];
        return;
    }
    
    NSLog(@"[FFmpegRecorder] Found video stream at index %d, codec_id=%d", 
          _videoStreamIndex, _inputContext->streams[_videoStreamIndex]->codecpar->codec_id);
    
    // Create output context (MP4 file)
    ret = avformat_alloc_output_context2(&_outputContext, NULL, "mp4", [_outputPath UTF8String]);
    if (ret < 0) {
        NSLog(@"[FFmpegRecorder] Failed to create output context: %s", av_err2str(ret));
        [self cleanup];
        return;
    }
    
    // Create output video stream
    AVStream *inStream = _inputContext->streams[_videoStreamIndex];
    AVStream *outStream = avformat_new_stream(_outputContext, NULL);
    if (!outStream) {
        NSLog(@"[FFmpegRecorder] Failed to create output stream");
        [self cleanup];
        return;
    }
    
    // Copy codec parameters from input to output
    ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
    if (ret < 0) {
        NSLog(@"[FFmpegRecorder] Failed to copy codec parameters: %s", av_err2str(ret));
        [self cleanup];
        return;
    }
    
    outStream->codecpar->codec_tag = 0;
    
    // Open output file
    if (!(_outputContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&_outputContext->pb, [_outputPath UTF8String], AVIO_FLAG_WRITE);
        if (ret < 0) {
            NSLog(@"[FFmpegRecorder] Failed to open output file: %s", av_err2str(ret));
            [self cleanup];
            return;
        }
    }
    
    // Set faststart for proper MP4 playback
    AVDictionary *muxerOpts = NULL;
    av_dict_set(&muxerOpts, "movflags", "faststart", 0);
    
    // Write header
    ret = avformat_write_header(_outputContext, &muxerOpts);
    av_dict_free(&muxerOpts);
    if (ret < 0) {
        NSLog(@"[FFmpegRecorder] Failed to write header: %s", av_err2str(ret));
        [self cleanup];
        return;
    }
    
    NSLog(@"[FFmpegRecorder] Header written successfully, waiting for keyframe...");
    
    // Recording loop
    while (!_stopRequested) {
        ret = av_read_frame(_inputContext, &pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                NSLog(@"[FFmpegRecorder] End of stream reached");
            } else {
                NSLog(@"[FFmpegRecorder] Error reading frame: %s", av_err2str(ret));
            }
            break;
        }
        
        // Only process video packets
        if (pkt.stream_index == _videoStreamIndex) {
            // Wait for first keyframe before writing any packets
            if (!gotKeyframe) {
                if (pkt.flags & AV_PKT_FLAG_KEY) {
                    gotKeyframe = YES;
                    startPts = pkt.pts;
                    startDts = pkt.dts;
                    NSLog(@"[FFmpegRecorder] Got first keyframe, starting recording");
                } else {
                    av_packet_unref(&pkt);
                    continue;
                }
            }
            
            AVStream *inStr = _inputContext->streams[_videoStreamIndex];
            AVStream *outStr = _outputContext->streams[0];
            
            // Adjust timestamps relative to first keyframe
            pkt.pts = av_rescale_q_rnd(pkt.pts - startPts, inStr->time_base, 
                                       outStr->time_base, 
                                       AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            pkt.dts = av_rescale_q_rnd(pkt.dts - startDts, inStr->time_base, 
                                       outStr->time_base, 
                                       AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            if (pkt.pts < pkt.dts) {
                pkt.pts = pkt.dts;
            }
            pkt.duration = av_rescale_q(pkt.duration, inStr->time_base, outStr->time_base);
            pkt.stream_index = 0;
            pkt.pos = -1;
            
            // Write packet to output
            ret = av_interleaved_write_frame(_outputContext, &pkt);
            if (ret < 0) {
                NSLog(@"[FFmpegRecorder] Error writing frame: %s", av_err2str(ret));
                av_packet_unref(&pkt);
                break;
            }
        }
        
        av_packet_unref(&pkt);
    }
    
    NSLog(@"[FFmpegRecorder] Recording loop finished");
    [self cleanup];
}

- (void)cleanup {
    // Write trailer
    if (_outputContext) {
        av_write_trailer(_outputContext);
        
        // Close output file
        if (!(_outputContext->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&_outputContext->pb);
        }
        
        avformat_free_context(_outputContext);
        _outputContext = NULL;
    }
    
    // Close input
    if (_inputContext) {
        avformat_close_input(&_inputContext);
        _inputContext = NULL;
    }
    
    @synchronized (self) {
        _isRecording = NO;
        _videoStreamIndex = -1;
        _stopRequested = NO;
    }
    
    NSLog(@"[FFmpegRecorder] Cleanup completed");
}

- (BOOL)stopRecordingWithError:(NSError **)error {
    @synchronized (self) {
        if (!_isRecording) {
            if (error) {
                *error = [NSError errorWithDomain:@"FFmpegRecorder" 
                                             code:-2 
                                         userInfo:@{NSLocalizedDescriptionKey: @"No recording in progress"}];
            }
            return NO;
        }
        
        _stopRequested = YES;
    }
    
    NSLog(@"[FFmpegRecorder] Stop recording requested");
    return YES;
}

- (BOOL)isRecording {
    @synchronized (self) {
        return _isRecording;
    }
}

@end
