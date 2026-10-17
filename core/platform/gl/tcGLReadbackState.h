#pragma once
// Native GL/GLES readback state, after the target coordinator has parked the
// encoder. No capture source lookup or resolve occurs in this adapter.
namespace trussc::internal {
class GLReadbackState {
public:
    GLReadbackState() {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_);
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment_);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength_);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows_);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels_);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer_);
#if defined(SOKOL_GLCORE)
        glGetIntegerv(GL_PACK_SWAP_BYTES, &swapBytes_);
        glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
#endif
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    }
    ~GLReadbackState() {
        if (sourceSelected_) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, source_);
            glReadBuffer((GLenum)sourceReadBuffer_);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw_);
        glPixelStorei(GL_PACK_ALIGNMENT, alignment_);
        glPixelStorei(GL_PACK_ROW_LENGTH, rowLength_);
        glPixelStorei(GL_PACK_SKIP_ROWS, skipRows_);
        glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels_);
#if defined(SOKOL_GLCORE)
        glPixelStorei(GL_PACK_SWAP_BYTES, swapBytes_);
#endif
        glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)packBuffer_);
        if (temporary_) glDeleteFramebuffers(1, &temporary_);
    }
    void bindSource(GLuint framebuffer) {
        source_ = framebuffer;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, source_);
        glGetIntegerv(GL_READ_BUFFER, &sourceReadBuffer_);
        sourceSelected_ = true;
        glReadBuffer(source_ ? GL_COLOR_ATTACHMENT0 : GL_BACK);
    }
    void bindTexture(GLuint texture) {
        glGenFramebuffers(1, &temporary_);
        glBindFramebuffer(GL_FRAMEBUFFER, temporary_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    }
    GLReadbackState(const GLReadbackState&) = delete;
    GLReadbackState& operator=(const GLReadbackState&) = delete;
private:
    GLint read_ = 0, draw_ = 0, alignment_ = 0, rowLength_ = 0;
    GLint skipRows_ = 0, skipPixels_ = 0, packBuffer_ = 0;
#if defined(SOKOL_GLCORE)
    GLint swapBytes_ = 0;
#endif
    GLuint source_ = 0, temporary_ = 0;
    GLint sourceReadBuffer_ = 0;
    bool sourceSelected_ = false;
};
} // namespace trussc::internal
