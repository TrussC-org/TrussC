# Android Back and quit control flow

Standalone headless unit test; no Android SDK, display or TrussC runtime is linked.
CMake extracts the production Android event, cleanup, frame callback and loop
functions from `sokol_app_tc.h` into the build tree. Deterministic native seams
exercise consumed / unconsumed / canceled Back key-up, request veto, ordered quit,
cleanup after Activity destroy, and both fallback and Choreographer paths.
The shared production singleton event gate verifies consumption reset.
The API 26 preprocessor branch is compiled separately.

This does not replace NDK compilation or Android device checks of Activity,
EGL, screen pinning, Back gestures and predictive-Back manifest configuration.
