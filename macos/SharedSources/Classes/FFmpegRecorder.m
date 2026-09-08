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
#include <stdlib.h>
#include <string.h>

@interface FFmpegRecorder () {
    // Pre-roll (ring buffer) storage of ref-counted AVPacket copies.
    AVPacket *_preRollPackets;
    int _preRollCount;
    int _preRollCapacity;
    // Synthesized timestamp counters for streams with missing PTS/DTS.
    int64_t _runningInputPts;
    int64_t _runningInputDts;
}

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
@property (atomic, assign) int64_t writtenBytes;
@property (atomic, strong) NSError *lastRecordingError;
@property (nonatomic, strong) dispatch_semaphore_t stopWaitSemaphore;

// Pre-roll state.
@property (atomic, assign) BOOL preRollMode;
@property (atomic, assign) NSInteger preRollSeconds;
@property (atomic, assign) BOOL commitRequested;

- (BOOL)writePacket:(AVPacket *)pkt
            basePts:(int64_t)basePts
            baseDts:(int64_t)baseDts
           inStream:(AVStream *)inStr
          outStream:(AVStream *)outStr;

- (BOOL)flushPreRollBufferWithBasePts:(int64_t)basePts
                              baseDts:(int64_t)baseDts
                             inStream:(AVStream *)inStr
                            outStream:(AVStream *)outStr;

- (BOOL)preRollAppendPacket:(const AVPacket *)pkt;
- (void)preRollClear;
- (void)preRollFree;
- (BOOL)preRollStaleForPacket:(const AVPacket *)pkt;
- (void)preRollHardCap;

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

static BOOL ffmpegRecorderBufferStartsWithStartCode(const uint8_t *data, int size) {
    if (!data || size < 4) return NO;
    if (data[0] == 0x00 && data[1] == 0x00) {
        if (data[2] == 0x01) return YES;
        if (size >= 4 && data[2] == 0x00 && data[3] == 0x01) return YES;
    }
    return NO;
}

static BOOL ffmpegRecorderIsH264IdrNal(uint8_t nalHeader) {
    uint8_t nalType = nalHeader & 0x1F;
    return nalType == 5; // IDR
}

static BOOL ffmpegRecorderIsHevcIrapNal(uint8_t nalHeader) {
    uint8_t nalType = (nalHeader >> 1) & 0x3F;
    // HEVC IRAP pictures: BLA/IDR/CRA range
    return nalType >= 16 && nalType <= 21;
}

static BOOL ffmpegRecorderPacketHasSyncNal(enum AVCodecID codecId, const uint8_t *data, int size) {
    if (!data || size <= 0) return NO;

    int startLen = 0;
    int pos = ffmpegRecorderFindStartCode(data, size, 0, &startLen);
    if (pos >= 0) {
        // Annex-B bitstream
        while (pos >= 0) {
            int nalStart = pos + startLen;
            if (nalStart < size) {
                uint8_t nalHeader = data[nalStart];
                if (codecId == AV_CODEC_ID_H264 && ffmpegRecorderIsH264IdrNal(nalHeader)) {
                    return YES;
                }
                if (codecId == AV_CODEC_ID_HEVC && ffmpegRecorderIsHevcIrapNal(nalHeader)) {
                    return YES;
                }
            }

            int nextLen = 0;
            pos = ffmpegRecorderFindStartCode(data, size, nalStart, &nextLen);
            startLen = nextLen;
        }
        return NO;
    }

    // Length-prefixed format (AVCC/HVCC)
    int offset = 0;
    while (offset + 4 <= size) {
        uint32_t nalSize = ((uint32_t)data[offset] << 24) |
                           ((uint32_t)data[offset + 1] << 16) |
                           ((uint32_t)data[offset + 2] << 8) |
                           (uint32_t)data[offset + 3];
        offset += 4;
        if (nalSize == 0 || offset + (int)nalSize > size) break;

        uint8_t nalHeader = data[offset];
        if (codecId == AV_CODEC_ID_H264 && ffmpegRecorderIsH264IdrNal(nalHeader)) {
            return YES;
        }
        if (codecId == AV_CODEC_ID_HEVC && ffmpegRecorderIsHevcIrapNal(nalHeader)) {
            return YES;
        }
        offset += (int)nalSize;
    }

    return NO;
}

