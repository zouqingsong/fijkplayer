// MIT License - FijkPlayer iOS Audio Decoder Implementation
// Uses AudioToolbox AudioConverter for AAC/MP3 decoding

#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#include "FJKAudioDecoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48kHz 32-bit audio

struct FJKAudioDecoder {
    // Stream info
    int sample_rate;
    int channels;
    FFAudioCodec codec;
    
    // AudioConverter
    AudioConverterRef converter;
    AudioStreamBasicDescription input_format;
    AudioStreamBasicDescription output_format;
    
    // Input packet queue
    uint8_t* input_buffer;
    size_t input_size;
    size_t input_capacity;
    int64_t current_pts;
    
    // Output buffer
    int16_t* output_buffer;
    int output_samples;  // Samples available in output_buffer
    int output_capacity; // Max samples in output_buffer
};

// AudioConverter callback
static OSStatus audioConverterCallback(AudioConverterRef converter,
                                       UInt32 *ioNumberDataPackets,
                                       AudioBufferList *ioData,
                                       AudioStreamPacketDescription **outDataPacketDescription,
                                       void *inUserData) {
    FJKAudioDecoder *decoder = (FJKAudioDecoder *)inUserData;
    
    if (decoder->input_size == 0) {
        *ioNumberDataPackets = 0;
        return -1;  // No data
    }
    
    // Provide input data to AudioConverter
    ioData->mBuffers[0].mData = decoder->input_buffer;
    ioData->mBuffers[0].mDataByteSize = (UInt32)decoder->input_size;
    ioData->mBuffers[0].mNumberChannels = decoder->channels;
    
    // Provide packet description if requested
    if (outDataPacketDescription) {
        static AudioStreamPacketDescription packetDesc;
        packetDesc.mStartOffset = 0;
        packetDesc.mVariableFramesInPacket = 0;
        packetDesc.mDataByteSize = decoder->input_size;
        *outDataPacketDescription = &packetDesc;
    }
    
    *ioNumberDataPackets = 1;
    // Don't clear input_size here - let the caller do it after decode completes
    
    return noErr;
}

FJKAudioDecoder* fjk_audio_decoder_create(FFStream* stream) {
    if (!stream || stream->type != FF_STREAM_TYPE_AUDIO) {
        return NULL;
    }
    
    FJKAudioDecoder* decoder = (FJKAudioDecoder*)calloc(1, sizeof(FJKAudioDecoder));
    if (!decoder) return NULL;
    
    decoder->sample_rate = stream->sample_rate;
    decoder->channels = stream->channels;
    decoder->codec = stream->audio_codec;
    decoder->current_pts = 0;
    
    // Allocate input buffer
    decoder->input_capacity = 65536;  // 64KB
    decoder->input_buffer = (uint8_t*)malloc(decoder->input_capacity);
    decoder->input_size = 0;
    
    // Allocate output buffer
    decoder->output_capacity = MAX_AUDIO_FRAME_SIZE / sizeof(int16_t);
    decoder->output_buffer = (int16_t*)malloc(decoder->output_capacity * sizeof(int16_t));
    decoder->output_samples = 0;
    
    if (!decoder->input_buffer || !decoder->output_buffer) {
        free(decoder->input_buffer);
        free(decoder->output_buffer);
        free(decoder);
        return NULL;
    }
    
    // Setup input format (AAC)
    memset(&decoder->input_format, 0, sizeof(AudioStreamBasicDescription));
    decoder->input_format.mSampleRate = stream->sample_rate;
    decoder->input_format.mFormatID = kAudioFormatMPEG4AAC;
    decoder->input_format.mChannelsPerFrame = stream->channels;
    
    // Setup output format (Linear PCM S16)
    memset(&decoder->output_format, 0, sizeof(AudioStreamBasicDescription));
    decoder->output_format.mSampleRate = stream->sample_rate;
    decoder->output_format.mFormatID = kAudioFormatLinearPCM;
    decoder->output_format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    decoder->output_format.mBytesPerPacket = stream->channels * sizeof(int16_t);
    decoder->output_format.mFramesPerPacket = 1;
    decoder->output_format.mBytesPerFrame = stream->channels * sizeof(int16_t);
    decoder->output_format.mChannelsPerFrame = stream->channels;
    decoder->output_format.mBitsPerChannel = 16;
    
    // Create AudioConverter
    OSStatus status = AudioConverterNew(&decoder->input_format,
                                       &decoder->output_format,
                                       &decoder->converter);
    
    if (status != noErr) {
        NSLog(@"[AudioDecoder] Failed to create AudioConverter: %d", (int)status);
        free(decoder->input_buffer);
        free(decoder->output_buffer);
        free(decoder);
        return NULL;
    }
    
    // Set magic cookie (codec configuration) if available
    if (stream->extradata && stream->extradata_size > 0) {
        status = AudioConverterSetProperty(decoder->converter,
                                           kAudioConverterDecompressionMagicCookie,
                                           stream->extradata_size,
                                           stream->extradata);
        if (status != noErr) {
            NSLog(@"[AudioDecoder] ⚠️ Failed to set magic cookie: %d", (int)status);
        } else {
            NSLog(@"[AudioDecoder] ✅ Magic cookie set (%d bytes)", stream->extradata_size);
        }
    } else {
        NSLog(@"[AudioDecoder] ⚠️ No extradata available - may fail for AAC");
    }
    
    printf("[AudioDecoder] Created: %d Hz, %d channels, codec=%d\n",
           stream->sample_rate, stream->channels, stream->audio_codec);
    
    return decoder;
}

