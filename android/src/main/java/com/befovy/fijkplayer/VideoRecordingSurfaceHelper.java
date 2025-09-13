// Android Recording Surface Helper
// This class handles the OpenGL texture copying from SurfaceTexture to MediaCodec input surface

package com.befovy.fijkplayer;

import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.util.Log;
import android.view.Surface;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

public class VideoRecordingSurfaceHelper {
    private static final String TAG = "RecordingSurface";
    
    // OpenGL program for texture copying
    private static final String VERTEX_SHADER = 
        "attribute vec4 aPosition;\n" +
        "attribute vec2 aTexCoord;\n" +
        "varying vec2 vTexCoord;\n" +
        "void main() {\n" +
        "    gl_Position = aPosition;\n" +
        "    vTexCoord = aTexCoord;\n" +
        "}";
    
    private static final String FRAGMENT_SHADER_OES = 
        "#extension GL_OES_EGL_image_external : require\n" +
        "precision mediump float;\n" +
        "uniform samplerExternalOES uTexture;\n" +
        "varying vec2 vTexCoord;\n" +
        "void main() {\n" +
        "    gl_FragColor = texture2D(uTexture, vTexCoord);\n" +
        "}";
        
    private static final String FRAGMENT_SHADER_2D = 
        "precision mediump float;\n" +
        "uniform sampler2D uTexture;\n" +
        "varying vec2 vTexCoord;\n" +
        "void main() {\n" +
        "    gl_FragColor = texture2D(uTexture, vTexCoord);\n" +
        "}";
    
