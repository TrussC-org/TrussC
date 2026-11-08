"""Native read validation and Web declaration exclusion using the real header."""
import unittest
import test_compute_shader as compute_tests


class StorageBufferReadTests(unittest.TestCase):
    compile_and_run = compute_tests.ComputeShaderTests.compile_and_run

    def test_validation_and_native_failure(self):
        source = r'''
#include <new>
namespace trussc {
int nativeCalls = 0;
bool StorageBuffer::readPlatform(void*, size_t) { ++nativeCalls; throw std::bad_alloc(); }
}
#undef sg_query_features
int main() {
    using namespace trussc;
    StorageBuffer buffer;
    int output = 42;
    sg_desc setup{}; sg_setup(&setup); _sg.features.compute = true;
    if (!buffer.allocate(16) || !sg_query_buffer_usage(buffer.getBuffer()).copy_src) return 2;
    warnings.str("");
    if (buffer.read(&output, 17) || buffer.read(&output, 17) || nativeCalls) return 3;
    // Rate-limited like the other StorageBuffer warnings (read() is often per frame).
    const auto messages = warnings.str();
    const auto first = messages.find("read requires");
    if (first == std::string::npos || messages.find("read requires", first + 1) != std::string::npos) return 4;
    StorageBuffer empty;
    if (!empty.read(nullptr, 0) || empty.read(&output, 4) || output != 42 || nativeCalls) return 1;
    if (buffer.read(nullptr, 4) || nativeCalls || internal::suspensions) return 5;
    if (buffer.read(&output, 4) || output != 42 || nativeCalls != 1 || internal::suspensions != 1) return 6;
    buffer.clear(); sg_shutdown();
}
'''
        self.compile_and_run(source, True, main='')

    def test_web_has_no_read_declaration(self):
        source = r'''
template<class T> concept HasRead = requires(T& buffer, void* dst) { buffer.read(dst, size_t(4)); };
static_assert(!HasRead<trussc::StorageBuffer>);
int main() {}
'''
        for backend in ('__EMSCRIPTEN__', 'SOKOL_WGPU'):
            with self.subTest(backend=backend):
                # Define the platform switch after the dummy sokol implementation;
                # the public StorageBuffer header sees the actual API guard.
                preamble = compute_tests.PREAMBLE.replace(
                    '#include "tc/gpu/tcStorageBuffer.h"',
                    f'#define {backend}\n#include "tc/gpu/tcStorageBuffer.h"')
                self.compile_and_run(source, True, main='', preamble=preamble)


if __name__ == '__main__':
    unittest.main()
