// fijk_texture.h
// Flutter texture wrapper for pixel buffer rendering on desktop

#ifndef FIJK_TEXTURE_H
#define FIJK_TEXTURE_H

#include <flutter/texture_registrar.h>
#include <memory>
#include <mutex>
#include <cstdint>

class FijkTexture {
public:
    FijkTexture(flutter::TextureRegistrar* registrar);
    ~FijkTexture();

    int64_t texture_id() const { return texture_id_; }

    // Update the pixel buffer (called from player thread)
    void UpdateBuffer(const uint8_t* buffer, int width, int height);

    // Mark frame available (triggers Flutter repaint)
    void MarkFrameAvailable();

private:
    flutter::TextureRegistrar* registrar_;
    std::unique_ptr<flutter::TextureVariant> texture_;
    int64_t texture_id_ = -1;

    std::mutex buffer_mutex_;
    std::unique_ptr<uint8_t[]> pixel_buffer_;
    int buffer_width_ = 0;
    int buffer_height_ = 0;

    FlutterDesktopPixelBuffer flutter_pixel_buffer_;

    const FlutterDesktopPixelBuffer* CopyPixelBuffer(size_t width, size_t height);
};

#endif // FIJK_TEXTURE_H
