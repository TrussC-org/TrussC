# tcxOsc tests

Headless console test (no window). Exercises OSC unicast loopback **and** the
IPv4 multicast path that `OscReceiver::joinMulticast` / `OscSender` (and the
underlying core `tc::UdpSocket` multicast API) provide:

- unicast loopback round-trips a message intact;
- a receiver that **joined** a group receives multicast sent to it;
- a same-port receiver that **did not join** receives nothing (membership required);
- traffic for a **different group** does not leak to a joined receiver.

Because it drives the core multicast socket options through OSC, it doubles as
the cross-platform regression test for that core feature.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR across macOS / Windows / Linux; a non-zero exit fails the job. Run it
locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
