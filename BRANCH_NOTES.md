# feat: experiment with trusscli share on a branch

This branch adds `trusscli share` and `trusscli share --unshare`. It builds the existing Web target, previews or captures a native screenshot, confirms the upload, publishes one orphan commit to gh-pages, and offers a separate gallery listing. `.trussc-share` protects branches created by hand, and every push uses an explicit lease on the inspected commit (or an absent branch). Unshare removes the listing while keeping the app online.

This is a branch-only experiment. Do not open a PR to main. Publishing has only been exercised with offline fixtures, and native auto-capture was verified on Linux Xvfb; no real GitHub repo, Pages configuration, or gallery was changed.

Follow-up validation: all 28 share tests and 3 help tests pass with the rebuilt CLI. The full tools sweep passes 81 of 84 scripts; the remaining three reproduce origin/main's read-only Emscripten cache failures. One real Linux Xvfb capture configured/built in scratch and preserved the normal native cache/bin byte for byte. The PNG was inspected. Mac and Windows execution remain unverified.

## Try the branch on a Mac

Build this branch's trusscli with the normal Mac tools build procedure. Use a separate, public GitHub app repository with a GitHub origin, Emscripten already installed, and GitHub CLI authentication:

```sh
gh auth status
cd path/to/myApp
trusscli update --web
trusscli share
```

Run these publish commands only when the owner starts the online trial. The command lists all outgoing files and asks before pushing. Answer `n` at that prompt to try the build and screenshot preview without publishing. Native auto-capture temporarily builds with MCP development tools, launches with `TRUSSC_MCP=1`, calls the existing localhost HTTP MCP screenshot tool, and quits the app. Check the OS viewer, fade-ins, retake, and skip; camera/microphone permission dialogs may require skip. No TrussC API grants those OS permissions. An accepted capture becomes `screenshot.png` in the project root.

Auto-capture builds its MCP development binary in a temporary build directory and removes it after sharing. It links to the project's `bin/data` for runtime assets. The project's normal native build directory, cache, and binary are preserved.

The publish preview comes from the staged Git tree, including files ignored by global Git rules. It includes only the exact HTML (renamed to `index.html`), JS, WASM, and optional data products, plus the share marker and selected screenshot. A separate “Bundled into” list shows the `bin/data` inputs with sizes and symlink targets before confirmation.

If Pages already serves another branch/path or uses a workflow, share stops before offering listing and prints a command to select `gh-pages:/`. It leaves that setting for the owner to change. If another writer changes gh-pages during share, the push stops; run `trusscli share` again to inspect and approve the current state.

Answer the separate listing prompt after publishing. To withdraw the listing later:

```sh
trusscli share --unshare
```

The first Pages deploy takes about a minute. Pthread/SharedArrayBuffer Web builds require headers that GitHub Pages cannot provide; share stops for these outputs. No service worker is added.
