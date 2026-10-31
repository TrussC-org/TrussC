Run `allCoreTests captureSubmission --gpu-check [--msaa4]` with a display
(Xvfb/Mesa works on Linux). The smoke test saves a small batch through the
real capture and writer thread, decodes the files, and exits normally.
After worker drain, it requires the final `exit.png` to be decodable or its
write failure to have been reported; only then does it remove the output directory.
`allCoreTests screenshotContract` checks that closing an uncaptured window
queue completes with failure. `screenshotExtension` retains extension checks.
