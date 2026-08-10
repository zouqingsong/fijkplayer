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
@property (atomic, assign) BOOL writeHeaderDone;
@property (atomic, assign) int writtenPackets;
@property (atomic, strong) NSError *lastRecordingError;
@property (nonatomic, strong) dispatch_semaphore_t stopWaitSemaphore;

@end

static int ffmpegRecorderInterruptCallback(void *opaque) {
    FFmpegRecorder *recorder = (__bridge FFmpegRecorder *)opaque;
    return recorder.stopRequested ? 1 : 0;
}

static int ffmpegRecorderFindStartCode(const uint8_t *data, int size, int from, int *startCodeLen) {
    for (int i = from; i + 3 < size; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                *startCodeLen = 3;
                return i;
            }
            if (i + 4 < size && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                *startCodeLen = 4;
                return i;
            }
        }
    }
    return -1;
}

static int ffmpegRecorderConvertAnnexBToLengthPrefixed(AVPacket *pkt) {
    if (!pkt || !pkt->data || pkt->size < 4) {
        return 0;
    }

    int firstStartLen = 0;
    int firstStart = ffmpegRecorderFindStartCode(pkt->data, pkt->size, 0, &firstStartLen);
    if (firstStart < 0) {
        // Packet does not look like Annex-B. Keep original payload.
        return 0;
    }

    int nalCount = 0;
    int outSize = 0;
    int pos = firstStart;
    int scLen = firstStartLen;
    while (pos >= 0) {
        int nalStart = pos + scLen;
        int nextScLen = 0;
        int nextPos = ffmpegRecorderFindStartCode(pkt->data, pkt->size, nalStart, &nextScLen);
        int nalEnd = (nextPos >= 0) ? nextPos : pkt->size;
        int nalSize = nalEnd - nalStart;
        if (nalSize > 0) {
            nalCount++;
            outSize += 4 + nalSize;
        }
        pos = nextPos;
        scLen = nextScLen;
    }

    if (nalCount == 0 || outSize <= 0) {
        return AVERROR_INVALIDDATA;
    }

    AVPacket converted;
    av_init_packet(&converted);
    int ret = av_new_packet(&converted, outSize);
    if (ret < 0) {
        return ret;
    }

    int writeOffset = 0;
    pos = firstStart;
    scLen = firstStartLen;
    while (pos >= 0) {
        int nalStart = pos + scLen;
        int nextScLen = 0;
        int nextPos = ffmpegRecorderFindStartCode(pkt->data, pkt->size, nalStart, &nextScLen);
        int nalEnd = (nextPos >= 0) ? nextPos : pkt->size;
        int nalSize = nalEnd - nalStart;
        if (nalSize > 0) {
            converted.data[writeOffset + 0] = (uint8_t)((nalSize >> 24) & 0xFF);
            converted.data[writeOffset + 1] = (uint8_t)((nalSize >> 16) & 0xFF);
            converted.data[writeOffset + 2] = (uint8_t)((nalSize >> 8) & 0xFF);
            converted.data[writeOffset + 3] = (uint8_t)(nalSize & 0xFF);
            memcpy(converted.data + writeOffset + 4, pkt->data + nalStart, nalSize);
            writeOffset += 4 + nalSize;
        }
        pos = nextPos;
        scLen = nextScLen;
    }

    ret = av_packet_copy_props(&converted, pkt);
    if (ret < 0) {
        av_packet_unref(&converted);
        return ret;
    }

    av_packet_unref(pkt);
    av_packet_move_ref(pkt, &converted);
    return 0;
}

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
        _writeHeaderDone = NO;
        _writtenPackets = 0;
        _lastRecordingError = nil;
        _stopWaitSemaphore = nil;
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
        _writeHeaderDone = NO;
        _writtenPackets = 0;
        _lastRecordingError = nil;
        _stopWaitSemaphore = dispatch_semaphore_create(0);
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
    BOOL useMpegTsOutput = NO;
    BOOL isRtspSource = NO;
    
    NSLog(@"[FFmpegRecorder] Starting recording from %@ to %@", _rtspUrl, _outputPath);

    NSString *sourceLower = [_rtspUrl lowercaseString];
    isRtspSource = [sourceLower hasPrefix:@"rtsp://"] || [sourceLower hasPrefix:@"rtsps://"];
    
    // Open input (RTSP stream)
    AVDictionary *options = NULL;
    if (isRtspSource) {
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set(&options, "max_delay", "500000", 0);
    }
    
    ret = avformat_open_input(&_inputContext, [_rtspUrl UTF8String], NULL, &options);
    av_dict_free(&options);
    if (_inputContext) {
        _inputContext->interrupt_callback.callback = ffmpegRecorderInterruptCallback;
        _inputContext->interrupt_callback.opaque = (__bridge void *)self;
    }
    
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

    enum AVCodecID inputCodecId = _inputContext->streams[_videoStreamIndex]->codecpar->codec_id;
    if (isRtspSource && inputCodecId == AV_CODEC_ID_HEVC) {
        // HEVC over RTSP can carry packetization that is fragile in MP4 copy-muxing.
        // Prefer MPEG-TS to preserve Annex-B semantics and maximize decoder compatibility.
        useMpegTsOutput = YES;
    }
    
    // Create output context (MP4 for H264, MPEG-TS for HEVC)
    const char *formatName = useMpegTsOutput ? "mpegts" : "mp4";
    ret = avformat_alloc_output_context2(&_outputContext, NULL, formatName, [_outputPath UTF8String]);
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
    outStream->time_base = inStream->time_base;
    
    // Open output file
    if (!(_outputContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&_outputContext->pb, [_outputPath UTF8String], AVIO_FLAG_WRITE);
        if (ret < 0) {
            NSLog(@"[FFmpegRecorder] Failed to open output file: %s", av_err2str(ret));
            [self cleanup];
            return;
        }
    }
    
    AVDictionary *muxerOpts = NULL;
    if (!useMpegTsOutput) {
        // Set faststart for proper MP4 playback.
        av_dict_set(&muxerOpts, "movflags", "faststart", 0);
    }
    
    // Write header
    ret = avformat_write_header(_outputContext, &muxerOpts);
    av_dict_free(&muxerOpts);
    if (ret < 0) {
        NSLog(@"[FFmpegRecorder] Failed to write header: %s", av_err2str(ret));
        [self cleanup];
        return;
    }
    _writeHeaderDone = YES;
    
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
            AVStream *inStr = _inputContext->streams[_videoStreamIndex];
            AVStream *outStr = _outputContext->streams[0];

            // Some RTSP sources provide Annex-B payloads. MP4 expects length-prefixed
            // samples for H264/HEVC, so normalize packet framing before muxing.
            enum AVCodecID codecId = inStr->codecpar->codec_id;
            if (!useMpegTsOutput && (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC)) {
                int convertRet = ffmpegRecorderConvertAnnexBToLengthPrefixed(&pkt);
                if (convertRet < 0) {
                    NSLog(@"[FFmpegRecorder] Failed to convert Annex-B packet: %s", av_err2str(convertRet));
                    av_packet_unref(&pkt);
                    break;
                }
            }

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
            _writtenPackets++;
        }
        
        av_packet_unref(&pkt);
    }
    
    NSLog(@"[FFmpegRecorder] Recording loop finished");
    if (_writtenPackets == 0) {
        _lastRecordingError = [NSError errorWithDomain:@"FFmpegRecorder"
                                                  code:-4
                                              userInfo:@{NSLocalizedDescriptionKey: @"No video packets were written before stop"}];
    }
    [self cleanup];
}

