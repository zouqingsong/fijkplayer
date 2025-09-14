//MIT License
//
//Copyright (c) [2019-2020] [Befovy]
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

package com.befovy.fijkplayer;

import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.SurfaceTexture;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.net.Uri;
import android.opengl.GLES11Ext;
import android.opengl.GLES20;
import android.os.Handler;
import android.os.HandlerThread;
import android.text.TextUtils;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

import io.flutter.plugin.common.EventChannel;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import io.flutter.view.TextureRegistry;
import tv.danmaku.ijk.media.player.IMediaPlayer;
import tv.danmaku.ijk.media.player.IjkEventListener;
import tv.danmaku.ijk.media.player.IjkMediaPlayer;
import tv.danmaku.ijk.media.player.misc.IMediaDataSource;

public class FijkPlayer implements MethodChannel.MethodCallHandler, IjkEventListener, IMediaPlayer.OnSnapShotListener {

    final private static AtomicInteger atomicId = new AtomicInteger(0);

    final private static int idle = 0;
    final private static int initialized = 1;
    final private static int asyncPreparing = 2;
    @SuppressWarnings("unused")
    final private static int prepared = 3;
    @SuppressWarnings("unused")
    final private static int started = 4;
    final private static int paused = 5;
    final private static int completed = 6;
    final private static int stopped = 7;
    @SuppressWarnings("unused")
    final private static int error = 8;
    final private static int end = 9;

    final private int mPlayerId;
    final private IjkMediaPlayer mIjkMediaPlayer;
    final private FijkEngine mEngine;
    // non-local field prevent GC
    final private EventChannel mEventChannel;

    // non-local field prevent GC
    final private MethodChannel mMethodChannel;

    final private QueuingEventSink mEventSink = new QueuingEventSink();
    final private HostOption mHostOptions = new HostOption();

    private int mState;
    private int mRotate = -1;
    private int mWidth = 0;
    private int mHeight = 0;
    private TextureRegistry.SurfaceTextureEntry mSurfaceTextureEntry;
    private SurfaceTexture mSurfaceTexture;
    private Surface mSurface;
    final private boolean mJustSurface;
    
    // Recording related fields
    private MediaCodec mMediaCodec;
    private MediaMuxer mMediaMuxer;
    private Surface mRecordingSurface;
    private boolean mIsRecording = false;
    private String mRecordingPath;
    private int mVideoTrackIndex = -1;
    private boolean mMuxerStarted = false;
    private HandlerThread mRecordingThread;
    private Handler mRecordingHandler;
    private VideoRecordingSurfaceHelper mRecordingSurfaceHelper;
    private int mOESTextureId = 0;

    FijkPlayer(@NonNull FijkEngine engine, boolean justSurface) {
        mEngine = engine;
        mPlayerId = atomicId.incrementAndGet();
        mState = 0;
        mJustSurface = justSurface;
        if (justSurface) {
            mIjkMediaPlayer = null;
            mEventChannel = null;
            mMethodChannel = null;
        } else {
            mIjkMediaPlayer = new IjkMediaPlayer();
            mIjkMediaPlayer.addIjkEventListener(this);
            mIjkMediaPlayer.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "enable-position-notify", 1);
            mIjkMediaPlayer.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "start-on-prepared", 0);

            // IjkMediaPlayer.native_setLogLevel(IjkMediaPlayer.IJK_LOG_INFO);
            mMethodChannel = new MethodChannel(mEngine.messenger(), "befovy.com/fijkplayer/" + mPlayerId);
            mMethodChannel.setMethodCallHandler(this);
            mIjkMediaPlayer.setOnSnapShotListener(this);

