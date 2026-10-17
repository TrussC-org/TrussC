#pragma once
// Shared GLCORE / GLES3 / WebGL2 synchronous readback.
#include "TrussC.h"
#include "../tcFboConversion.h"
#if defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace trussc {

// Check the resource before any staging allocation, including a flip row.
static bool validGLReadImage(sg_image image) {
    return sg_query_image_state(image) == SG_RESOURCESTATE_VALID &&
           sg_gl_query_image_info(image).tex[0] != 0;
}

// Attach once and preserve the caller's read/draw bindings and pack layout.
// The callback runs only after the source attachment has been validated.
template<class Read>
static bool withGLReadTexture(sg_image image, bool reportError, Read read) {
    if (!validGLReadImage(image)) {
        if (reportError) tc::logError("Fbo") << "Failed to get GL texture handle";
        return false;
    }
    const GLuint texID = sg_gl_query_image_info(image).tex[0];

    GLint prevReadFbo, prevDrawFbo, packAlignment, packRowLength, packSkipRows, packSkipPixels;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);

    GLuint tempFbo;
    glGenFramebuffers(1, &tempFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texID, 0);

    bool ok = false;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        ok = read();
        if (!ok && reportError) tc::logError("Fbo") << "glReadPixels failed";

    } else {
        if (reportError) tc::logError("Fbo") << "Temporary FBO is incomplete";
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels);
    glDeleteFramebuffers(1, &tempFbo);
    return ok;
}

// Drain unrelated errors immediately before each read so only this read's
// error determines success (and whether its output is vertically flipped).
static bool readGLPixels(int y, int width, int height, GLenum format,
                         GLenum type, void* buffer) {
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, y, width, height, format, type, buffer);
    return glGetError() == GL_NO_ERROR;
}

static bool readFromGLTexture(sg_image image, int width, int height,
                              GLenum glFormat, GLenum glType,
                              void* buffer, size_t rowBytes, bool reportError = true) {
    return withGLReadTexture(image, reportError, [&] {
        if (!readGLPixels(0, width, height, glFormat, glType, buffer)) return false;
        std::vector<uint8_t> row(rowBytes);
        auto* p = static_cast<uint8_t*>(buffer);
        for (int y = 0; y < height / 2; ++y) {
            auto* top = p + size_t(y) * rowBytes;
            auto* bottom = p + size_t(height - 1 - y) * rowBytes;
            std::memcpy(row.data(), top, rowBytes);
            std::memcpy(top, bottom, rowBytes);
            std::memcpy(bottom, row.data(), rowBytes);
        }
        return true;
    });
}

// Convert in top-left row order without a whole-image staging allocation.
template<class T, class ConvertRow>
static bool readConvertedGLTexture(sg_image image, int width, int height,
                                   GLenum format, GLenum type, int channels,
                                   bool reportError, ConvertRow convert) {
    size_t rowBytes, totalBytes;
    if (!internal::fboReadbackLayout(width, height, size_t(channels) * sizeof(T),
                                    rowBytes, totalBytes)) return false;
    return withGLReadTexture(image, reportError, [&] {
        // Stage 64 rows at a time so converted staging stays small, whatever the image height.
        const int rows = std::min(height, 64);
        std::vector<T> chunk((rowBytes / sizeof(T)) * size_t(rows));
        for (int y = 0; y < height;) {
            const int count = std::min(rows, height - y);
            if (!readGLPixels(y, width, count, format, type, chunk.data())) return false;
            for (int r = 0; r < count; ++r)
                convert(height - 1 - (y + r), chunk.data() + size_t(r) * (rowBytes / sizeof(T)));
            y += count;
        }
        return true;
    });
}

bool Fbo::readPixelsPlatform(unsigned char* pixels, bool reportError) const {
    size_t rowBytes, totalBytes;
    if (!allocated_ || !pixels ||
        !internal::fboReadbackLayout(target_.width, target_.height, 4, rowBytes, totalBytes) ||
        !validGLReadImage(curColorTex_().getImage())) return false;

#if defined(SOKOL_GLES3)
    // GLES3/WebGL2 require GL_RGBA/GL_FLOAT even for R/RG float attachments.
    if (isFloatFormat(format_)) {
        return readConvertedGLTexture<float>(curColorTex_().getImage(), target_.width, target_.height,
            GL_RGBA, GL_FLOAT, 4, reportError, [&](int y, const float* row) {
                internal::fboRowToRgba8(row, pixels + size_t(y) * rowBytes,
                                       target_.width, TextureFormat::RGBA32F);
            });
    }
#endif
    // Desktop GL retains its native normalised conversion and channel expansion.
    return readFromGLTexture(curColorTex_().getImage(), target_.width, target_.height,
                             GL_RGBA, GL_UNSIGNED_BYTE, pixels, rowBytes, reportError);
}

bool Fbo::readPixelsFloatPlatform(float* pixels) const {
    const int channels = channelCount(format_);
    size_t rowBytes, totalBytes;
    if (!allocated_ || !pixels ||
        !internal::fboReadbackLayout(target_.width, target_.height, size_t(channels) * sizeof(float),
                                    rowBytes, totalBytes) ||
        !validGLReadImage(curColorTex_().getImage())) return false;
    const size_t rowElements = rowBytes / sizeof(float);

#if defined(SOKOL_GLES3)
    if (isFloatFormat(format_)) {
        return readConvertedGLTexture<float>(curColorTex_().getImage(), target_.width, target_.height,
            GL_RGBA, GL_FLOAT, 4, true, [&](int y, const float* row) {
                for (int x = 0; x < target_.width; ++x)
                    for (int c = 0; c < channels; ++c)
                        pixels[size_t(y) * rowElements + size_t(x) * channels + c] = row[size_t(x) * 4 + c];
            });
    }
    const GLenum glFormat = GL_RGBA;
    const int readChannels = 4;
#else
    const GLenum glFormat = channels == 1 ? GL_RED : channels == 2 ? GL_RG : GL_RGBA;
    if (isFloatFormat(format_) || format_ == TextureFormat::RGBA16) {
        return readFromGLTexture(curColorTex_().getImage(), target_.width, target_.height,
                                 glFormat, GL_FLOAT, pixels, rowBytes);
    }
    const int readChannels = channels;
#endif
    return readConvertedGLTexture<unsigned char>(curColorTex_().getImage(), target_.width, target_.height,
        glFormat, GL_UNSIGNED_BYTE, readChannels, true, [&](int y, const unsigned char* row) {
            for (int x = 0; x < target_.width; ++x)
                for (int c = 0; c < channels; ++c)
                    pixels[size_t(y) * rowElements + size_t(x) * channels + c] =
                        row[size_t(x) * readChannels + c] / 255.0f;
        });
}

} // namespace trussc