static BOOL ffmpegRecorderPacketIsKeyframeForCodec(enum AVCodecID codecId, const AVPacket *pkt) {
    if (!pkt) return NO;
    if (pkt->flags & AV_PKT_FLAG_KEY) return YES;
    if (codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC) {
        return ffmpegRecorderPacketHasSyncNal(codecId, pkt->data, pkt->size);
    }
    return NO;
}

static int ffmpegRecorderConvertAnnexBToLengthPrefixed(AVPacket *pkt) {
    if (!pkt || !pkt->data || pkt->size < 4) {
        return 0;
    }

    int firstStartLen = 0;
    int firstStart = ffmpegRecorderFindStartCode(pkt->data, pkt->size, 0, &firstStartLen);
    if (firstStart < 0 || firstStart != 0) {
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

// Hard upper bound on buffered packets when PTS-based trimming cannot run.
#define FFMPEG_PRE_ROLL_MAX_PACKETS 1200

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
        _writtenBytes = 0;
        _lastRecordingError = nil;
        _stopWaitSemaphore = nil;
        _preRollMode = NO;
        _preRollSeconds = 0;
        _commitRequested = NO;
        _preRollPackets = NULL;
        _preRollCount = 0;
        _preRollCapacity = 0;
        _runningInputPts = 0;
        _runningInputDts = 0;
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
        _writtenBytes = 0;
        _lastRecordingError = nil;
        _preRollMode = NO;
        _preRollSeconds = 0;
        _commitRequested = NO;
        _runningInputPts = 0;
        _runningInputDts = 0;
        _stopWaitSemaphore = dispatch_semaphore_create(0);
    }
    
    dispatch_async(_recordingQueue, ^{
        [self performRecording];
    });
    
    return YES;
}

- (BOOL)startPreRollWithRtspUrl:(NSString *)rtspUrl
                     outputPath:(NSString *)outputPath
                 preRollSeconds:(NSInteger)preRollSeconds
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
        _writtenBytes = 0;
        _lastRecordingError = nil;
        _preRollMode = YES;
        _preRollSeconds = preRollSeconds > 0 ? preRollSeconds : 5;
        _commitRequested = NO;
        _preRollPackets = NULL;
        _preRollCount = 0;
        _preRollCapacity = 0;
        _runningInputPts = 0;
        _runningInputDts = 0;
        _stopWaitSemaphore = dispatch_semaphore_create(0);
    }

    dispatch_async(_recordingQueue, ^{
        [self performRecording];
    });

    return YES;
}

- (BOOL)commitPreRoll {
    @synchronized (self) {
        if (!_isRecording) {
            return NO;
        }
        if (!_preRollMode) {
            return YES;
        }
        _commitRequested = YES;
        return YES;
    }
}

- (BOOL)preRollAppendPacket:(const AVPacket *)pkt {
    if (_preRollCount == _preRollCapacity) {
        int newCap = _preRollCapacity > 0 ? _preRollCapacity * 2 : 64;
        AVPacket *nb = (AVPacket *)realloc(_preRollPackets, (size_t)newCap * sizeof(AVPacket));
        if (!nb) {
            NSLog(@"[FFmpegRecorder] Failed to grow pre-roll buffer");
            return NO;
        }
        _preRollPackets = nb;
        _preRollCapacity = newCap;
    }
    av_init_packet(&_preRollPackets[_preRollCount]);
    int ret = av_packet_ref(&_preRollPackets[_preRollCount], pkt);
    if (ret < 0) {
        NSLog(@"[FFmpegRecorder] Failed to ref packet into pre-roll buffer: %s", av_err2str(ret));
        return NO;
    }
    _preRollCount++;
    return YES;
}

