# tcxOsc tests

Headless console test (no window). Exercises OSC unicast loopback **and** the
IPv4 multicast path that `OscReceiver::joinMulticast` / `OscSender` (and the
underlying core `tc::UdpSocket` multicast API) provide:

- unicast loopback round-trips a message intact;
- a receiver that **joined** a group receives multicast sent to it;
- traffic for a group **nobody joined** is not received (multicast is join-gated).

It also checks bundle parsing and dispatch (unicast loopback into `OscReceiver`,
plus `OscBundle::fromBytes` directly):

- bundles nest up to `OscBundle::MAX_NESTING_DEPTH` levels; one level more, or a
  nested bundle that fails to parse, is exactly one `onParseError` and delivers
  nothing;
- a 10-level bundle delivers each inner message exactly once, and
  `onBundleReceived` listeners get the parsed bundles themselves, not copies.

> Note: it does *not* assert that a non-member socket on a *different port* gets
> nothing while another socket on the host has joined the group — IPv4 membership
> is an interface-level IGMP concept, so the kernel may still deliver to a
> port-matching socket (observable on Linux). The robust invariant is per-group.

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