            mEventChannel = new EventChannel(mEngine.messenger(), "befovy.com/fijkplayer/event/" + mPlayerId);
            mEventChannel.setStreamHandler(new EventChannel.StreamHandler() {
                @Override
                public void onListen(Object o, EventChannel.EventSink eventSink) {
                    mEventSink.setDelegate(eventSink);
                }

                @Override
                public void onCancel(Object o) {
                    mEventSink.setDelegate(null);
                }
            });
        }
    }

    int getPlayerId() {
        return mPlayerId;
    }

    void setup() {
        if (mJustSurface)
            return;
        // Always enable snapshot support
        mIjkMediaPlayer.setAmcGlesRender();
        mIjkMediaPlayer.setOption(IjkMediaPlayer.OPT_CATEGORY_PLAYER, "overlay-format", "fcc-_es2");
    }

    long setupSurface() {
        setup();
        if (mSurfaceTextureEntry == null) {
            TextureRegistry.SurfaceTextureEntry surfaceTextureEntry = mEngine.createSurfaceEntry();
            mSurfaceTextureEntry = surfaceTextureEntry;
            if (surfaceTextureEntry != null) {
                mSurfaceTexture = surfaceTextureEntry.surfaceTexture();
                mSurface = new Surface(mSurfaceTexture);
            }
            if (!mJustSurface) {
                mIjkMediaPlayer.setSurface(mSurface);
            }
        }
        if (mSurfaceTextureEntry != null)
            return mSurfaceTextureEntry.id();
        else {
            Log.e("FIJKPLAYER", "setup surface, null SurfaceTextureEntry");
            return 0;
        }
    }

    void release() {
        // Stop recording if in progress
        if (mIsRecording) {
            cleanupRecording();
        }
        if (!mJustSurface) {
            handleEvent(PLAYBACK_STATE_CHANGED, end, mState, null);
            mIjkMediaPlayer.release();
        }
        if (mSurfaceTextureEntry != null) {
            mSurfaceTextureEntry.release();
            mSurfaceTextureEntry = null;
        }
        if (mSurfaceTexture != null) {
            mSurfaceTexture.release();
            mSurfaceTexture = null;
        }
        if (mSurface != null) {
            mSurface.release();
            mSurface = null;
        }
        if (!mJustSurface) {
            mMethodChannel.setMethodCallHandler(null);
            mEventChannel.setStreamHandler(null);
        }
    }

    private boolean isPlayable(int state) {
        return state == started || state == paused || state == completed || state == prepared;
    }

    private void onStateChanged(int newState, int oldState) {
        if (newState == started && oldState != started) {
            mEngine.onPlayingChange(1);

            if (mHostOptions.getIntOption(HostOption.REQUEST_AUDIOFOCUS, 0) == 1) {
                mEngine.audioFocus(true);
            }

            if (mHostOptions.getIntOption(HostOption.REQUEST_SCREENON, 0) == 1) {
                mEngine.setScreenOn(true);
            }
        } else if (newState != started && oldState == started) {
            mEngine.onPlayingChange(-1);

            if (mHostOptions.getIntOption(HostOption.RELEASE_AUDIOFOCUS, 0) == 1) {
                mEngine.audioFocus(false);
            }

            if (mHostOptions.getIntOption(HostOption.REQUEST_SCREENON, 0) == 1) {
                mEngine.setScreenOn(false);
            }
        }

        if (isPlayable(newState) && !isPlayable(oldState)) {
            mEngine.onPlayableChange(1);
        } else if (!isPlayable(newState) && isPlayable(oldState)) {
            mEngine.onPlayableChange(-1);
        }
    }

    private void handleEvent(int what, int arg1, int arg2, Object extra) {
        Map<String, Object> event = new HashMap<>();

        switch (what) {
            case PREPARED:
                event.put("event", "prepared");
                long duration = mIjkMediaPlayer.getDuration();
                event.put("duration", duration);
                mEventSink.success(event);
                break;
            case PLAYBACK_STATE_CHANGED:
                mState = arg1;
                event.put("event", "state_change");
                event.put("new", arg1);
                event.put("old", arg2);
                onStateChanged(arg1, arg2);
                mEventSink.success(event);
                break;
            case VIDEO_RENDERING_START:
            case AUDIO_RENDERING_START:
                event.put("event", "rendering_start");
                event.put("type", what == VIDEO_RENDERING_START ? "video" : "audio");
                mEventSink.success(event);
                break;
            case BUFFERING_START:
            case BUFFERING_END:
                event.put("event", "freeze");
                event.put("value", what == BUFFERING_START);
                mEventSink.success(event);
                break;

            // buffer / cache position
            case BUFFERING_UPDATE:
                event.put("event", "buffering");
                event.put("head", arg1);
                event.put("percent", arg2);
                mEventSink.success(event);
                break;
            case CURRENT_POSITION_UPDATE:
                event.put("event", "pos");
                event.put("pos", arg1);
                mEventSink.success(event);
                break;
            case VIDEO_ROTATION_CHANGED:
                event.put("event", "rotate");
                event.put("degree", arg1);
                mRotate = arg1;
                mEventSink.success(event);
                if (mWidth > 0 && mHeight > 0) {
                    handleEvent(VIDEO_SIZE_CHANGED, mWidth, mHeight, null);
                }
                break;
            case VIDEO_SIZE_CHANGED:
                event.put("event", "size_changed");
                if (mRotate == 0 || mRotate == 180) {
                    event.put("width", arg1);
                    event.put("height", arg2);
                    mEventSink.success(event);
                } else if (mRotate == 90 || mRotate == 270) {
                    event.put("width", arg2);
                    event.put("height", arg1);
                    mEventSink.success(event);
                }
                // default mRotate is -1 which means unknown
                // do not send event if mRotate is unknown
                mWidth = arg1;
                mHeight = arg2;
                break;
            case SEEK_COMPLETE:
                event.put("event", "seek_complete");
                event.put("pos", arg1);
                event.put("err", arg2);
                mEventSink.success(event);
                break;
            case ERROR:
                mEventSink.error(String.valueOf(arg1), extra.toString(), arg2);
                break;
            default:
                // Log.d("FLUTTER", "jonEvent:" + what);
                break;
        }
    }

    @Override
    public void onSnapShot(IMediaPlayer iMediaPlayer, Bitmap bitmap, int w, int h) {
        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        bitmap.compress(Bitmap.CompressFormat.JPEG, 100, stream);
        bitmap.recycle();
        Map<String, Object> args = new HashMap<>();
        args.put("data", stream.toByteArray());
        args.put("w", w);
        args.put("h", h);
        mMethodChannel.invokeMethod("_onSnapshot", args);
    }

    @Override
    public void onEvent(IjkMediaPlayer ijkMediaPlayer, int what, int arg1, int arg2, Object extra) {
        switch (what) {
            case PREPARED:
            case PLAYBACK_STATE_CHANGED:
            case BUFFERING_START:
            case BUFFERING_END:
            case BUFFERING_UPDATE:
            case VIDEO_SIZE_CHANGED:
            case ERROR:
            case VIDEO_RENDERING_START:
            case AUDIO_RENDERING_START:
            case CURRENT_POSITION_UPDATE:
            case VIDEO_ROTATION_CHANGED:
            case SEEK_COMPLETE:
                handleEvent(what, arg1, arg2, extra);
                break;
            default:
                break;
        }
    }


    private void applyOptions(Object options) {
        if (options instanceof Map) {
            Map optionsMap = (Map) options;
            for (Object o : optionsMap.keySet()) {
                Object option = optionsMap.get(o);
                if (o instanceof Integer && option instanceof Map) {
                    int cat = (Integer) o;
                    Map optionMap = (Map) option;
                    for (Object key : optionMap.keySet()) {
                        Object value = optionMap.get(key);
                        if (key instanceof String && cat != 0) {
                            String name = (String) key;
                            if (value instanceof Integer) {
                                mIjkMediaPlayer.setOption(cat, name, (Integer) value);
                            } else if (value instanceof String) {
                                mIjkMediaPlayer.setOption(cat, name, (String) value);
                            }
                        } else if (key instanceof String) {
                            // cat == 0, hostCategory
                            String name = (String) key;
                            if (value instanceof Integer) {
                                mHostOptions.addIntOption(name, (Integer) value);
                            } else if (value instanceof String) {
                                mHostOptions.addStrOption(name, (String) value);
                            }
                        }
                    }
                }
            }
        }
    }

    private void startRecording(String path, MethodChannel.Result result) {
        if (mIsRecording) {
            result.error("RECORDING_IN_PROGRESS", "Recording is already in progress", null);
            return;
        }

        if (path == null || path.isEmpty()) {
            result.error("INVALID_PATH", "Recording path cannot be null or empty", null);
            return;
        }

        try {
            // Initialize recording thread
            mRecordingThread = new HandlerThread("RecordingThread");
            mRecordingThread.start();
            mRecordingHandler = new Handler(mRecordingThread.getLooper());

            // Create MediaMuxer for output
            mMediaMuxer = new MediaMuxer(path, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);

            // Create MediaCodec for H.264 encoding with higher quality
            MediaFormat format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, 
                mWidth > 0 ? mWidth : 1280, mHeight > 0 ? mHeight : 720);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            format.setInteger(MediaFormat.KEY_BIT_RATE, 5000000); // 5Mbps for better quality
            format.setInteger(MediaFormat.KEY_FRAME_RATE, 30);
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2); // More frequent keyframes

            mMediaCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
            
            // Set up async callback for MediaCodec BEFORE configuring
            mMediaCodec.setCallback(new MediaCodec.Callback() {
                @Override
                public void onInputBufferAvailable(MediaCodec codec, int index) {
                    // Surface input, so this won't be called
                }

                @Override
                public void onOutputBufferAvailable(MediaCodec codec, int index, MediaCodec.BufferInfo info) {
                    if (mIsRecording) {
                        mRecordingHandler.post(() -> processEncodedData(codec, index, info));
                    }
                }

                @Override
                public void onError(MediaCodec codec, MediaCodec.CodecException e) {
                    Log.e("FIJKPLAYER", "MediaCodec error", e);
                    mMethodChannel.invokeMethod("_onRecordingError", e.getMessage());
                }

                @Override
                public void onOutputFormatChanged(MediaCodec codec, MediaFormat format) {
                    if (!mMuxerStarted) {
                        mVideoTrackIndex = mMediaMuxer.addTrack(format);
                        mMediaMuxer.start();
                        mMuxerStarted = true;
                        Log.d("FIJKPLAYER", "Muxer started with video track");
                    }
                }
            });
            
            mMediaCodec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            
            // Get input surface for encoding
            mRecordingSurface = mMediaCodec.createInputSurface();
            mMediaCodec.start();

            // Initialize recording surface helper for texture copying
            mRecordingSurfaceHelper = new VideoRecordingSurfaceHelper();
            if (!mRecordingSurfaceHelper.initialize(mRecordingSurface)) {
                cleanupRecording();
                result.error("SURFACE_INIT_FAILED", "Failed to initialize recording surface helper", null);
                return;
            }

            // Set frame listener to capture frames
            if (mSurfaceTexture != null) {
                mSurfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
                    @Override
                    public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                        if (mIsRecording && mRecordingSurfaceHelper != null) {
                            // Copy frame to MediaCodec surface using OpenGL
                            mRecordingHandler.post(() -> {
                                try {
                                    surfaceTexture.updateTexImage();
                                    
                                    // Create OES texture if not exists
                                    if (mOESTextureId == 0) {
                                        int[] textures = new int[1];
                                        android.opengl.GLES20.glGenTextures(1, textures, 0);
                                        mOESTextureId = textures[0];
                                        android.opengl.GLES20.glBindTexture(android.opengl.GLES11Ext.GL_TEXTURE_EXTERNAL_OES, mOESTextureId);
                                        android.opengl.GLES20.glTexParameteri(android.opengl.GLES11Ext.GL_TEXTURE_EXTERNAL_OES, 
                                            android.opengl.GLES20.GL_TEXTURE_MIN_FILTER, android.opengl.GLES20.GL_LINEAR);
                                        android.opengl.GLES20.glTexParameteri(android.opengl.GLES11Ext.GL_TEXTURE_EXTERNAL_OES, 
                                            android.opengl.GLES20.GL_TEXTURE_MAG_FILTER, android.opengl.GLES20.GL_LINEAR);
                                        android.opengl.GLES20.glTexParameteri(android.opengl.GLES11Ext.GL_TEXTURE_EXTERNAL_OES, 
                                            android.opengl.GLES20.GL_TEXTURE_WRAP_S, android.opengl.GLES20.GL_CLAMP_TO_EDGE);
                                        android.opengl.GLES20.glTexParameteri(android.opengl.GLES11Ext.GL_TEXTURE_EXTERNAL_OES, 
                                            android.opengl.GLES20.GL_TEXTURE_WRAP_T, android.opengl.GLES20.GL_CLAMP_TO_EDGE);
                                    }
                                    
                                    float[] transformMatrix = new float[16];
                                    surfaceTexture.getTransformMatrix(transformMatrix);
                                    mRecordingSurfaceHelper.drawFrame(mOESTextureId, transformMatrix);
                                } catch (Exception e) {
                                    Log.e("FIJKPLAYER", "Error processing frame for recording", e);
                                }
                            });
                        }
                    }
                });
            }

            mRecordingPath = path;
            mIsRecording = true;
            
            // Notify Dart side that recording started
            mMethodChannel.invokeMethod("_onRecordingStarted", null);
            result.success(null);
            
        } catch (Exception e) {
            Log.e("FIJKPLAYER", "Failed to start recording", e);
            cleanupRecording();
            mMethodChannel.invokeMethod("_onRecordingError", e.getMessage());
            result.error("RECORDING_FAILED", "Failed to start recording: " + e.getMessage(), null);
        }
    }

    private void processEncodedData(MediaCodec codec, int index, MediaCodec.BufferInfo info) {
        if (mIsRecording && mMuxerStarted) {
            try {
                java.nio.ByteBuffer encodedData = codec.getOutputBuffer(index);
                if (encodedData != null && info.size > 0) {
                    mMediaMuxer.writeSampleData(mVideoTrackIndex, encodedData, info);
                }
                codec.releaseOutputBuffer(index, false);
            } catch (Exception e) {
                Log.e("FIJKPLAYER", "Error processing encoded data", e);
            }
        } else {
            codec.releaseOutputBuffer(index, false);
        }
    }

    private void cleanupRecording() {
        mIsRecording = false;
        
        if (mRecordingSurfaceHelper != null) {
            mRecordingSurfaceHelper.release();
            mRecordingSurfaceHelper = null;
        }
        
        if (mOESTextureId != 0) {
            GLES20.glDeleteTextures(1, new int[]{mOESTextureId}, 0);
            mOESTextureId = 0;
        }
        
        if (mMediaCodec != null) {
            try {
                mMediaCodec.stop();
                mMediaCodec.release();
            } catch (Exception e) {
                Log.e("FIJKPLAYER", "Error stopping MediaCodec", e);
            }
            mMediaCodec = null;
        }
        
        if (mRecordingSurface != null) {
            mRecordingSurface.release();
            mRecordingSurface = null;
        }
        
        if (mMediaMuxer != null) {
            try {
                if (mMuxerStarted) {
                    mMediaMuxer.stop();
                }
                mMediaMuxer.release();
            } catch (Exception e) {
                Log.e("FIJKPLAYER", "Error stopping MediaMuxer", e);
            }
            mMediaMuxer = null;
        }
        
        if (mRecordingThread != null) {
            mRecordingThread.quitSafely();
            mRecordingThread = null;
            mRecordingHandler = null;
        }
        
        mVideoTrackIndex = -1;
        mMuxerStarted = false;
        mRecordingPath = null;
    }

    private void stopRecording(MethodChannel.Result result) {
        if (!mIsRecording) {
            result.error("NO_RECORDING", "No recording in progress", null);
            return;
        }

        try {
            String recordingPath = mRecordingPath;
            cleanupRecording();
            
            // Notify Dart side that recording stopped
            mMethodChannel.invokeMethod("_onRecordingStopped", recordingPath);
            result.success(null);
            
        } catch (Exception e) {
            Log.e("FIJKPLAYER", "Failed to stop recording", e);
            cleanupRecording();
            mMethodChannel.invokeMethod("_onRecordingError", e.getMessage());
            result.error("RECORDING_STOP_FAILED", "Failed to stop recording: " + e.getMessage(), null);
        }
    }

    @Override
    public void onMethodCall(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
        //noinspection IfCanBeSwitch
        if (call.method.equals("setupSurface")) {
            long viewId = setupSurface();
            result.success(viewId);
        } else if (call.method.equals("setOption")) {
            Integer category = call.argument("cat");
            final String key = call.argument("key");
            if (call.hasArgument("long")) {
                final Integer value = call.argument("long");
                if (category != null && category != 0) {
                    mIjkMediaPlayer.setOption(category, key, value != null ? value.longValue() : 0);
                } else if (category != null) {
                    // cat == 0, hostCategory
                    mHostOptions.addIntOption(key, value);
                }
            } else if (call.hasArgument("str")) {
                final String value = call.argument("str");
                if (category != null && category != 0) {
                    mIjkMediaPlayer.setOption(category, key, value);
                } else if (category != null) {
                    // cat == 0, hostCategory
                    mHostOptions.addStrOption(key, value);
                }
            } else {
                Log.w("FIJKPLAYER", "error arguments for setOptions");
            }
            result.success(null);
        } else if (call.method.equals("applyOptions")) {
            applyOptions(call.arguments);
            result.success(null);
        } else if (call.method.equals("setDataSource")) {
            String url = call.argument("url");
            Uri uri = Uri.parse(url);
            boolean openAsset = false;
            if ("asset".equals(uri.getScheme())) {
                openAsset = true;
                String host = uri.getHost();
                String path = uri.getPath() != null ? uri.getPath().substring(1) : "";
                String asset = mEngine.lookupKeyForAsset(path, host);
                if (!TextUtils.isEmpty(asset)) {
                    uri = Uri.parse(asset);
                }
            }
            try {
                Context context = mEngine.context();
                if (openAsset && context != null) {
                    AssetManager assetManager = context.getAssets();
                    InputStream is = assetManager.open(uri.getPath() != null ? uri.getPath() : "", AssetManager.ACCESS_RANDOM);
                    mIjkMediaPlayer.setDataSource(new RawMediaDataSource(is));
                } else if (context != null){
                    if (TextUtils.isEmpty(uri.getScheme()) || "file".equals(uri.getScheme())) {
                        String path = uri.getPath() != null ? uri.getPath() : "";
                        IMediaDataSource dataSource = new FileMediaDataSource(new File(path));
                        mIjkMediaPlayer.setDataSource(dataSource);
                    } else {
                        mIjkMediaPlayer.setDataSource(mEngine.context(), uri);
                    }
                } else {
                    Log.e("FIJKPLAYER", "context null, can't setDataSource");
                }
                handleEvent(PLAYBACK_STATE_CHANGED, initialized, -1, null);
                if (context == null) {
                    handleEvent(PLAYBACK_STATE_CHANGED, error, -1, null);
                }
                result.success(null);
            } catch (FileNotFoundException e) {
                result.error("-875574348", "Local File not found:" + e.getMessage(), null);
            } catch (IOException e) {
                result.error("-1162824012", "Local IOException:" + e.getMessage(), null);
            }
        } else if (call.method.equals("prepareAsync")) {
            setup();
            mIjkMediaPlayer.prepareAsync();
            handleEvent(PLAYBACK_STATE_CHANGED, asyncPreparing, -1, null);
            result.success(null);
        } else if (call.method.equals("start")) {
            mIjkMediaPlayer.start();
            result.success(null);
        } else if (call.method.equals("pause")) {
            mIjkMediaPlayer.pause();
            result.success(null);
        } else if (call.method.equals("stop")) {
            mIjkMediaPlayer.stop();
            handleEvent(PLAYBACK_STATE_CHANGED, stopped, -1, null);
            result.success(null);
        } else if (call.method.equals("reset")) {
            mIjkMediaPlayer.reset();
            handleEvent(PLAYBACK_STATE_CHANGED, idle, -1, null);
            result.success(null);
        } else if (call.method.equals("getCurrentPosition")) {
            long pos = mIjkMediaPlayer.getCurrentPosition();
            result.success(pos);
        } else if (call.method.equals("setVolume")) {
            final Double volume = call.argument("volume");
            float vol = volume != null ? volume.floatValue() : 1.0f;
            mIjkMediaPlayer.setVolume(vol, vol);
            result.success(null);
        } else if (call.method.equals("seekTo")) {
            final Integer msec = call.argument("msec");
            if (mState == completed)
                handleEvent(PLAYBACK_STATE_CHANGED, paused, -1, null);
            mIjkMediaPlayer.seekTo(msec != null ? msec.longValue() : 0);
            result.success(null);
        } else if (call.method.equals("setLoop")) {
            final Integer loopCount = call.argument("loop");
            mIjkMediaPlayer.setLoopCount(loopCount != null ? loopCount : 1);
            result.success(null);
        } else if (call.method.equals("setSpeed")) {
            final Double speed = call.argument("speed");
            mIjkMediaPlayer.setSpeed(speed != null ? speed.floatValue() : 1.0f);
            result.success(null);
        } else if (call.method.equals("snapshot")) {
            // Force enable snapshot for this call
            if (mIjkMediaPlayer != null) {
                mIjkMediaPlayer.snapShot();
            } else {
                mMethodChannel.invokeMethod("_onSnapshot", "Player not initialized");
            }
            result.success(null);
        } else if (call.method.equals("startRecording")) {
            final String path = call.argument("path");
            startRecording(path, result);
        } else if (call.method.equals("stopRecording")) {
            stopRecording(result);
        } else {
            result.notImplemented();
        }
    }
}
