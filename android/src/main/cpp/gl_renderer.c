/**
 * OpenGL ES Renderer Implementation
 * Hardware-accelerated YUV to RGB conversion and rendering
 */

#include "gl_renderer.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include <sys/time.h>

#define LOG_TAG "GLRenderer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Vertex shader (simple pass-through with texture coordinates)
static const char* vertex_shader_source =
    "#version 100\n"
    "attribute vec4 aPosition;\n"
    "attribute vec2 aTexCoord;\n"
    "varying vec2 vTexCoord;\n"
    "void main() {\n"
    "    gl_Position = aPosition;\n"
    "    vTexCoord = aTexCoord;\n"
    "}\n";

// Fragment shader for YUV420P (planar) to RGB conversion
static const char* fragment_shader_yuv420p =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec2 vTexCoord;\n"
    "uniform sampler2D yTexture;\n"
    "uniform sampler2D uTexture;\n"
    "uniform sampler2D vTexture;\n"
    "uniform mat3 colorMatrix;\n"
    "void main() {\n"
    "    float y = texture2D(yTexture, vTexCoord).r;\n"
    "    float u = texture2D(uTexture, vTexCoord).r - 0.5;\n"
    "    float v = texture2D(vTexture, vTexCoord).r - 0.5;\n"
    "    vec3 yuv = vec3(y, u, v);\n"
    "    vec3 rgb = colorMatrix * yuv;\n"
    "    gl_FragColor = vec4(rgb, 1.0);\n"
    "}\n";

// Fragment shader for NV12/NV21 (semi-planar) to RGB conversion
static const char* fragment_shader_nv12 =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec2 vTexCoord;\n"
    "uniform sampler2D yTexture;\n"
    "uniform sampler2D uvTexture;\n"
    "uniform mat3 colorMatrix;\n"
    "uniform bool isNV21;\n"  // NV21 has VU order instead of UV
    "void main() {\n"
    "    float y = texture2D(yTexture, vTexCoord).r;\n"
    "    vec2 uv = texture2D(uvTexture, vTexCoord).ra;\n"
    "    if (isNV21) {\n"
    "        uv = uv.gr;\n"  // Swap for NV21
    "    }\n"
    "    float u = uv.r - 0.5;\n"
    "    float v = uv.g - 0.5;\n"
    "    vec3 yuv_vec = vec3(y, u, v);\n"
    "    vec3 rgb = colorMatrix * yuv_vec;\n"
    "    gl_FragColor = vec4(rgb, 1.0);\n"
    "}\n";

// BT.601 color conversion matrix (standard definition)
static const float bt601_matrix[9] = {
    1.0f,     0.0f,      1.402f,
    1.0f,    -0.344f,   -0.714f,
    1.0f,     1.772f,    0.0f
};

// Full rectangle vertices (normalized device coordinates)
static const float vertices[] = {
    -1.0f, -1.0f,  // Bottom-left
     1.0f, -1.0f,  // Bottom-right
    -1.0f,  1.0f,  // Top-left
     1.0f,  1.0f   // Top-right
};

// Texture coordinates (standard orientation)
static const float tex_coords[] = {
    0.0f, 1.0f,  // Bottom-left
    1.0f, 1.0f,  // Bottom-right
    0.0f, 0.0f,  // Top-left
    1.0f, 0.0f   // Top-right
};

// Renderer structure
struct GLRenderer {
    // OpenGL objects
    GLuint program_yuv420p;
    GLuint program_nv12;
    GLuint y_texture;
    GLuint u_texture;
    GLuint v_texture;
    GLuint uv_texture;
    GLuint vbo_vertices;
    GLuint vbo_texcoords;
    
    // Shader uniforms
    GLint u_y_texture;
    GLint u_u_texture;
    GLint u_v_texture;
    GLint u_uv_texture;
    GLint u_color_matrix;
    GLint u_is_nv21;
    GLint a_position;
    GLint a_texcoord;
    