- (void)cleanup {
    // Write trailer
    if (_outputContext) {
        if (_writeHeaderDone) {
            int trailerRet = av_write_trailer(_outputContext);
            if (trailerRet < 0 && !_lastRecordingError) {
                _lastRecordingError = [NSError errorWithDomain:@"FFmpegRecorder"
                                                          code:trailerRet
                                                      userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"Failed to finalize recording trailer: %s", av_err2str(trailerRet)]}];
            }
        }
        
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
        _writeHeaderDone = NO;
    }
    dispatch_semaphore_t waitSem = _stopWaitSemaphore;
    if (waitSem) {
        dispatch_semaphore_signal(waitSem);
    }
    
    NSLog(@"[FFmpegRecorder] Cleanup completed");
}

- (BOOL)stopRecordingWithError:(NSError **)error {
    dispatch_semaphore_t waitSem = nil;
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
        waitSem = _stopWaitSemaphore;
    }
    
    NSLog(@"[FFmpegRecorder] Stop recording requested");

    if (waitSem) {
        // Wait for performRecording to flush trailer and cleanup.
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC);
        long waitResult = dispatch_semaphore_wait(waitSem, timeout);
        if (waitResult != 0) {
            if (error) {
                *error = [NSError errorWithDomain:@"FFmpegRecorder"
                                             code:-3
                                         userInfo:@{NSLocalizedDescriptionKey: @"Timed out waiting for recorder finalization"}];
            }
            return NO;
        }
    }

    if (_lastRecordingError) {
        if (error) {
            *error = _lastRecordingError;
        }
        return NO;
    }

    return YES;
}

- (BOOL)isRecording {
    @synchronized (self) {
        return _isRecording;
    }
}

@end
