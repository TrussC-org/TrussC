# Scissor state recovery (#914)

`allCoreTests scissorState` checks per-window CPU state recovery and repeating
leak warnings. OnceGate has no clock seam, so it waits 1.1 seconds once.

`allCoreTests scissorState --gpu-check` also checks integer/mixed calls,
empty-pop reset and warnings on every call, Fbo readback, and an unclipped
window draw after a leaked push. Run with a display (Xvfb works on Linux).

The existing shaderScopeDummy scissor probe checks the recorded sgl scissor
immediately after beginFrame, before target selection can repair it.