    // Current state
    int texture_width;
    int texture_height;
    int surface_width;
    int surface_height;
    FrameFormat current_format;
    float color_matrix[9];
    
    // Configuration
    GLRendererConfig config;
    
    // Statistics
    GLRendererStats stats;
    int64_t last_fps_time;
    int fps_frame_count;
    
    // State flags
    bool initialized;
    bool textures_allocated;
};

// Helper: Compile shader
static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        LOGE("Failed to create shader");
        return 0;
    }
    
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char* info_log = (char*)malloc(info_len);
            glGetShaderInfoLog(shader, info_len, NULL, info_log);
            LOGE("Shader compile error: %s", info_log);
            free(info_log);
        }
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

// Helper: Link shader program
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    if (program == 0) {
        LOGE("Failed to create program");
        return 0;
    }
    
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char* info_log = (char*)malloc(info_len);
            glGetProgramInfoLog(program, info_len, NULL, info_log);
            LOGE("Program link error: %s", info_log);
            free(info_log);
        }
        glDeleteProgram(program);
        return 0;
    }
    
    return program;
}

// Helper: Create texture
static GLuint create_texture(int width, int height, GLint format) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Allocate texture storage
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, 
                 format, GL_UNSIGNED_BYTE, NULL);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOGE("Failed to create texture: 0x%x", error);
        glDeleteTextures(1, &texture);
        return 0;
    }
    
    return texture;
}

// Helper: Get current time in microseconds
static int64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

GLRenderer* gl_renderer_create(const GLRendererConfig* config) {
    GLRenderer* renderer = (GLRenderer*)calloc(1, sizeof(GLRenderer));
    if (!renderer) {
        LOGE("Failed to allocate renderer");
        return NULL;
    }
    
    // Set configuration
    if (config) {
        renderer->config = *config;
    } else {
        renderer->config.texture_width = 0;  // Auto-detect from first frame
        renderer->config.texture_height = 0;
        renderer->config.maintain_aspect = true;
        renderer->config.flip_vertical = false;
    }
    
    // Set default color matrix (BT.601)
    memcpy(renderer->color_matrix, bt601_matrix, sizeof(bt601_matrix));
    
    // Compile shaders
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    if (vertex_shader == 0) {
        free(renderer);
        return NULL;
    }
    
    GLuint fragment_yuv420p = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_yuv420p);
    GLuint fragment_nv12 = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_nv12);
    
    if (fragment_yuv420p == 0 || fragment_nv12 == 0) {
        glDeleteShader(vertex_shader);
        if (fragment_yuv420p) glDeleteShader(fragment_yuv420p);
        if (fragment_nv12) glDeleteShader(fragment_nv12);
        free(renderer);
        return NULL;
    }
    
    // Link programs
    renderer->program_yuv420p = link_program(vertex_shader, fragment_yuv420p);
    renderer->program_nv12 = link_program(vertex_shader, fragment_nv12);
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_yuv420p);
    glDeleteShader(fragment_nv12);
    
    if (renderer->program_yuv420p == 0 || renderer->program_nv12 == 0) {
        if (renderer->program_yuv420p) glDeleteProgram(renderer->program_yuv420p);
        if (renderer->program_nv12) glDeleteProgram(renderer->program_nv12);
        free(renderer);
        return NULL;
    }
    
    // Create VBOs
    glGenBuffers(1, &renderer->vbo_vertices);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo_vertices);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glGenBuffers(1, &renderer->vbo_texcoords);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo_texcoords);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_STATIC_DRAW);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    renderer->initialized = true;
    renderer->last_fps_time = get_time_us();
    
    LOGD("OpenGL ES renderer created");
    return renderer;
}

