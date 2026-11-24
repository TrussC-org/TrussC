# Input replay tests

Run `tcbuild.sh build_all --addon-tests-only --addons tcxInputReplay` from the repository root. The console suite validates JSONL v1, coordinate scaling, scene timing, ring export, and the Node toolbar with a virtual native window.

The display suite uses the same Node checks in a real window. Run `tests-display/bin/tests-display --display-check` with a private display server. Add `--shots <absolute-directory>` to capture idle, recording, playing, and ring-recording states. Playback waits at a mark while its screenshot is taken.
