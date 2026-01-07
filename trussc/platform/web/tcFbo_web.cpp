#include "TrussC.h"

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>

namespace trussc {

bool Fbo::readPixelsPlatform(unsigned char* pixels) const {
    if (!allocated_ || !pixels) return false;

    // Get GL texture handle from sokol image
    sg_gl_image_info info = sg_gl_query_image_info(colorTexture_.getImage());
    GLuint texID = info.tex[0];

    if (texID == 0) {
        logError("Fbo") << "Failed to get GL texture handle";
        return false;
    }

    // Save current FBO binding to restore later
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    // Create a temporary FBO for reading
    GLuint tempFbo;
    glGenFramebuffers(1, &tempFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, tempFbo);

    // Attach the texture to the temporary FBO
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texID, 0);

    // Check FBO status
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        // Read pixels
        // TrussC/Sokol standard is RGBA8
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        
        // Note: OpenGL origin is bottom-left, TrussC is top-left.
        // Vertical flip to match TrussC/Image coordinate system
        int rowSize = width_ * 4;
        unsigned char* row = new unsigned char[rowSize];
        for (int y = 0; y < height_ / 2; ++y) {
            unsigned char* top = pixels + y * rowSize;
            unsigned char* bottom = pixels + (height_ - 1 - y) * rowSize;
            memcpy(row, top, rowSize);
            memcpy(top, bottom, rowSize);
            memcpy(bottom, row, rowSize);
        }
        delete[] row;
    } else {
        logError("Fbo") << "Temporary FBO is incomplete";
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glDeleteFramebuffers(1, &tempFbo);
        return false;
    }

    // Restore state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glDeleteFramebuffers(1, &tempFbo);

    return true;
}

} // namespace trussc

#endif