void fjk_audio_decoder_destroy(FJKAudioDecoder* decoder) {
    if (!decoder) return;
    
    if (decoder->converter) {
        AudioConverterDispose(decoder->converter);
    }
    
    free(decoder->input_buffer);
    free(decoder->output_buffer);
    free(decoder);
}

int fjk_audio_decoder_send_packet(FJKAudioDecoder* decoder, FFPacket* packet) {
    if (!decoder || !packet || !packet->data) {
        return -1;
    }
    
    // Store packet data in input buffer
    if (packet->size > decoder->input_capacity) {
        // Reallocate if needed
        decoder->input_capacity = packet->size * 2;
        decoder->input_buffer = (uint8_t*)realloc(decoder->input_buffer, decoder->input_capacity);
    }
    
    memcpy(decoder->input_buffer, packet->data, packet->size);
    decoder->input_size = packet->size;
    decoder->current_pts = packet->pts;
    
    return 0;
}

int fjk_audio_decoder_receive_samples(FJKAudioDecoder* decoder, 
                                      int16_t* samples, 
                                      int max_samples,
                                      int64_t* pts) {
    if (!decoder || !samples || max_samples <= 0) {
        return -1;
    }
    
    if (decoder->input_size == 0) {
        return 0;  // Need more data
    }
    
    // Decode using AudioConverter
    UInt32 ioOutputDataPacketSize = max_samples;
    
    AudioBufferList outputBufferList;
    outputBufferList.mNumberBuffers = 1;
    outputBufferList.mBuffers[0].mNumberChannels = decoder->channels;
    outputBufferList.mBuffers[0].mDataByteSize = max_samples * decoder->channels * sizeof(int16_t);
    outputBufferList.mBuffers[0].mData = samples;
    
    OSStatus status = AudioConverterFillComplexBuffer(decoder->converter,
                                                      audioConverterCallback,
                                                      decoder,
                                                      &ioOutputDataPacketSize,
                                                      &outputBufferList,
                                                      NULL);  // Packet desc provided in callback
    
    // Clear input buffer after decode attempt
    decoder->input_size = 0;
    
    if (status != noErr && status != 1) {  // 1 = end of data
        // Only log every 10th error to reduce spam
        static int error_count = 0;
        if (++error_count % 10 == 1) {
            char errorStr[5] = {0};
            *(UInt32*)errorStr = CFSwapInt32HostToBig(status);
            NSLog(@"[AudioDecoder] Decode error: %d ('%s')", (int)status, errorStr);
        }
        return -1;
    }
    
    if (pts) {
        *pts = decoder->current_pts;
    }
    
    return (int)ioOutputDataPacketSize;
}

void fjk_audio_decoder_flush(FJKAudioDecoder* decoder) {
    if (!decoder) return;
    
    decoder->input_size = 0;
    decoder->output_samples = 0;
    
    if (decoder->converter) {
        AudioConverterReset(decoder->converter);
    }
}
