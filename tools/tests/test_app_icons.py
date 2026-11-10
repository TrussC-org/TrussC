"""Exercise the production icon CMake functions with local tool stubs."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipIf(sys.platform == 'win32', 'POSIX executable tool stubs')
class AppIconTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix='trussc-icons-')
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name)
        self.source = self.base / 'project with spaces'
        self.source.mkdir()
        self.icons = self.source / 'icon'
        self.icons.mkdir()
        self.build = self.base / 'build'
        self.log = self.base / 'tools.jsonl'
        self.tool_dir = self.base / 'tools with spaces'
        self.tool_dir.mkdir()
        for tool in ('sips', 'iconutil', 'magick', 'convert'):
            script = self.tool_dir / tool
            script.write_text(f'#!{sys.executable}\n' + r'''
import json, os, pathlib, shutil, sys
args = sys.argv[1:]
name = pathlib.Path(sys.argv[0]).name
with open(os.environ['ICON_TEST_LOG'], 'a', encoding='utf-8') as log:
    log.write(json.dumps([name, *args]) + '\n')
if os.environ.get('ICON_TEST_FAIL') == name:
    sys.exit(1)
if name == 'sips':
    assert args[0] == '-z' and args[1] == args[2] and args[4] == '--out'
    shutil.copyfile(args[3], args[5])
elif name == 'iconutil':
    assert args[:2] == ['-c', 'icns'] and args[3] == '-o'
    pathlib.Path(args[4]).write_text('converted icns')
else:
    assert args[1:3] == ['-define', 'icon:auto-resize=256,128,64,48,32,16']
    shutil.copyfile(args[0], args[3])
''', encoding='utf-8')
            script.chmod(0o755)
        core = (ROOT / 'core/CMakeLists.txt').read_text(encoding='utf-8')
        functions = core[core.index('function(trussc_setup_icon '):core.index('# trussc_setup_manifest(')]
        (self.source / 'icons.cmake').write_text(functions, encoding='utf-8')
        (self.source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(IconTest NONE)
include("${CMAKE_CURRENT_SOURCE_DIR}/icons.cmake")
# Bundle post-build commands and actool require macOS. Keep selection and
# PNG conversion production code; record only the existing .icon application.
function(add_custom_command)
endfunction()
function(_trussc_apply_macos_icon_folder TARGET_NAME ICON_FOLDER OUT_SUCCESS)
    set(${OUT_SUCCESS} ${TEST_FOLDER_SUCCESS} PARENT_SCOPE)
endfunction()
add_custom_target(app)
if(TEST_PLATFORM STREQUAL "ios")
    _trussc_setup_icon_ios(app "${CMAKE_CURRENT_SOURCE_DIR}/icon" "${DEFAULT_DIR}")
elseif(TEST_PLATFORM STREQUAL "windows")
    _trussc_setup_icon_windows(app "${CMAKE_CURRENT_SOURCE_DIR}/icon" "${DEFAULT_DIR}")
else()
    _trussc_setup_icon_macos(app "${CMAKE_CURRENT_SOURCE_DIR}/icon" "${DEFAULT_DIR}")
endif()
get_target_property(ICON_FILE app MACOSX_BUNDLE_ICON_FILE)
get_target_property(ICON_SOURCES app SOURCES)
get_property(ICON_DEPENDENCIES DIRECTORY PROPERTY CMAKE_CONFIGURE_DEPENDS)
get_source_file_property(CATALOG_LOCATION "${CMAKE_BINARY_DIR}/AppIcon.xcassets" MACOSX_PACKAGE_LOCATION)
file(WRITE "${CMAKE_BINARY_DIR}/selection.txt" "${ICON_FILE}\\n${ICON_SOURCES}\\n${ICON_DEPENDENCIES}\\ncatalog=${CATALOG_LOCATION}")
''', encoding='utf-8')

    def configure(self, platform='macos', missing=(), fail='', folder=False, host_windows=False):
        env = dict(os.environ, ICON_TEST_LOG=str(self.log), ICON_TEST_FAIL=fail)
        command = ['cmake', '--no-warn-unused-cli', '-S', str(self.source), '-B', str(self.build),
                   '-G', 'Unix Makefiles', '-DCMAKE_MAKE_PROGRAM=' + shutil.which('make'),
                   '-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=FALSE',
                   '-DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=FALSE',
                   '-DTEST_PLATFORM=' + platform,
                   '-DTEST_FOLDER_SUCCESS=' + ('TRUE' if folder else 'FALSE'),
                   '-DDEFAULT_DIR=' + (ROOT / 'core/resources').as_posix()]
        for tool in ('sips', 'iconutil'):
            value = tool + '-NOTFOUND' if tool in missing else (self.tool_dir / tool).as_posix()
            command.append('-DTRUSSC_' + tool.upper() + '_EXECUTABLE=' + value)
        # Exercise production magick/convert discovery, with no host tools.
        command.extend(['-DMAGICK_EXECUTABLE:FILEPATH=MAGICK_EXECUTABLE-NOTFOUND',
                        '-DCMAKE_PROGRAM_PATH=' + ('' if 'magick' in missing else self.tool_dir.as_posix())])
        if host_windows:
            # CMake accepts a host override in this isolated project on Linux.
            command.extend(['-DCMAKE_HOST_WIN32=TRUE', '--trace-expand',
                            '--trace-source=' + str(self.source / 'icons.cmake')])
        self.env = env
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.selection = (self.build / 'selection.txt').read_text(encoding='utf-8')
        if platform == 'windows':
            self.selection += (self.build / 'app_icon.rc').read_text(encoding='utf-8')
        return result.stdout + result.stderr

    def png(self):
        # The tool stubs copy bytes; real image dimensions are checked separately.
        path = self.icons / 'custom image.png'
        path.write_bytes(b'project PNG')
        return path

    def calls(self):
        return [json.loads(line) for line in self.log.read_text(encoding='utf-8').splitlines()] if self.log.exists() else []

    def test_empty_icon_directory_silently_uses_default(self):
        output = self.configure(missing=('sips', 'iconutil'))
        self.assertNotIn('Warning', output)
        self.assertIn('default.icns', self.selection)
        self.assertEqual(self.calls(), [])
        output = self.configure('ios', missing=('sips',))
        self.assertNotIn('Warning', output)
        self.assert_ios_default()

    def assert_ios_default(self):
        catalog = self.build / 'AppIcon.xcassets/AppIcon.appiconset'
        contents = json.loads((catalog / 'Contents.json').read_text(encoding='utf-8'))
        self.assertEqual(contents['images'][0]['size'], '1024x1024')
        image = catalog / contents['images'][0]['filename']
        self.assertEqual(image.read_bytes(), (ROOT / 'core/resources/default-ios.png').read_bytes())
        self.assertEqual(struct.unpack('>II', image.read_bytes()[16:24]), (1024, 1024))

    def test_macos_png_conversion_reuse_and_changed_input(self):
        png = self.png()
        output = self.configure()
        self.assertNotIn('Warning', output)
        self.assertIn('app_icon.icns', self.selection)
        self.assertIn(png.as_posix(), self.selection)
        calls = self.calls()
        self.assertEqual(len(calls), 11)
        sizes = {f'icon_{size}x{size}{suffix}.png': str(size * scale)
                 for size in (16, 32, 128, 256, 512)
                 for scale, suffix in ((1, ''), (2, '@2x'))}
        self.assertEqual({Path(c[-1]).name: c[2] for c in calls[:-1]}, sizes)
        self.assertTrue(all(Path(c[-1]).is_relative_to(self.build) for c in calls))
        self.configure()
        self.assertEqual(self.calls(), calls)
        png.write_bytes(b'changed project PNG')
        rebuilt = subprocess.run(['cmake', '--build', str(self.build)],
                                 capture_output=True, text=True, encoding='utf-8', env=self.env)
        self.assertEqual(rebuilt.returncode, 0, rebuilt.stdout + rebuilt.stderr)
        self.assertEqual(len(self.calls()), 22)
        self.assertEqual(sorted(p.name for p in self.icons.iterdir()), [png.name])
        self.assertEqual(png.read_bytes(), b'changed project PNG')

    def png_header(self, width, height):
        # Signature + IHDR length/type + big-endian width/height; the stubs only copy bytes.
        path = self.icons / 'sized.png'
        path.write_bytes(b'\x89PNG\r\n\x1a\n' + struct.pack('>I', 13) + b'IHDR' + struct.pack('>II', width, height))
        return path

    def test_non_square_png_warns_that_it_will_be_stretched(self):
        self.png_header(300, 500)
        for platform in ('macos', 'ios', 'windows'):
            output = self.configure(platform)
            self.assertIn('sized.png is not square (300x500); it will be stretched', output)
        self.png_header(512, 512)
        for platform in ('macos', 'ios', 'windows'):
            self.assertNotIn('not square', self.configure(platform))

    def test_macos_png_reports_the_icon_it_uses(self):
        self.png()
        self.assertIn('Using icon from PNG: custom image.png', self.configure())

    def test_ios_asset_catalog_is_a_bundle_resource(self):
        self.configure('ios')
        self.assertIn('catalog=Resources', self.selection)

    def test_missing_tools_warn_and_use_default(self):
        self.png()
        for tool in ('sips', 'iconutil'):
            with self.subTest(tool=tool):
                output = self.configure(missing=(tool,))
                self.assertIn(tool + ' not found', output)
                self.assertIn('default.icns', self.selection)
        output = self.configure('ios', missing=('sips',))
        self.assertIn('sips not found', output)
        self.assert_ios_default()

    def test_failed_tools_warn_and_use_default_even_after_success(self):
        png = self.png()
        for tool in ('sips', 'iconutil'):
            with self.subTest(tool=tool):
                self.configure()
                png.write_bytes(png.read_bytes() + b'changed')
                output = self.configure(fail=tool)
                self.assertIn(tool + ' failed', output)
                self.assertIn('default.icns', self.selection)
        self.configure('ios')
        png.write_bytes(png.read_bytes() + b'changed')
        output = self.configure('ios', fail='sips')
        self.assertIn('sips failed', output)
        self.assert_ios_default()

    def test_existing_formats_keep_priority(self):
        self.png()
        icns = self.icons / 'existing.icns'
        icns.write_bytes(b'existing icns')
        self.assertNotIn('Warning', self.configure(missing=('sips', 'iconutil')))
        self.assertIn('existing.icns', self.selection)
        self.assertEqual(self.calls(), [])
        icns.unlink()
        (self.icons / 'existing.icon').mkdir()
        self.assertNotIn('Warning', self.configure(folder=True, missing=('sips', 'iconutil')))
        self.assertEqual(self.calls(), [])
        # If actool cannot apply the folder, a PNG remains available.
        self.configure(folder=False)
        self.assertIn('app_icon.icns', self.selection)

    def test_ios_resizes_custom_png_into_build_tree(self):
        png = self.png()
        self.assertNotIn('Warning', self.configure('ios'))
        call, = self.calls()
        self.assertEqual(call[:4], ['sips', '-z', '1024', '1024'])
        self.assertEqual(Path(call[4]), png)
        self.assertTrue(Path(call[-1]).is_relative_to(self.build))
        self.assertEqual(png.read_bytes(), b'project PNG')
        self.assertIn(png.as_posix(), self.selection)
        self.configure('ios')
        self.assertEqual(self.calls(), [call])
        png.write_bytes(b'changed project PNG')
        rebuilt = subprocess.run(['cmake', '--build', str(self.build)],
                                 capture_output=True, text=True, encoding='utf-8', env=self.env)
        self.assertEqual(rebuilt.returncode, 0, rebuilt.stdout + rebuilt.stderr)
        self.assertEqual(self.calls(), [call, call])
        self.assertEqual(Path(call[-1]).read_bytes(), b'changed project PNG')
        self.assertEqual(png.read_bytes(), b'changed project PNG')

    def test_windows_png_conversion_reuse_and_changed_input(self):
        png = self.png()
        output = self.configure('windows')
        ico = self.build / 'app_icon.ico'
        self.assertNotIn('Warning', output)
        self.assertIn('Using icon from PNG: custom image.png', output)
        self.assertIn(ico.as_posix(), self.selection)
        self.assertIn(png.as_posix(), self.selection)
        call, = self.calls()
        self.assertEqual(call[0], 'magick')
        self.assertEqual(Path(call[-1]), ico)
        self.assertEqual(ico.read_bytes(), png.read_bytes())
        before = ico.stat().st_mtime_ns
        self.configure('windows')
        self.assertEqual(self.calls(), [call])
        self.assertEqual(ico.stat().st_mtime_ns, before)
        png.write_bytes(b'changed project PNG')
        rebuilt = subprocess.run(['cmake', '--build', str(self.build)],
                                 capture_output=True, text=True, encoding='utf-8', env=self.env)
        self.assertEqual(rebuilt.returncode, 0, rebuilt.stdout + rebuilt.stderr)
        self.assertEqual(self.calls(), [call, call])
        self.assertEqual(ico.read_bytes(), b'changed project PNG')
        self.assertEqual(sorted(p.name for p in self.icons.iterdir()), [png.name])
        self.assertEqual(png.read_bytes(), b'changed project PNG')

    def test_windows_user_ico_wins_over_png(self):
        self.png()
        ico = self.icons / 'existing.ico'
        ico.write_bytes(b'user ico')
        output = self.configure('windows', missing=('magick',))
        self.assertNotIn('Warning', output)
        self.assertIn(ico.as_posix(), self.selection)
        self.assertEqual(self.calls(), [])
        self.assertEqual(ico.read_bytes(), b'user ico')

    def test_windows_missing_imagemagick_warns_and_uses_default(self):
        self.png()
        output = self.configure('windows', missing=('magick',))
        self.assertIn('ImageMagick (magick or convert) not found', output)
        self.assertIn('using default TrussC icon', output)
        self.assertIn((ROOT / 'core/resources/default.ico').as_posix(), self.selection)
        self.assertEqual(self.calls(), [])
        self.assertFalse((self.build / 'app_icon.ico').exists())

    def test_windows_failed_conversion_warns_and_recovers(self):
        png = self.png()
        self.configure('windows')
        png.write_bytes(b'changed project PNG')
        output = self.configure('windows', fail='magick')
        self.assertIn('magick failed to convert PNG', output)
        self.assertIn('using default TrussC icon', output)
        self.assertIn((ROOT / 'core/resources/default.ico').as_posix(), self.selection)
        self.assertFalse((self.build / 'app_icon.ico.sha256').exists())
        self.configure('windows')
        self.assertEqual(len(self.calls()), 3)
        self.assertEqual((self.build / 'app_icon.ico').read_bytes(), png.read_bytes())

    def test_windows_uses_convert_when_magick_is_missing(self):
        self.png()
        (self.tool_dir / 'magick').unlink()
        self.assertNotIn('Warning', self.configure('windows'))
        call, = self.calls()
        self.assertEqual(call[0], 'convert')

    def test_windows_host_never_searches_or_runs_convert(self):
        self.png()
        (self.tool_dir / 'magick').unlink()
        output = self.configure('windows', host_windows=True)
        self.assertRegex(output, r'find_program\(MAGICK_EXECUTABLE magick\b')
        self.assertNotRegex(output, r'find_program\(MAGICK_EXECUTABLE convert\b')
        self.assertIn('ImageMagick 7 (magick) not found', output)
        self.assertIn('using default TrussC icon', output)
        self.assertIn((ROOT / 'core/resources/default.ico').as_posix(), self.selection)
        self.assertEqual(self.calls(), [])
        self.assertFalse((self.build / 'app_icon.ico').exists())


if __name__ == '__main__':
    unittest.main()