- (void)preRollClear {
    for (int i = 0; i < _preRollCount; i++) {
        av_packet_unref(&_preRollPackets[i]);
    }
    _preRollCount = 0;
}

- (void)preRollFree {
    [self preRollClear];
    if (_preRollPackets) {
        free(_preRollPackets);
        _preRollPackets = NULL;
    }
    _preRollCapacity = 0;
}

// True when the newest keyframe `pkt` is >= preRollSeconds ahead of the
// current anchor (buffer[0]).
- (BOOL)preRollStaleForPacket:(const AVPacket *)pkt {
    if (_preRollCount == 0) return NO;
    if (_videoStreamIndex < 0 || !_inputContext) return NO;
    AVStream *in = _inputContext->streams[_videoStreamIndex];
    if (in->time_base.num <= 0) return NO;
    int64_t cur = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
    int64_t anchor = _preRollPackets[0].pts != AV_NOPTS_VALUE ? _preRollPackets[0].pts : _preRollPackets[0].dts;
    if (cur == AV_NOPTS_VALUE || anchor == AV_NOPTS_VALUE) return NO;
    int64_t span = cur - anchor;
    if (span < 0) return NO;
    int64_t window = ((int64_t)_preRollSeconds) * in->time_base.den / in->time_base.num;
    return span >= window;
}

// Safety net for streams with missing timestamps.
- (void)preRollHardCap {
    if (_preRollCount <= FFMPEG_PRE_ROLL_MAX_PACKETS) return;
    int lastKey = -1;
    for (int i = _preRollCount - 1; i >= 0; i--) {
        if (_preRollPackets[i].flags & AV_PKT_FLAG_KEY) {
            lastKey = i;
            break;
        }
    }
    if (lastKey > 0) {
        for (int i = 0; i < lastKey; i++) {
            av_packet_unref(&_preRollPackets[i]);
        }
        memmove(_preRollPackets, _preRollPackets + lastKey,
                (size_t)(_preRollCount - lastKey) * sizeof(AVPacket));
        _preRollCount -= lastKey;
    } else if (lastKey < 0) {
        [self preRollClear];
    }
}

