Run `allCoreTests captureSubmission --gpu-check [--msaa4]` with a display
(Xvfb/Mesa works on Linux). The smoke test saves a small batch through the
real capture and writer thread, decodes the files, and exits normally.
After worker drain, it requires the final `exit.png` to be decodable or its
write failure to have been reported; only then does it remove the output directory.
Teardown may cancel this last request before capture. A failure completion, or
an Error naming `exit.png` when its completion cannot reach the ended run, meets
that contract. The five earlier captures must still complete and decode.
`allCoreTests screenshotContract` checks that closing an uncaptured window
queue completes with failure. `screenshotExtension` retains extension checks.
