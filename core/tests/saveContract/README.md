# saveContract

Guards PR-B and its addendum for #761: SaveResult classification,
relative paths, parent creation, bundle refusal, extension policy, atomic
replacement and cleanup, and checked in-place append/FileWriter/logger writes.
The Web test enables exception catching locally for the explicit throwing-writer
case; the production Web build flags are unchanged.
Native screenshot tests check synchronous bad-path/bundle refusal and the same
checked pixel writer used by deferred captures, with exactly one Error on failure.
On Linux a real ENOSPC during encoding also preserves the previous screenshot
and removes the temporary.
A forked null-audio take hits a real mid-write file limit: the take ends itself,
logs exactly one Error (including repeated stop), and its seconds equal complete
frames in the file. Linux video first finalizes a valid take, then uses /dev/full
to require a failure from the backend and counts only accepted frames.
Web skips native recorder/readback tests; saveScreenshot remains a queued browser
download with no filesystem destination or temporary.

The test runs in allCoreTests and also as an Emscripten/Node test (`web-test`).
Linux additionally uses a private temporary symlink to `/dev/full` to produce
real ENOSPC at flush, and a forked child with RLIMIT_FSIZE to interrupt a real
regular-file write. It never replaces `/dev/full`, fills a disk or changes a
mount. Callback-writer failures and rename failure are portable. Permission
checks skip when running as root. Windows Unicode paths use `char8_t` paths.

```sh
core/tests/allCoreTests/bin/allCoreTests saveContract
```
