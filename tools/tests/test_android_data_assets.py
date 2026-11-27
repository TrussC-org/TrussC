"""Offline staging and host execution of Android's production copy routine.

Run through tcbuild.sh; the host substitutes only the NDK asset API and logger.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AndroidDataAssetsTests(unittest.TestCase):
    def test_staging(self):
        with tempfile.TemporaryDirectory(prefix='android-data-', dir=ROOT) as scratch:
            work = Path(scratch)
            data, assets = work / 'bin/data', work / 'assets'
            def stage():
                subprocess.run(['cmake', f'-DDATA_DIR={data}', f'-DASSETS_DIR={assets}',
                                '-P', str(ROOT / 'core/cmake/tc_android_assets.cmake')], check=True)
            stage()
            self.assertEqual(list(assets.iterdir()), [])
            (data / 'nested').mkdir(parents=True)
            (data / 'nested/日本語 name.json').write_text('{}', encoding='utf-8')
            (data / 'image.png').write_bytes(b'png')
            for name in ['.gitkeep', '.DS_Store', 'backup~', '.hidden/file',
                         'nested/.hidden', 'directory~/file']:
                hidden = data / name
                hidden.parent.mkdir(parents=True, exist_ok=True)
                hidden.write_text('ignored', encoding='utf-8')
            (data / '_folder').mkdir()
            (data / '_folder/keep.txt').write_text('kept', encoding='utf-8')
            stage()
            manifest = (assets / 'trussc-data.manifest').read_text(encoding='utf-8')
            self.assertEqual(manifest.splitlines()[1:], ['_folder/keep.txt', 'image.png', 'nested/日本語 name.json'])
            self.assertEqual((assets / 'data/nested/日本語 name.json').read_bytes(), b'{}')
            stage()
            self.assertEqual((assets / 'trussc-data.manifest').read_text(encoding='utf-8'), manifest)
            (data / 'image.png').write_bytes(b'PNG')
            stage()
            self.assertNotEqual((assets / 'trussc-data.manifest').read_text(encoding='utf-8').splitlines()[0],
                                manifest.splitlines()[0])
            (data / 'image.png').unlink()
            stage()
            self.assertFalse((assets / 'data/image.png').exists())
            (data / 'nested/日本語 name.json').unlink()
            (data / '_folder/keep.txt').unlink()
            stage()
            self.assertEqual(list(assets.iterdir()), [])
            for name in ['0', 'OFF', 'FALSE']:
                (data / name).write_text('single', encoding='utf-8')
                stage()
                self.assertEqual((assets / 'trussc-data.manifest').read_text(
                    encoding='utf-8').splitlines()[1:], [name])
                (data / name).unlink()

    @unittest.skipIf(os.name == 'nt', 'POSIX fake aapt and signing tools')
    def test_packaging_dependencies(self):
        source = (ROOT / 'core/cmake/trussc_app.cmake').read_text(encoding='utf-8')
        packaging = source[source.index('            # Data changes must'):source.index('            message(STATUS "[${PROJECT_NAME}] APK auto-packaging enabled")')]
        with tempfile.TemporaryDirectory(prefix='android-apk-', dir=ROOT) as scratch:
            work = Path(scratch)
            (work / 'dummy.c').write_text('int dummy(void) { return 0; }\n', encoding='utf-8')
            # Exercise the actual packaging command and data dependencies without an SDK.
            aapt = work / 'aapt'
            aapt.write_text('#!/usr/bin/env python3\n'
                            'from pathlib import Path\nimport sys\n'
                            'assert sys.argv[sys.argv.index("--ignore-assets") + 1] == ".*:!*~"\n'
                            'assets = Path(sys.argv[sys.argv.index("-A") + 1])\n'
                            'with (assets.parent / "packages").open("a") as log:\n'
                            '    log.write("package\\n")\n', encoding='utf-8')
            aapt.chmod(0o755)
            (work / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.20)\nproject(apk_fixture C)\n'
                'add_library(apk_fixture SHARED dummy.c)\n'
                f'set(_TC_HR_TRUSSC_DIR "{ROOT}/core")\n'
                f'set(_TC_AAPT "{aapt}")\n'
                'set(_TC_ZIPALIGN /bin/true)\nset(_TC_APKSIGNER /bin/true)\n' + packaging, encoding='utf-8')
            build = work / 'build'
            subprocess.run(['cmake', '-S', str(work), '-B', str(build),
                            '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'], check=True)
            def rebuild(expected):
                subprocess.run(['cmake', '--build', str(build)], check=True)
                self.assertEqual(len((build / 'packages').read_text(encoding='utf-8').splitlines()), expected)
            rebuild(1)
            rebuild(1)
            data = work / 'bin/data'
            (data / 'nested').mkdir(parents=True)
            file = data / 'nested/日本語 name.txt'
            file.write_text('first', encoding='utf-8')
            rebuild(2)
            file.write_text('second', encoding='utf-8')
            rebuild(3)
            self.assertEqual((build / 'apk_assets/data/nested/日本語 name.txt').read_text(encoding='utf-8'), 'second')
            file.unlink()
            rebuild(4)
            self.assertFalse((build / 'apk_assets/trussc-data.manifest').exists())
            shared = work / 'shared'
            shared.mkdir()
            (data / 'shared').symlink_to(shared, target_is_directory=True)
            (shared / 'a.txt').write_text('first', encoding='utf-8')
            rebuild(5)
            (shared / 'a.txt').write_text('second', encoding='utf-8')
            rebuild(6)
            self.assertEqual((build / 'apk_assets/data/shared/a.txt').read_text(
                encoding='utf-8'), 'second')

    def test_copy(self):
        source = (ROOT / 'core/platform/android/tcPlatform_android.cpp').read_text(encoding='utf-8')
        routine = source[source.index('void internal::copyAndroidDataAssets()'):source.index('static bool immersiveMode_')]
        header = (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8')
        callback = header[header.index('inline void _setup_cb_impl()'):header.index('inline void _setup_cb(void*')]
        self.assertLess(callback.index('copyAndroidDataAssets();'), callback.index('        setup();'))
        with tempfile.TemporaryDirectory(prefix='android-copy-', dir=ROOT) as scratch:
            work = Path(scratch)
            (work / 'test.cpp').write_text(r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
namespace fs = std::filesystem;
struct AAsset { std::string bytes; size_t position = 0; bool broken = false; bool throws = false; };
struct AAssetManager { std::map<std::string, std::string> files; std::string fail, throws; int opens = 0; };
constexpr int AASSET_MODE_STREAMING = 2;
AAsset* AAssetManager_open(AAssetManager* manager, const char* name, int) {
    ++manager->opens;
    auto it = manager->files.find(name);
    return it == manager->files.end() ? nullptr : new AAsset{it->second, 0, manager->fail == name, manager->throws == name};
}
int AAsset_read(AAsset* asset, void* buffer, size_t size) {
    if (asset->throws) throw std::bad_alloc();
    if (asset->broken) return -1;
    size_t count = std::min(size, asset->bytes.size() - asset->position);
    std::memcpy(buffer, asset->bytes.data() + asset->position, count);
    asset->position += count;
    return static_cast<int>(count);
}
void AAsset_close(AAsset* asset) { delete asset; }
struct ANativeActivity { AAssetManager* assetManager; const char* internalDataPath; };
ANativeActivity activity;
void* sapp_android_get_native_activity() { return &activity; }
std::ostringstream errors, notices;
std::ostream& logError(const char*) { return errors; }
std::ostream& logNotice(const char*) { return notices; }
namespace internal { void copyAndroidDataAssets(); }
''' + routine + r'''
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
int main(int argc, char** argv) {
    assert(argc == 2);
    fs::path base = argv[1];
    std::string directory = base.string();
    AAssetManager manager;
    activity = {&manager, directory.c_str()};
    auto copy = [] { internal::copyAndroidDataAssets(); };
    copy();
    assert(!fs::exists(base / "data"));
    fs::current_path(base);
    fs::create_directory(base / "data");
    std::ofstream(base / "data/keep") << "keep";
    manager.files["trussc-data.manifest"] = "invalid-path-test\nvalid\n";
    for (const char* invalid : {static_cast<const char*>(nullptr), "", "relative"}) {
        errors.str("");
        activity.internalDataPath = invalid;
        copy();
        assert(read(base / "data/keep") == "keep");
        assert(!fs::exists(base / "relative"));
        assert(!fs::exists(base / ".trussc-data-copy"));
        assert(errors.str().find("Invalid internal storage path") != std::string::npos);
    }
    activity.internalDataPath = directory.c_str();
    const std::string name = "nested/日本語 name.json";
    manager.files = {{"trussc-data.manifest", "one\n" + name + "\nlarge.bin\nempty\n"},
                     {"data/" + name, "{}"}, {"data/large.bin", std::string(150000, 'x')},
                     {"data/empty", ""}};
    copy();
    assert(read(base / "data/.trussc-data-id") == "one\n");
    assert(read(base / "data" / name) == "{}");
    assert(read(base / "data/large.bin") == std::string(150000, 'x'));
    assert(fs::file_size(base / "data/empty") == 0);
    int opens = manager.opens;
    copy();
    assert(manager.opens == opens + 1);
    assert(notices.str().find("skipping copy") != std::string::npos);
    manager.files["trussc-data.manifest"] = "two\nnew.txt\n";
    manager.files["data/new.txt"] = "new";
    copy();
    assert(!fs::exists(base / "data/large.bin"));
    assert(read(base / "data/new.txt") == "new");
    fs::remove_all(base / "data");
    copy();
    assert(read(base / "data/.trussc-data-id") == "two\n");
    manager.files["trussc-data.manifest"] = "three\nfirst\nmissing\nlast\n";
    manager.files["data/first"] = "first";
    manager.files["data/last"] = "last";
    copy();
    assert(read(base / "data/first") == "first");
    assert(read(base / "data/last") == "last");
    assert(!fs::exists(base / "data/.trussc-data-id"));
    assert(errors.str().find("missing") != std::string::npos);
    manager.files["data/missing"] = "recovered";
    manager.fail = "data/missing";
    copy();
    assert(!fs::exists(base / "data/.trussc-data-id"));
    manager.fail.clear();
    fs::create_directory(base / ".trussc-data-copy");
    copy();
    assert(!fs::exists(base / "data/.trussc-data-id"));
    fs::remove_all(base / ".trussc-data-copy");
    copy();
    assert(read(base / "data/missing") == "recovered");
    assert(read(base / "data/.trussc-data-id") == "three\n");
    manager.files["trussc-data.manifest"] = "four\n../escape\n/absolute\nvalid\n";
    manager.files["data/valid"] = "ok";
    copy();
    assert(!fs::exists(base / "escape"));
    assert(!fs::exists(base / "data/.trussc-data-id"));
    assert(read(base / "data/valid") == "ok");
    assert(errors.str().find("../escape") != std::string::npos);
    manager.throws = "data/valid";
    copy();
    assert(!fs::exists(base / "data/.trussc-data-id"));
    manager.throws.clear();
    // An absent manifest preserves existing files.
    std::ofstream(base / "data/valid") << "ok";
    manager.files.erase("trussc-data.manifest");
    copy();
    assert(read(base / "data/valid") == "ok");
}
''', encoding='utf-8')
            binary = work / 'copy-test'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                            '-Werror', str(work / 'test.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary), str(work)], check=True)


if __name__ == '__main__':
    unittest.main()