void gl_renderer_destroy(GLRenderer* renderer) {
    if (!renderer) return;
    
    // Delete textures
    if (renderer->y_texture) glDeleteTextures(1, &renderer->y_texture);
    if (renderer->u_texture) glDeleteTextures(1, &renderer->u_texture);
    if (renderer->v_texture) glDeleteTextures(1, &renderer->v_texture);
    if (renderer->uv_texture) glDeleteTextures(1, &renderer->uv_texture);
    
    // Delete VBOs
    if (renderer->vbo_vertices) glDeleteBuffers(1, &renderer->vbo_vertices);
    if (renderer->vbo_texcoords) glDeleteBuffers(1, &renderer->vbo_texcoords);
    
    // Delete programs
    if (renderer->program_yuv420p) glDeleteProgram(renderer->program_yuv420p);
    if (renderer->program_nv12) glDeleteProgram(renderer->program_nv12);
    
    free(renderer);
    LOGD("OpenGL ES renderer destroyed");
}

int gl_renderer_set_surface_size(GLRenderer* renderer, int width, int height) {
    if (!renderer) return -1;
    
    renderer->surface_width = width;
    renderer->surface_height = height;
    glViewport(0, 0, width, height);
    
    LOGD("Surface size set: %dx%d", width, height);
    return 0;
}

static int allocate_textures(GLRenderer* renderer, int width, int height, FrameFormat format) {
    if (renderer->textures_allocated && 
        renderer->texture_width == width &&
        renderer->texture_height == height &&
        renderer->current_format == format) {
        return 0;  // Already allocated with correct dimensions
    }
    
    // Delete old textures
    if (renderer->y_texture) glDeleteTextures(1, &renderer->y_texture);
    if (renderer->u_texture) glDeleteTextures(1, &renderer->u_texture);
    if (renderer->v_texture) glDeleteTextures(1, &renderer->v_texture);
    if (renderer->uv_texture) glDeleteTextures(1, &renderer->uv_texture);
    
    renderer->y_texture = 0;
    renderer->u_texture = 0;
    renderer->v_texture = 0;
    renderer->uv_texture = 0;
    
    // Create Y texture (full resolution)
    renderer->y_texture = create_texture(width, height, GL_LUMINANCE);
    if (renderer->y_texture == 0) {
        LOGE("Failed to create Y texture");
        return -1;
    }
    
    // Create U/V or UV texture based on format
    if (format == FRAME_FORMAT_YUV420P) {
        // Planar: separate U and V textures (half resolution)
        renderer->u_texture = create_texture(width / 2, height / 2, GL_LUMINANCE);
        renderer->v_texture = create_texture(width / 2, height / 2, GL_LUMINANCE);
        
        if (renderer->u_texture == 0 || renderer->v_texture == 0) {
            LOGE("Failed to create U/V textures");
            return -1;
        }
    } else {
        // Semi-planar (NV12/NV21): interleaved UV texture
        renderer->uv_texture = create_texture(width / 2, height / 2, GL_LUMINANCE_ALPHA);
        
        if (renderer->uv_texture == 0) {
            LOGE("Failed to create UV texture");
            return -1;
        }
    }
    
    renderer->texture_width = width;
    renderer->texture_height = height;
    renderer->current_format = format;
    renderer->textures_allocated = true;
    
    LOGD("Textures allocated: %dx%d, format: %d", width, height, format);
    return 0;
}

