#include "TrussC.h"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)

// Define prototypes for FBO functions if using legacy GL headers
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

namespace trussc {

// Helper: read from GL texture via temp FBO
static bool readFromGLTexture(sg_image image, int width, int height,
                              GLenum glFormat, GLenum glType,
                              void* buffer, size_t rowBytes, bool flipY) {
    sg_gl_image_info info = sg_gl_query_image_info(image);
    GLuint texID = info.tex[0];

    if (texID == 0) {
        tc::logError("Fbo") << "Failed to get GL texture handle";
        return false;
    }

    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    GLuint tempFbo;
    glGenFramebuffers(1, &tempFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texID, 0);

    bool ok = false;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glReadPixels(0, 0, width, height, glFormat, glType, buffer);

        // Vertical flip (OpenGL bottom-left -> TrussC top-left)
        if (flipY) {
            std::vector<uint8_t> row(rowBytes);
            uint8_t* p = (uint8_t*)buffer;
            for (int y = 0; y < height / 2; ++y) {
                uint8_t* top = p + y * rowBytes;
                uint8_t* bottom = p + (height - 1 - y) * rowBytes;
                memcpy(row.data(), top, rowBytes);
                memcpy(top, bottom, rowBytes);
                memcpy(bottom, row.data(), rowBytes);
            }
        }
        ok = true;
    } else {
        tc::logError("Fbo") << "Temporary FBO is incomplete";
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glDeleteFramebuffers(1, &tempFbo);
    return ok;
}

bool Fbo::readPixelsPlatform(unsigned char* pixels) const {
    if (!allocated_ || !pixels) return false;

    size_t rowBytes = width_ * 4;
    return readFromGLTexture(colorTexture_.getImage(), width_, height_,
                             GL_RGBA, GL_UNSIGNED_BYTE, pixels, rowBytes, true);
}

bool Fbo::readPixelsFloatPlatform(float* pixels) const {
    if (!allocated_ || !pixels) return false;

    sg_pixel_format sgFmt = colorTexture_.getPixelFormat();
    if (sgFmt == SG_PIXELFORMAT_NONE) sgFmt = SG_PIXELFORMAT_RGBA8;

    int ch = channelCount(format_);
    size_t rowBytes = width_ * bytesPerPixel(format_);

    // Determine GL format/type
    GLenum glFormat;
    switch (ch) {
        case 1: glFormat = GL_RED; break;
        case 2: glFormat = GL_RG; break;
        default: glFormat = GL_RGBA; break;
    }

    // For 32F: read directly as float
    if (sgFmt == SG_PIXELFORMAT_R32F || sgFmt == SG_PIXELFORMAT_RG32F || sgFmt == SG_PIXELFORMAT_RGBA32F) {
        return readFromGLTexture(colorTexture_.getImage(), width_, height_,
                                 glFormat, GL_FLOAT, pixels, rowBytes, true);
    }

    // For 16F: read as half-float then convert
    if (sgFmt == SG_PIXELFORMAT_R16F || sgFmt == SG_PIXELFORMAT_RG16F || sgFmt == SG_PIXELFORMAT_RGBA16F) {
        // GL can read half-float directly into float with GL_FLOAT
        return readFromGLTexture(colorTexture_.getImage(), width_, height_,
                                 glFormat, GL_FLOAT, pixels, width_ * ch * sizeof(float), true);
    }

    // U8: read as unsigned byte then convert
    std::vector<unsigned char> tmp(width_ * height_ * ch);
    if (!readFromGLTexture(colorTexture_.getImage(), width_, height_,
                           glFormat, GL_UNSIGNED_BYTE, tmp.data(), width_ * ch, true)) {
        return false;
    }
    int total = width_ * height_ * ch;
    for (int i = 0; i < total; i++) {
        pixels[i] = (float)tmp[i] / 255.0f;
    }
    return true;
}

} // namespace trussc

#endif
