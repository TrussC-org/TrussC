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