    // Vertex coordinates and texture coordinates
    private static final float[] VERTICES = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };
    
    private EGLDisplay mEGLDisplay = EGL14.EGL_NO_DISPLAY;
    private EGLContext mEGLContext = EGL14.EGL_NO_CONTEXT;
    private EGLSurface mEGLSurface = EGL14.EGL_NO_SURFACE;
    
    private int mProgram;
    private int mPositionHandle;
    private int mTexCoordHandle;
    private int mTextureHandle;
    private FloatBuffer mVertexBuffer;
    
    public boolean initialize(Surface recordingSurface) {
        try {
            // Setup EGL
            mEGLDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
            if (mEGLDisplay == EGL14.EGL_NO_DISPLAY) {
                Log.e(TAG, "Unable to get EGL14 display");
                return false;
            }
            
            int[] version = new int[2];
            if (!EGL14.eglInitialize(mEGLDisplay, version, 0, version, 1)) {
                Log.e(TAG, "Unable to initialize EGL14");
                return false;
            }
            
            // Configure EGL
            int[] configs = new int[1];
            int[] numConfigs = new int[1];
            int[] configAttribs = {
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                EGL14.EGL_RED_SIZE, 8,
                EGL14.EGL_GREEN_SIZE, 8,
                EGL14.EGL_BLUE_SIZE, 8,
                EGL14.EGL_ALPHA_SIZE, 8,
                EGL14.EGL_NONE
            };
            
            EGLConfig[] eglConfigs = new EGLConfig[1];
            if (!EGL14.eglChooseConfig(mEGLDisplay, configAttribs, 0, eglConfigs, 0, 1, numConfigs, 0)) {
                Log.e(TAG, "Unable to find suitable EGL config");
                return false;
            }
            
            // Create EGL context
            int[] contextAttribs = {
                EGL14.EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL14.EGL_NONE
            };
            mEGLContext = EGL14.eglCreateContext(mEGLDisplay, eglConfigs[0], EGL14.EGL_NO_CONTEXT, contextAttribs, 0);
            if (mEGLContext == EGL14.EGL_NO_CONTEXT) {
                Log.e(TAG, "Failed to create EGL context");
                return false;
            }
            
            // Create EGL surface
            int[] surfaceAttribs = { EGL14.EGL_NONE };
            mEGLSurface = EGL14.eglCreateWindowSurface(mEGLDisplay, eglConfigs[0], recordingSurface, surfaceAttribs, 0);
            if (mEGLSurface == EGL14.EGL_NO_SURFACE) {
                Log.e(TAG, "Failed to create EGL surface");
                return false;
            }
            
            // Make current
            if (!EGL14.eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext)) {
                Log.e(TAG, "Failed to make EGL context current");
                return false;
            }
            
            // Setup OpenGL program
            setupOpenGL();
            
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Failed to initialize EGL", e);
            return false;
        }
    }
    
    private void setupOpenGL() {
        // Create shader program
        int vertexShader = loadShader(GLES20.GL_VERTEX_SHADER, VERTEX_SHADER);
        int fragmentShader = loadShader(GLES20.GL_FRAGMENT_SHADER, FRAGMENT_SHADER_OES);
        
        mProgram = GLES20.glCreateProgram();
        GLES20.glAttachShader(mProgram, vertexShader);
        GLES20.glAttachShader(mProgram, fragmentShader);
        GLES20.glLinkProgram(mProgram);
        
        // Get attribute and uniform locations
        mPositionHandle = GLES20.glGetAttribLocation(mProgram, "aPosition");
        mTexCoordHandle = GLES20.glGetAttribLocation(mProgram, "aTexCoord");
        mTextureHandle = GLES20.glGetUniformLocation(mProgram, "uTexture");
        
        // Create vertex buffer
        ByteBuffer bb = ByteBuffer.allocateDirect(VERTICES.length * 4);
        bb.order(ByteOrder.nativeOrder());
        mVertexBuffer = bb.asFloatBuffer();
        mVertexBuffer.put(VERTICES);
        mVertexBuffer.position(0);
    }
    
    private int loadShader(int type, String shaderCode) {
        int shader = GLES20.glCreateShader(type);
        GLES20.glShaderSource(shader, shaderCode);
        GLES20.glCompileShader(shader);
        return shader;
    }
    
    public void drawFrame(int textureId, float[] transformMatrix) {
        // Make sure EGL context is current
        EGL14.eglMakeCurrent(mEGLDisplay, mEGLSurface, mEGLSurface, mEGLContext);
        
        // Clear and setup viewport
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        GLES20.glUseProgram(mProgram);
        
        // Bind texture
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textureId);
        GLES20.glUniform1i(mTextureHandle, 0);
        
        // Set vertex attributes
        mVertexBuffer.position(0);
        GLES20.glVertexAttribPointer(mPositionHandle, 2, GLES20.GL_FLOAT, false, 16, mVertexBuffer);
        GLES20.glEnableVertexAttribArray(mPositionHandle);
        
        mVertexBuffer.position(2);
        GLES20.glVertexAttribPointer(mTexCoordHandle, 2, GLES20.GL_FLOAT, false, 16, mVertexBuffer);
        GLES20.glEnableVertexAttribArray(mTexCoordHandle);
        
        // Draw
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
        
        // Present frame
        EGL14.eglSwapBuffers(mEGLDisplay, mEGLSurface);
    }
    
    public void release() {
        if (mEGLDisplay != EGL14.EGL_NO_DISPLAY) {
            EGL14.eglMakeCurrent(mEGLDisplay, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
            if (mEGLSurface != EGL14.EGL_NO_SURFACE) {
                EGL14.eglDestroySurface(mEGLDisplay, mEGLSurface);
            }
            if (mEGLContext != EGL14.EGL_NO_CONTEXT) {
                EGL14.eglDestroyContext(mEGLDisplay, mEGLContext);
            }
            EGL14.eglTerminate(mEGLDisplay);
        }
        
        mEGLDisplay = EGL14.EGL_NO_DISPLAY;
        mEGLContext = EGL14.EGL_NO_CONTEXT;
        mEGLSurface = EGL14.EGL_NO_SURFACE;
    }
}
