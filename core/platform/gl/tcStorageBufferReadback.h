#pragma once
// Shared native GL 4.3 / GLES 3.1 storage readback (not WebGL).
#include "TrussC.h"
#if defined(SOKOL_GLES3)
#include <GLES3/gl31.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace trussc {
namespace internal {
namespace {
// Copy bindings are independent of indexed storage bindings used by shaders.
struct StorageCopyBindings {
    GLint read = 0, write = 0;
    StorageCopyBindings() {
        glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &read);
        glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &write);
    }
    ~StorageCopyBindings() {
        glBindBuffer(GL_COPY_READ_BUFFER, GLuint(read));
        glBindBuffer(GL_COPY_WRITE_BUFFER, GLuint(write));
        sg_reset_state_cache();
    }
};
class GLBufferReadback final : public BufferReadbackRequest {
public:
    explicit GLBufferReadback(size_t bytes) : bytes_(bytes) {}
    ~GLBufferReadback() override {
        // GL defers deletion of resources still used by submitted commands.
        if (fence) glDeleteSync(fence);
        if (staging) glDeleteBuffers(1, &staging);
    }
    bool poll() override {
        const auto status = glClientWaitSync(fence, 0, 0);
        if (status == GL_TIMEOUT_EXPIRED) return false;
        if (status == GL_WAIT_FAILED) return true;
        StorageCopyBindings bindings;
        while (glGetError() != GL_NO_ERROR) {}
        glBindBuffer(GL_COPY_READ_BUFFER, staging);
        const void* mapped = glMapBufferRange(GL_COPY_READ_BUFFER, 0, GLsizeiptr(bytes_.size()), GL_MAP_READ_BIT);
        bool ok = mapped && glGetError() == GL_NO_ERROR;
        if (mapped) {
            if (ok) std::memcpy(bytes_.data(), mapped, bytes_.size());
            ok = glUnmapBuffer(GL_COPY_READ_BUFFER) == GL_TRUE && ok;
        }
        if (glGetError() == GL_NO_ERROR && ok) result = ReadbackResult(std::move(bytes_));
        return true;
    }
    GLuint staging = 0;
    GLsync fence = nullptr;
private:
    std::vector<unsigned char> bytes_;
};
} // namespace
} // namespace internal

std::unique_ptr<internal::BufferReadbackRequest> StorageBuffer::readAsyncPlatform() {
    const auto info = sg_gl_query_buffer_info(buffer_);
    if (!info.buf[info.active_slot]) return {};
    auto request = std::make_unique<internal::GLBufferReadback>(size_);
    internal::StorageCopyBindings bindings;
    while (glGetError() != GL_NO_ERROR) {}
    glGenBuffers(1, &request->staging);
    glBindBuffer(GL_COPY_WRITE_BUFFER, request->staging);
    glBufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(size_), nullptr, GL_STREAM_READ);
    glBindBuffer(GL_COPY_READ_BUFFER, info.buf[info.active_slot]);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, GLsizeiptr(size_));
    request->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    if (!request->staging || !request->fence || glGetError() != GL_NO_ERROR) return {};
    return request;
}

bool StorageBuffer::readPlatform(void* destination, size_t bytes) {
    const auto info = sg_gl_query_buffer_info(buffer_);
    if (!info.buf[info.active_slot]) return false;
    // Read into owned memory so GL errors, including unmap failure, cannot
    // partially overwrite the caller's destination.
    std::vector<unsigned char> snapshot(bytes);
    while (glGetError() != GL_NO_ERROR) {}
    GLint previous = 0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &previous);
    glBindBuffer(GL_COPY_READ_BUFFER, info.buf[info.active_slot]);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
#if defined(SOKOL_GLES3)
    const void* mapped = glMapBufferRange(GL_COPY_READ_BUFFER, 0, GLsizeiptr(bytes), GL_MAP_READ_BIT);
    bool ok = mapped != nullptr && glGetError() == GL_NO_ERROR;
    if (mapped) {
        if (ok) std::memcpy(snapshot.data(), mapped, bytes);
        ok = glUnmapBuffer(GL_COPY_READ_BUFFER) == GL_TRUE && ok;
    }
#else
    glGetBufferSubData(GL_COPY_READ_BUFFER, 0, GLsizeiptr(bytes), snapshot.data());
    bool ok = glGetError() == GL_NO_ERROR;
#endif
    ok = glGetError() == GL_NO_ERROR && ok;
    glBindBuffer(GL_COPY_READ_BUFFER, GLuint(previous));
    sg_reset_state_cache();
    if (ok) std::memcpy(destination, snapshot.data(), bytes);
    return ok;
}
} // namespace trussc
