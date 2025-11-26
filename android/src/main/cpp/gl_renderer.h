/**
 * OpenGL ES Renderer - Hardware-accelerated video rendering
 * 
 * Features:
 * - YUV to RGB conversion using shaders (GPU-accelerated)
 * - Support for multiple YUV formats (YUV420P, NV12, NV21)
 * - Texture upload and rendering
 * - Flutter texture registry integration
 * - Aspect ratio handling and scaling
 */

#ifndef FIJKPLAYER_GL_RENDERER_H
#define FIJKPLAYER_GL_RENDERER_H

#include "frame_queue.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Renderer configuration
typedef struct {
    int texture_width;        // Texture width (power of 2 not required in ES 2.0+)
    int texture_height;       // Texture height
    bool maintain_aspect;     // Maintain aspect ratio when scaling
    bool flip_vertical;       // Flip Y coordinate (for different coordinate systems)
} GLRendererConfig;

// Render statistics
typedef struct {
    int frames_rendered;      // Total frames rendered
    int frames_dropped;       // Frames dropped due to timing
    int64_t last_frame_pts;   // PTS of last rendered frame
    float fps;                // Current FPS
    int texture_updates;      // Number of texture uploads
} GLRendererStats;

// Opaque renderer handle
typedef struct GLRenderer GLRenderer;

/**
 * Create OpenGL ES renderer
 * Must be called on a thread with an active EGL context
 * 
 * @param config Renderer configuration (NULL for defaults)
 * @return Renderer handle, or NULL on error
 */
GLRenderer* gl_renderer_create(const GLRendererConfig* config);

/**
 * Destroy renderer and free resources
 * 
 * @param renderer Renderer to destroy
 */
void gl_renderer_destroy(GLRenderer* renderer);

/**
 * Set the output surface size (viewport)
 * Call this when the surface size changes
 * 
 * @param renderer Renderer handle
 * @param width Surface width in pixels
 * @param height Surface height in pixels
 * @return 0 on success, -1 on error
 */
int gl_renderer_set_surface_size(GLRenderer* renderer, int width, int height);

/**
 * Render a video frame to the current OpenGL context
 * Uploads YUV data to textures and renders with shader conversion
 * 
 * @param renderer Renderer handle
 * @param frame Video frame to render
 * @return 0 on success, -1 on error
 */
int gl_renderer_render_frame(GLRenderer* renderer, const VideoFrame* frame);

/**
 * Clear the render surface (black screen)
 * 
 * @param renderer Renderer handle
 */
void gl_renderer_clear(GLRenderer* renderer);

/**
 * Update texture data without rendering
 * Useful for Flutter texture registry integration
 * 
 * @param renderer Renderer handle
 * @param frame Video frame to upload
 * @return 0 on success, -1 on error
 */
int gl_renderer_update_texture(GLRenderer* renderer, const VideoFrame* frame);

/**
 * Get the OpenGL texture ID for Flutter integration
 * 
 * @param renderer Renderer handle
 * @return OpenGL texture ID (0 if not available)
 */
GLuint gl_renderer_get_texture_id(GLRenderer* renderer);

/**
 * Get renderer statistics
 * 
 * @param renderer Renderer handle
 * @param stats Output statistics
 */
void gl_renderer_get_stats(GLRenderer* renderer, GLRendererStats* stats);

/**
 * Reset renderer statistics
 * 
 * @param renderer Renderer handle
 */
void gl_renderer_reset_stats(GLRenderer* renderer);

/**
 * Check if renderer is ready for rendering
 * 
 * @param renderer Renderer handle
 * @return true if ready
 */
bool gl_renderer_is_ready(GLRenderer* renderer);

/**
 * Set YUV color conversion matrix
 * Supports different YUV color spaces (BT.601, BT.709, etc.)
 * 
 * @param renderer Renderer handle
 * @param matrix 3x3 conversion matrix (NULL for BT.601 default)
 * @return 0 on success, -1 on error
 */
int gl_renderer_set_color_matrix(GLRenderer* renderer, const float* matrix);

#ifdef __cplusplus
}
#endif

#endif // FIJKPLAYER_GL_RENDERER_H
