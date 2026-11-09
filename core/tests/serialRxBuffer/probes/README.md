These Linux-only probes need separate processes for static teardown and malloc/new interposition. Build allCoreTests first, then from the repository root:

```sh
python3 core/tests/serialRxBuffer/probes/run.py \
  --build-wrapper /path/to/tcbuild.sh --output /tmp/serial-rx-probes
```

`staticExit` uses ASan/UBSan and a global Serial over a real PTY. A destructor feeds the still-running reader after main returns, verifies the exact late drop, then lets Serial join the reader. The current Runtime retains its main-thread queue for the process lifetime. Logger is initialized before Serial to isolate its separate lifetime.

`allocationFailure` persistently rejects chunk malloc on the real RX worker and rejects any subsequent operator new on that worker with exit 86. The main thread must deliver exactly one allocation-loss Warning while the failure is still armed, a second byte must still be received and counted, and input must recover when allocation becomes possible. The injection uses no throw/catch; the framework TU retains its normal exception configuration. The isolated shared-buffer tests also run with `-fno-exceptions`.

`--probe staticExit` / `--probe allocationFailure` select one probe. For a before/after comparison, `--before-include DIR` selects a saved include overlay containing `tc/comm/tcSerial{,Rx}.h`. Include the sibling `tc/utils` and `tc/events` directories (symlinks to the checkout suffice) because those headers use relative includes. An allocating worker enqueue path must fail `allocationFailure`; incorrect late-drop accounting must fail `staticExit`. A process-lifetime Runtime means an allocating enqueue alone no longer implies a static queue-lifetime failure. Compile/link always runs through the supplied wrapper. LeakSanitizer is disabled for environments that trace processes; AddressSanitizer and UndefinedBehaviorSanitizer remain enabled on the teardown probe.
