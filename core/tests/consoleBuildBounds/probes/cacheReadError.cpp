// Emscripten/Node libc++ regression: inject EIO after a valid command prefix.
#include <emscripten.h>
#include "tc/app/internal/tcHotReloadScan.h"
namespace trussc {
Logger& getLogger() { static Logger logger; return logger; }
std::thread::id Thread::getMainThreadId() { return std::this_thread::get_id(); }
namespace internal {
bool isLogNonBlocking() { return false; }
// Cache/log delivery is synchronous; asynchronous event admission is outside this probe.
std::shared_ptr<ExternalDeliveryStore::Lease> ExternalDeliveryStore::reserve(size_t, size_t) { std::abort(); }
}
}
int main() {
    EM_ASM({
        const content = 'CMAKE_COMMAND:INTERNAL=/usr/bin/cmake\n';
        FS.writeFile('/cache', content + 'x'.repeat(8192));
        const node = FS.lookupPath('/cache').node;
        const original = node.stream_ops;
        node.stream_ops = Object.assign({}, original, {
            read: function(stream, buffer, offset, length, position) {
                if (position >= content.length) throw new FS.ErrnoError(29);
                return original.read(stream, buffer, offset, Math.min(length, content.length - position), position);
            }
        });
    });
    const auto command = trussc::internal::hotReloadCMakeCommand("/cache");
    std::printf("injected EIO: accepted='%s'\n", command.c_str());
    return command.empty() ? 0 : 1;
}