- (void)performRecording {
    int ret;
    AVPacket pkt;
    int64_t basePts = AV_NOPTS_VALUE;
    int64_t baseDts = AV_NOPTS_VALUE;
    BOOL gotKeyframe = NO;
    BOOL writing = !_preRollMode;
    BOOL isRtspSource = NO;

    NSLog(@"[FFmpegRecorder] Starting recording from %@ to %@ (preRoll=%d, seconds=%ld)",
          _rtspUrl, _outputPath, _preRollMode, (long)_preRollSeconds);

    NSString *sourceLower = [_rtspUrl lowercaseString];
    isRtspSource = [sourceLower hasPrefix:@"rtsp://"] || [sourceLower hasPrefix:@"rtsps://"];
    
    // Open input (RTSP stream)
    AVDictionary *options = NULL;
    if (isRtspSource) {
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set(&options, "max_delay", "500000", 0);
        av_dict_set(&options, "probesize", "32", 0);
        av_dict_set(&options, "analyzeduration", "1000000", 0);
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

    // Create output context. Recording is standardized to MP4 on mobile.
    const char *formatName = "mp4";
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
    // Set faststart for proper MP4 playback.
    av_dict_set(&muxerOpts, "movflags", "faststart", 0);
    
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

        if (pkt.stream_index != _videoStreamIndex) {
            av_packet_unref(&pkt);
            continue;
        }

        AVStream *inStr = _inputContext->streams[_videoStreamIndex];
        AVStream *outStr = _outputContext->streams[0];
        enum AVCodecID codecId = inStr->codecpar->codec_id;
        BOOL isVideoKeyPacket = ffmpegRecorderPacketIsKeyframeForCodec(codecId, &pkt);

        if (!writing) {
            // ---- pre-roll buffering phase ----
            if (_commitRequested) {
                if (_preRollCount > 0) {
                    basePts = _preRollPackets[0].pts != AV_NOPTS_VALUE ? _preRollPackets[0].pts : _preRollPackets[0].dts;
                    baseDts = _preRollPackets[0].dts != AV_NOPTS_VALUE ? _preRollPackets[0].dts : basePts;
                    if (basePts == AV_NOPTS_VALUE) basePts = 0;
                    if (baseDts == AV_NOPTS_VALUE) baseDts = basePts;
                    _runningInputPts = 0;
                    _runningInputDts = 0;
                    int flushed = _preRollCount;
                    if (![self flushPreRollBufferWithBasePts:basePts baseDts:baseDts inStream:inStr outStream:outStr]) {
                        av_packet_unref(&pkt);
                        break;
                    }
                    gotKeyframe = YES;
                    NSLog(@"[FFmpegRecorder] Pre-roll committed; flushed %d buffered packets", flushed);
                }
                writing = YES;
                // fall through to write the current packet
            } else if (!gotKeyframe) {
                if (isVideoKeyPacket) {
                    pkt.flags |= AV_PKT_FLAG_KEY;
                    if ([self preRollAppendPacket:&pkt]) {
                        gotKeyframe = YES;
                    }
                }
                av_packet_unref(&pkt);
                continue;
            } else {
                if (isVideoKeyPacket) {
                    pkt.flags |= AV_PKT_FLAG_KEY;
                    if ([self preRollStaleForPacket:&pkt]) {
                        [self preRollClear];
                    }
                }
                if ([self preRollAppendPacket:&pkt]) {
                    [self preRollHardCap];
                }
                av_packet_unref(&pkt);
                continue;
            }
        }

        // ---- writing phase ----
        if (!gotKeyframe) {
            if (isVideoKeyPacket) {
                gotKeyframe = YES;
                int64_t keyPts = (pkt.pts != AV_NOPTS_VALUE) ? pkt.pts : pkt.dts;
                int64_t keyDts = (pkt.dts != AV_NOPTS_VALUE) ? pkt.dts : keyPts;
                if (keyPts == AV_NOPTS_VALUE) keyPts = 0;
                if (keyDts == AV_NOPTS_VALUE) keyDts = keyPts;
                basePts = keyPts;
                baseDts = keyDts;
                _runningInputPts = 0;
                _runningInputDts = 0;
            } else {
                av_packet_unref(&pkt);
                continue;
            }
        }

        if (isVideoKeyPacket) {
            pkt.flags |= AV_PKT_FLAG_KEY;
        }
        if (![self writePacket:&pkt basePts:basePts baseDts:baseDts inStream:inStr outStream:outStr]) {
            av_packet_unref(&pkt);
            break;
        }

        av_packet_unref(&pkt);
    }

    NSLog(@"[FFmpegRecorder] Recording loop finished");
    if (_writtenPackets == 0) {
        _lastRecordingError = [NSError errorWithDomain:@"FFmpegRecorder"
                                                  code:-4
                                              userInfo:@{NSLocalizedDescriptionKey: @"No video packets were written before stop"}];
    } else if (_writtenPackets < 5) {
        _lastRecordingError = [NSError errorWithDomain:@"FFmpegRecorder"
                                                  code:-5
                                              userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"Recording incomplete: only %d video packets were written", _writtenPackets]}];
    }
    [self cleanup];
}

