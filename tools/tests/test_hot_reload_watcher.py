"""Headless check of the host's actual watcher and source extension filter."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from check_build_warnings import run_build


class HotReloadWatcherTests(unittest.TestCase):
    def test_shader_edit_is_a_change(self):
        scan = (ROOT / 'core/include/tc/app/internal/tcHotReloadScan.h').read_text(encoding='utf-8')
        scan = scan[scan.index('inline std::vector'):scan.index('inline std::string hotReloadCMakeCommand')]
        host = (ROOT / 'core/include/tc/app/tcHotReloadHost.h').read_text(encoding='utf-8')
        watcher = host[host.index('struct FileWatcher {'):host.index('// Find cmake binary')]
        with tempfile.TemporaryDirectory(prefix='tc watcher ') as temporary:
            project = Path(temporary)
            # Only the allocation-error logger is mocked; filesystem scanning,
            # extension selection, and change detection are production code.
            (project / 'main.cpp').write_text('''
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <vector>
namespace fs = std::filesystem;
using std::string;
using std::vector;
std::ostream& logError(const char*) { return std::cerr; }
namespace internal {
''' + scan + '}\n' + watcher + '''
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const fs::path root = argv[1];
    fs::create_directories(root / "shaders");
    const auto shader = root / "shaders/probe.glsl";
    const auto ignored = root / "ignored.txt";
    std::ofstream(shader) << "// initial shader\\n";
    std::ofstream(root / "main.cpp") << "// source\\n";
    std::ofstream(ignored) << "text\\n";
    FileWatcher watcher;
    watcher.init(root.string());
    if (watcher.watchPaths.size() != 2 || watcher.hasChanges()) return 2;
    // Set mtimes explicitly: the verdict never depends on waiting or clock resolution.
    const auto editedTime = watcher.lastBuildTime + std::chrono::seconds(10);
    fs::last_write_time(ignored, editedTime);
    if (watcher.hasChanges()) return 3;
    std::ofstream(shader) << "// edited shader\\n";
    fs::last_write_time(shader, editedTime);
    if (!watcher.hasChanges()) return 4;
    watcher.lastBuildTime = fs::last_write_time(shader);
    if (watcher.hasChanges()) return 5;
    const auto added = root / "shaders/added.glsl";
    std::ofstream(added) << "// added shader\\n";
    fs::last_write_time(added, editedTime + std::chrono::seconds(10));
    watcher.rescan(root.string());
    if (watcher.watchPaths.size() != 3 || !watcher.hasChanges()) return 6;
    std::puts("PASS: shader edit and rescan trigger the host watcher; text does not");
}
''', encoding='utf-8')
            (project / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.20)
project(watcher_probe CXX)
add_executable(watcher_probe main.cpp)
target_compile_features(watcher_probe PRIVATE cxx_std_17)
''', encoding='utf-8')
            build = project / 'build'
            run_build(['cmake', '-S', str(project), '-B', str(build)], check=True)
            run_build(['cmake', '--build', str(build), '--config', 'Release'], check=True)
            binary = build / ('Release/watcher_probe.exe' if sys.platform == 'win32' else 'watcher_probe')
            if sys.platform == 'win32' and not binary.exists():
                binary = build / 'watcher_probe.exe'
            subprocess.run([str(binary), str(project / 'src')], check=True)


if __name__ == '__main__':
    unittest.main()
