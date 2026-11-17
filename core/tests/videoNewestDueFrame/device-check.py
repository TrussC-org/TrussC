#!/usr/bin/env python3
"""Compare base/fix 4 x 4K CUDA or VAAPI playback on a Linux desktop.

Uses an existing benchmark checkout and cached clip, without downloads or git
writes. Run from the reviewed TrussC checkout with its desktop DISPLAY.
"""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tarfile


def traced_player(source):
    """Sample actual copies, independently of position reported after a seek."""
    source = source.replace('class TCVideoPlayerImpl {',
        'static std::atomic<int> sampleNextId{0};\nclass TCVideoPlayerImpl {', 1)
    source = source.replace('    TCVideoPlayerImpl() = default;', '''    TCVideoPlayerImpl() {
        std::string path = std::string(std::getenv("VIDEO_SAMPLE_DIR")) +
            "/player-" + std::to_string(sampleNextId++) + ".csv";
        sampleFile_ = std::fopen(path.c_str(), "w");
        if (sampleFile_) std::fprintf(sampleFile_, "wall,clock,presented_pts,presented_count,seek_count\\n");
    }''')
    source = source.replace('~TCVideoPlayerImpl() { close(); }',
        '~TCVideoPlayerImpl() { close(); if (sampleFile_) std::fclose(sampleFile_); }')
    source = source.replace('    bool loadAudioForPlayback();', '''    FILE* sampleFile_ = nullptr;
    double sampleStart_ = 0, samplePresented_ = 0;
    uint64_t sampleFrames_ = 0;
    std::atomic<uint64_t> sampleSeeks_{0};
    bool loadAudioForPlayback();''', 1)
    source = source.replace('void TCVideoPlayerImpl::play() {',
        'void TCVideoPlayerImpl::play() {\n    sampleStart_ = av_gettime_relative() / 1000000.0;', 1)
    source = re.sub(r'(currentPts_\s*=\s*(front|newest)\.pts;)',
        r'\1 samplePresented_ = \2.pts;', source)
    marker = '    // Hard re-sync if drift' if '// Hard re-sync if drift' in source else '    // Publish the selected clock'
    pos = source.index(marker)
    source = source[:pos] + '''    auto samplePoll = [&] {
        if (!sampleFile_) return;
        if (hasNewFrame_) ++sampleFrames_;
        double wall = av_gettime_relative() / 1000000.0 - sampleStart_;
        // Stay before the cached five-second clip's first loop boundary.
        if (wall <= 4.5) std::fprintf(sampleFile_, "%.6f,%.6f,%.6f,%llu,%llu\\n",
            wall, targetPts, samplePresented_, (unsigned long long)sampleFrames_,
            (unsigned long long)sampleSeeks_.load());
    };
    struct SampleOnExit { decltype(samplePoll)& fn; ~SampleOnExit() { fn(); } } sampleExit{samplePoll};
''' + source[pos:]
    if '    ++seekCount_;' in source:
        source = source.replace('    ++seekCount_;', '    ++seekCount_;\n    ++sampleSeeks_;')
    else:
        source = source.replace('            av_seek_frame(formatCtx_, videoStreamIndex_, timestamp, AVSEEK_FLAG_BACKWARD);',
            '            ++sampleSeeks_;\n            av_seek_frame(formatCtx_, videoStreamIndex_, timestamp, AVSEEK_FLAG_BACKWARD);')
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark', type=Path, required=True)
    parser.add_argument('--base', required=True)
    parser.add_argument('--backend', choices=('cuda', 'vaapi'), required=True)
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    args = parser.parse_args()
    root = Path.cwd()
    evidence = root / '.verification/videoNewestDueFrame-device' / args.backend
    evidence.mkdir(parents=True, exist_ok=True)
    clip = Path.home() / '.cache/trussc-benchmark/plasma_3840x2160_30fps_150f_h264_24000k.mp4'
    if not clip.is_file():
        parser.error(f'cached clip required: {clip}')
    env = os.environ.copy()
    if args.backend == 'vaapi':
        env['CUDA_VISIBLE_DEVICES'] = ''
        subprocess.run(['vainfo'], env=env, check=True)
    base = evidence / 'base'
    base.mkdir(exist_ok=True)
    archive = subprocess.check_output(['git', 'archive', args.base])
    with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
        tree.extractall(base, filter='data')
    if not (base / '.build').exists():
        (base / '.build').symlink_to(root / '.build', target_is_directory=True)
    base_build = root / '.build' / f'video-device-{args.backend}-base-core'
    base_build.mkdir(parents=True, exist_ok=True)
    if not (base / 'core/build-linux').exists():
        (base / 'core/build-linux').symlink_to(base_build, target_is_directory=True)
    compiler = root / 'core/tools/sokol-shdc/sokol-shdc'
    recipe = (base / 'core/cmake/trussc_shaders.cmake').read_text(encoding='utf-8')
    expected = re.search(r'_TC_SOKOL_SHDC_SHA256_linux "([a-f0-9]+)"', recipe)[1]
    if hashlib.sha256(compiler.read_bytes()).hexdigest() != expected:
        parser.error('base shader pin differs; supply the matching local binary before configuring')
    shdc = base / 'core/tools/sokol-shdc/sokol-shdc'
    shdc.parent.mkdir(parents=True, exist_ok=True)
    if not shdc.exists():
        shdc.symlink_to(compiler)
    bench = evidence / 'bench'
    for entry in ('CMakeLists.txt', 'cmake', 'src', 'shaders'):
        source = args.benchmark.resolve() / entry
        if source.is_dir():
            shutil.copytree(source, bench / entry, dirs_exist_ok=True)
        else:
            bench.mkdir(exist_ok=True)
            shutil.copy2(source, bench / entry)
    scene = bench / 'src/scenes/VideoPlay10.cpp'
    text = scene.read_text(encoding='utf-8')
    old = 'VideoPlay(10, media::ClipSpec{1920, 1080, 30, 300, 8000000}'
    if old not in text:
        parser.error('benchmark scene changed; review the four-player adaptation')
    scene.write_text(text.replace(old, 'VideoPlay(4, media::ClipSpec{3840, 2160, 30, 150, 24000000}'), encoding='utf-8')
    shared = bench / 'src/scenes/shared/VideoPlay.h'
    text = shared.read_text(encoding='utf-8').replace('        players_.clear();', '''        for (const auto& slot : players_)
            std::fprintf(stderr, "DEVICE_BACKEND %s\\n", slot->player.getHwAccelName().c_str());
        players_.clear();''')
    shared.write_text(text, encoding='utf-8')
    cmake = bench / 'CMakeLists.txt'
    text = cmake.read_text(encoding='utf-8').replace('trussc_app(NAME trussc-benchmark)', '''file(GLOB device_runner "${CMAKE_CURRENT_SOURCE_DIR}/src/bench/*.cpp")
trussc_app(NAME trussc-benchmark SOURCES src/main.cpp src/tcApp.cpp ${device_runner} src/scenes/VideoPlay10.cpp)''')
    cmake.write_text(text + '''
get_target_property(device_sources TrussC SOURCES)
list(REMOVE_ITEM device_sources "${TRUSSC_DIR}/platform/linux/tcVideoPlayer_linux.cpp")
set_property(TARGET TrussC PROPERTY SOURCES "${device_sources}")
target_sources(TrussC PRIVATE "${VIDEO_SAMPLE_SOURCE}")
set_source_files_properties("${VIDEO_SAMPLE_SOURCE}" DIRECTORY "${TRUSSC_DIR}"
    PROPERTIES INCLUDE_DIRECTORIES "${TRUSSC_DIR}/platform/linux")
''', encoding='utf-8')
    wrapper = Path(os.environ.get('TCBUILD', str(Path.home() / 'TrussC-dev/trussc-ai-tools/tcbuild.sh')))
    binaries = {}
    for variant, checkout in (('base', base), ('fix', root)):
        sampled = evidence / f'{variant}-player.cpp'
        sampled.write_text(traced_player((checkout / 'core/platform/linux/tcVideoPlayer_linux.cpp').read_text(encoding='utf-8')), encoding='utf-8')
        build = root / '.build' / f'video-device-{args.backend}-{variant}'
        with (evidence / f'build-{variant}.log').open('w', encoding='utf-8') as log:
            for command in (
                ['cmake', '-S', str(bench), '-B', str(build), '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
                 f'-DTRUSSC_DIR={checkout / "core"}', '-DTCB_TC_VERSION_OVERRIDE=0.8.1', f'-DVIDEO_SAMPLE_SOURCE={sampled}'],
                ['cmake', '--build', str(build), '--target', 'trussc-benchmark'],
            ):
                subprocess.run([str(wrapper)] + command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        binaries[variant] = build / 'bin/trussc-benchmark'
    results = {}
    for run in range(1, 4):
        for variant in ('base', 'fix'):
            case = evidence / f'{variant}-{run}'
            case.mkdir(exist_ok=True)
            env['VIDEO_SAMPLE_DIR'] = str(case)
            command = ['taskset', '-c', str(args.cpu), str(binaries[variant]), '--scene', 'media.video_play_10', '--warmup', '1', '--out', str(case)]
            loads = []
            load_before = os.getloadavg()
            try:
                for _ in range(4):
                    loads.append(subprocess.Popen(['taskset', '-c', str(args.cpu), 'python3', '-c', 'while True: pass']))
                (case / 'command.json').write_text(json.dumps({'command': command, 'load_pids': [p.pid for p in loads], 'load_before': load_before}), encoding='utf-8')
                with (case / 'console.log').open('w', encoding='utf-8') as log:
                    subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120, check=True)
            finally:
                for process in loads:
                    process.terminate()
                for process in loads:
                    process.wait()
            backends = re.findall(r'DEVICE_BACKEND (\S+)', (case / 'console.log').read_text(encoding='utf-8'))
            if len(backends) != 4 or any(name != args.backend for name in backends):
                raise SystemExit(f'{args.backend} UNVERIFIED: requested backend not active on all four players')
            players = {}
            for sample in case.glob('player-*.csv'):
                with sample.open(encoding='utf-8') as stream:
                    rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(stream)]
                players[sample.stem] = {'lag_median': statistics.median(r['clock'] - r['presented_pts'] for r in rows),
                    'presented_fps': rows[-1]['presented_count'] / 4.5, 'seek_count': rows[-1]['seek_count']}
            results[f'{variant}-{run}'] = players
            print(variant, run, json.dumps(players), flush=True)
    (evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
