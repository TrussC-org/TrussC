#pragma once

// Include after the platform's GL/GLES3 declarations.
namespace trussc::internal {

class ScopedTightPixelPack {
public:
    ScopedTightPixelPack() {
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment_);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength_);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows_);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels_);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer_);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    ~ScopedTightPixelPack() {
        glPixelStorei(GL_PACK_ALIGNMENT, alignment_);
        glPixelStorei(GL_PACK_ROW_LENGTH, rowLength_);
        glPixelStorei(GL_PACK_SKIP_ROWS, skipRows_);
        glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels_);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer_);
    }
    ScopedTightPixelPack(const ScopedTightPixelPack&) = delete;
    ScopedTightPixelPack& operator=(const ScopedTightPixelPack&) = delete;
private:
    GLint alignment_, rowLength_, skipRows_, skipPixels_, buffer_;
};

} // namespace trussc::internal
