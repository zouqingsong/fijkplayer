// fijk_texture.cpp
// Flutter texture wrapper implementation

#include "fijk_texture.h"
#include <cstring>

FijkTexture::FijkTexture(flutter::TextureRegistrar* registrar)
    : registrar_(registrar) {
    memset(&flutter_pixel_buffer_, 0, sizeof(flutter_pixel_buffer_));

    texture_ = std::make_unique<flutter::TextureVariant>(
        flutter::PixelBufferTexture(
            [this](size_t width, size_t height) -> const FlutterDesktopPixelBuffer* {
                return CopyPixelBuffer(width, height);
            }));

    texture_id_ = registrar_->RegisterTexture(texture_.get());
}

FijkTexture::~FijkTexture() {
    if (texture_id_ >= 0) {
        registrar_->UnregisterTexture(texture_id_);
    }
}

void FijkTexture::UpdateBuffer(const uint8_t* buffer, int width, int height) {
    if (!buffer || width <= 0 || height <= 0) return;

    std::lock_guard<std::mutex> lock(buffer_mutex_);

    int size = width * height * 4;
    if (buffer_width_ != width || buffer_height_ != height) {
        pixel_buffer_ = std::make_unique<uint8_t[]>(size);
        buffer_width_ = width;
        buffer_height_ = height;
    }

    memcpy(pixel_buffer_.get(), buffer, size);
}

void FijkTexture::MarkFrameAvailable() {
    if (texture_id_ >= 0) {
        registrar_->MarkTextureFrameAvailable(texture_id_);
    }
}

const FlutterDesktopPixelBuffer* FijkTexture::CopyPixelBuffer(size_t width, size_t height) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    if (!pixel_buffer_ || buffer_width_ <= 0 || buffer_height_ <= 0) {
        return nullptr;
    }

    flutter_pixel_buffer_.buffer = pixel_buffer_.get();
    flutter_pixel_buffer_.width = buffer_width_;
    flutter_pixel_buffer_.height = buffer_height_;

    return &flutter_pixel_buffer_;
}