int gl_renderer_update_texture(GLRenderer* renderer, const VideoFrame* frame) {
    if (!renderer || !frame) return -1;
    
    // Allocate textures if needed
    if (allocate_textures(renderer, frame->width, frame->height, frame->format) < 0) {
        return -1;
    }
    
    // Upload Y plane
    glBindTexture(GL_TEXTURE_2D, renderer->y_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[0]);
    
    // Upload U/V or UV plane
    if (frame->format == FRAME_FORMAT_YUV420P) {
        glBindTexture(GL_TEXTURE_2D, renderer->u_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width / 2, frame->height / 2,
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[1]);
        
        glBindTexture(GL_TEXTURE_2D, renderer->v_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width / 2, frame->height / 2,
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[2]);
    } else {
        glBindTexture(GL_TEXTURE_2D, renderer->uv_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width / 2, frame->height / 2,
                        GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, frame->data[1]);
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    renderer->stats.texture_updates++;
    return 0;
}

int gl_renderer_render_frame(GLRenderer* renderer, const VideoFrame* frame) {
    if (!renderer || !frame) return -1;
    
    // Update textures
    if (gl_renderer_update_texture(renderer, frame) < 0) {
        return -1;
    }
    
    // Select program based on format
    GLuint program = (frame->format == FRAME_FORMAT_YUV420P) ? 
                     renderer->program_yuv420p : renderer->program_nv12;
    
    glUseProgram(program);
    
    // Get uniform/attribute locations
    GLint a_position = glGetAttribLocation(program, "aPosition");
    GLint a_texcoord = glGetAttribLocation(program, "aTexCoord");
    GLint u_color_matrix = glGetUniformLocation(program, "colorMatrix");
    
    // Bind textures and set uniforms
    if (frame->format == FRAME_FORMAT_YUV420P) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer->y_texture);
        glUniform1i(glGetUniformLocation(program, "yTexture"), 0);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, renderer->u_texture);
        glUniform1i(glGetUniformLocation(program, "uTexture"), 1);
        
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, renderer->v_texture);
        glUniform1i(glGetUniformLocation(program, "vTexture"), 2);
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer->y_texture);
        glUniform1i(glGetUniformLocation(program, "yTexture"), 0);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, renderer->uv_texture);
        glUniform1i(glGetUniformLocation(program, "uvTexture"), 1);
        
        glUniform1i(glGetUniformLocation(program, "isNV21"), 
                   frame->format == FRAME_FORMAT_NV21 ? 1 : 0);
    }
    
    // Set color matrix
    glUniformMatrix3fv(u_color_matrix, 1, GL_FALSE, renderer->color_matrix);
    
    // Bind VBOs and draw
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo_vertices);
    glEnableVertexAttribArray(a_position);
    glVertexAttribPointer(a_position, 2, GL_FLOAT, GL_FALSE, 0, 0);
    
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo_texcoords);
    glEnableVertexAttribArray(a_texcoord);
    glVertexAttribPointer(a_texcoord, 2, GL_FLOAT, GL_FALSE, 0, 0);
    
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // Cleanup
    glDisableVertexAttribArray(a_position);
    glDisableVertexAttribArray(a_texcoord);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    
    // Update statistics
    renderer->stats.frames_rendered++;
    renderer->stats.last_frame_pts = frame->pts;
    
    // Calculate FPS
    renderer->fps_frame_count++;
    int64_t now = get_time_us();
    int64_t elapsed = now - renderer->last_fps_time;
    if (elapsed >= 1000000) {  // 1 second
        renderer->stats.fps = (float)renderer->fps_frame_count / (elapsed / 1000000.0f);
        renderer->fps_frame_count = 0;
        renderer->last_fps_time = now;
    }
    
    return 0;
}

void gl_renderer_clear(GLRenderer* renderer) {
    if (!renderer) return;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

GLuint gl_renderer_get_texture_id(GLRenderer* renderer) {
    return renderer ? renderer->y_texture : 0;
}

void gl_renderer_get_stats(GLRenderer* renderer, GLRendererStats* stats) {
    if (!renderer || !stats) return;
    *stats = renderer->stats;
}

void gl_renderer_reset_stats(GLRenderer* renderer) {
    if (!renderer) return;
    memset(&renderer->stats, 0, sizeof(GLRendererStats));
    renderer->last_fps_time = get_time_us();
    renderer->fps_frame_count = 0;
}

bool gl_renderer_is_ready(GLRenderer* renderer) {
    return renderer && renderer->initialized && renderer->textures_allocated;
}

int gl_renderer_set_color_matrix(GLRenderer* renderer, const float* matrix) {
    if (!renderer) return -1;
    
    if (matrix) {
        memcpy(renderer->color_matrix, matrix, sizeof(renderer->color_matrix));
    } else {
        memcpy(renderer->color_matrix, bt601_matrix, sizeof(bt601_matrix));
    }
    
    return 0;
}
