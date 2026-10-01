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
- the drop is logged from the polling call (not the receive thread), a second
  overflow within 2 s is not logged yet, and the next report after 2 s gives
  the drops summed since the last one;
- shrinking a filled queue with `setBufferSize(40)` keeps the newest 40, counts
  the 110 discarded in `getDroppedMessageCount()` and reports them in the next
  poll's warning, so received + dropped == sent over the whole section.

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
