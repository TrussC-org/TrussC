# tcxOsc tests

Headless console test (no window). Exercises OSC unicast loopback **and** the
IPv4 multicast path that `OscReceiver::joinMulticast` / `OscSender` (and the
underlying core `tc::UdpSocket` multicast API) provide:

- unicast loopback round-trips a message intact;
- a second `setup()` on an occupied port fails with one error event and log;
- even a later socket with reuse enabled cannot take the exclusive port;
- `bindFirstFree()` skips an occupied port;
- two `setupMulticast()` receivers share one port and both receive the message
  (a clear SKIP is printed if there is no multicast route);
- invalid multicast group/interface setup fails with one error and no listener;
- a receiver that **joined** a group receives multicast sent to it;
- traffic for a group **nobody joined** is not received (multicast is join-gated).

It also checks bundle parsing and dispatch (unicast loopback into `OscReceiver`,
plus `OscBundle::fromBytes` directly):

- bundles nest without a depth limit: 17 and 100 levels (the old cap was 16)
  parse with every level kept, 17 and 400 levels (a datagram under 9 KiB, which every OS
  delivers over loopback) arrive through an `OscReceiver`, and a bundle as deep as fits in 65,536 bytes (3276 levels) is
  parsed, dispatched, copied, serialized and freed on a thread with a 64 KiB
  stack; a nested bundle that fails to parse is exactly one `onParseError` and
  delivers nothing;
- the deepest packet (3276 levels) delivered to a `Deliver::Main` listener
  allocates memory proportional to the packet (checked with a counting
  `operator new`), and a Main listener's copy never sees edits that other
  listeners make to the tree afterwards (`storageBounds.cpp`);
- a 10-level bundle delivers each inner message exactly once, and
  `onBundleReceived` listeners get the parsed bundles themselves, not copies;
- a bundle element or blob whose size runs past the end of the data fails to
  parse (one `onParseError`, nothing delivered), and so do sizes up to the
  32-bit maximum;
- valid packets of every argument shape and padding length still parse.

And the OSC 1.0 type tags (messages built byte by byte at run time):

- every tag (`i f s b T F h d t S c r m N I [ ]`) round-trips: `OscMessage`
  encodes it to its wire bytes and those bytes decode to the same value, one
  tag per message and all tags in one message;
- a tag outside OSC 1.0, fixed-size argument data 1 byte short, or an unpaired
  `[` / `]` fails the message, and a damaged message fails its bundle (also one
  level down); through `OscReceiver` that is one `onParseError`, nothing
  delivered;
- a message that ends before its last zero padding parses and is delivered,
  and each `OscReceiver` logs one warning the first time.
- every getter returns the same value at every index of a message with all
  tags and of one that mixes tags with and without data (`i T s N f [ i ] b F
  h I`), whether built with `addX()`, decoded, copied, moved, carried in a
  bundle or rebuilt after `clear()`; the mixed message encodes to its wire
  bytes and round-trips byte for byte;
- only the tags with data store an argument object: `T F N I [ ]` store none,
  also in a message of only `T` tags.

And the polling queue:

- a receiver polled with `getNextMessage()` only (never `hasNewMessage()`)
  receives messages;
- with the default size (1024) a 150-message bundle arrives whole, 0 dropped;
- with `setBufferSize(100)` the same bundle keeps the newest 100: 50 dropped
  and counted in `getDroppedMessageCount()`;
- first loss logs once on the main thread automatically, even without another poll;
- shrinking keeps the newest whole messages and preserves the lifetime counter;
- 5000-float messages parse and arrive whole; the datagram size bound remains;
- retained-storage eviction accounts for empty arguments, blobs and queue nodes;
- parsed addresses, string arguments and type tags at lengths 15, 16, 17, 29,
  30 and 31 poll intact with zero drops from an empty queue; charges match the
  copies' actual retained capacities;
- 1024 messages with 64,000-byte blobs fit; a single over-budget copy is rejected;
- send-only UDP sockets allocate no receive buffer; both receiving modes allocate
  their own run's buffer without changing a retained old run.

`storageBounds.cpp` drives the production parser/events and queue through private
friend seams, without UDP timing or a display. Existing loopback tests still
exercise the socket path.

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