- (BOOL)writePacket:(AVPacket *)pkt
            basePts:(int64_t)basePts
            baseDts:(int64_t)baseDts
           inStream:(AVStream *)inStr
          outStream:(AVStream *)outStr {
    int64_t srcPts = pkt->pts;
    int64_t srcDts = pkt->dts;
    if (srcPts == AV_NOPTS_VALUE && srcDts != AV_NOPTS_VALUE) srcPts = srcDts;
    if (srcDts == AV_NOPTS_VALUE && srcPts != AV_NOPTS_VALUE) srcDts = srcPts;
    if (srcPts == AV_NOPTS_VALUE) srcPts = basePts + _runningInputPts;
    if (srcDts == AV_NOPTS_VALUE) srcDts = baseDts + _runningInputDts;

    int64_t relPts = srcPts - basePts;
    int64_t relDts = srcDts - baseDts;
    if (relPts < 0) relPts = 0;
    if (relDts < 0) relDts = 0;

    int64_t inputDur = pkt->duration;
    if (inputDur <= 0 || inputDur == AV_NOPTS_VALUE) {
        if (inStr->avg_frame_rate.num > 0 && inStr->avg_frame_rate.den > 0) {
            inputDur = av_rescale_q(1, av_inv_q(inStr->avg_frame_rate), inStr->time_base);
        }
        if (inputDur <= 0) inputDur = 1;
    }

    pkt->pts = av_rescale_q_rnd(relPts, inStr->time_base, outStr->time_base,
                                AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    pkt->dts = av_rescale_q_rnd(relDts, inStr->time_base, outStr->time_base,
                                AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    if (pkt->pts < pkt->dts) {
        pkt->pts = pkt->dts;
    }
    pkt->duration = av_rescale_q(inputDur, inStr->time_base, outStr->time_base);
    if (pkt->duration <= 0) pkt->duration = 1;
    pkt->stream_index = 0;
    pkt->pos = -1;

    _runningInputPts = relPts + inputDur;
    _runningInputDts = relDts + inputDur;

    int wret = av_interleaved_write_frame(_outputContext, pkt);
    if (wret < 0) {
        NSLog(@"[FFmpegRecorder] Error writing frame: %s", av_err2str(wret));
        return NO;
    }
    _writtenPackets++;
    _writtenBytes += pkt->size;
    return YES;
}

- (BOOL)flushPreRollBufferWithBasePts:(int64_t)basePts
                              baseDts:(int64_t)baseDts
                             inStream:(AVStream *)inStr
                            outStream:(AVStream *)outStr {
    for (int i = 0; i < _preRollCount; i++) {
        AVPacket tmp;
        av_init_packet(&tmp);
        if (av_packet_ref(&tmp, &_preRollPackets[i]) < 0) {
            NSLog(@"[FFmpegRecorder] Failed to ref buffered packet %d", i);
            return NO;
        }
        BOOL ok = [self writePacket:&tmp basePts:basePts baseDts:baseDts inStream:inStr outStream:outStr];
        av_packet_unref(&tmp);
        if (!ok) {
            NSLog(@"[FFmpegRecorder] Failed to flush buffered packet %d", i);
            return NO;
        }
    }
    [self preRollClear];
    return YES;
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

    // Detect false-success outputs where trailer exists but media payload is too small.
    if (_outputPath.length > 0) {
        NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:_outputPath error:nil];
        unsigned long long fileSize = [attrs fileSize];
        NSLog(@"[FFmpegRecorder] Recording summary: packets=%d, payloadBytes=%lld, fileSize=%llu bytes", _writtenPackets, _writtenBytes, fileSize);
    }

    if (!_lastRecordingError && _outputPath.length > 0) {
        NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:_outputPath error:nil];
        unsigned long long fileSize = [attrs fileSize];
        if (fileSize > 0 && fileSize < 32 * 1024) {
            _lastRecordingError = [NSError errorWithDomain:@"FFmpegRecorder"
                                                      code:-6
                                                  userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"Recording output too small (%llu bytes)", fileSize]}];
            NSError *deleteError = nil;
            [[NSFileManager defaultManager] removeItemAtPath:_outputPath error:&deleteError];
            if (deleteError) {
                NSLog(@"[FFmpegRecorder] Failed to delete tiny output file: %@", deleteError.localizedDescription);
            }
        }
    }
    
    // Close input
    if (_inputContext) {
        avformat_close_input(&_inputContext);
        _inputContext = NULL;
    }

    [self preRollFree];

    @synchronized (self) {
        _isRecording = NO;
        _videoStreamIndex = -1;
        _stopRequested = NO;
        _writeHeaderDone = NO;
        _preRollMode = NO;
        _preRollSeconds = 0;
        _commitRequested = NO;
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
