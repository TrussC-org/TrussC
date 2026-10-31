# tcxOsc

OSC (Open Sound Control) send/receive for [TrussC](https://github.com/TrussC-org/TrussC).
Talk to other OSC apps — TouchDesigner, Max/MSP, Pure Data, Processing, VJ/lighting
software, phones — straight from a TrussC app over UDP. No external library; it's a
self-contained encoder/decoder on top of the core `UdpSocket`.

Supports every OSC 1.0 type tag, for sending and receiving: `int32` (`i`),
`float32` (`f`), `string` (`s`), `blob` (`b`), the booleans `T`/`F`, and the optional
types `int64` (`h`), `float64` (`d`), timetag (`t`), symbol (`S`), char (`c`), RGBA
color (`r`), MIDI message (`m`), nil (`N`), impulse (`I`) and arrays (`[` ... `]`).
Bundles (with NTP timetags) can nest messages and other bundles.

> **Namespace:** the classes live in **`tcx`** (`tcx::OscSender`, `tcx::OscMessage`, …),
> like every other addon. They used to live in the core `trussc` (`tc`) namespace by
> mistake; `tc::OscSender` still compiles as a deprecated alias so existing code keeps
> working, but it **will be removed in v1.0.0**. Migrate to `tcx::` — in practice just
> add `using namespace tcx;` and the unqualified names (`OscSender`, …) keep working.

## Features

- **Sender** — multi-destination (`send()` fans out to every connected host), or
  one-off `sendTo(host, port, …)`. Broadcast is enabled automatically for a `.255`
  address.
- **Receiver** — two ways to consume incoming messages:
  - **Event style** (`onMessageReceived`, `onBundleReceived`, `onParseError`):
    callbacks fire on the receive thread the moment a packet arrives.
  - **Polling style** (`hasNewMessage()` / `getNextMessage()`): drain a queue from
    `update()` on the main thread — no threading to think about.
- **Bundles** — build nested `OscBundle`s with a timetag; the receiver dispatches each
  contained message individually (and also emits `onBundleReceived`).
- **Robust parser** — a packet is either read in full or rejected (reported once via
  `onParseError`, nothing delivered). Rejected: a type tag that is not an OSC 1.0
  tag, argument data that runs past the end of the packet, a `[` or `]` without its
  pair, and a bundle element whose size runs past the end of the data. A bundle with
  a nested message or bundle that fails to parse is rejected as a whole. Bundles
  nest up to `OscBundle::MAX_NESTING_DEPTH` (16) levels.
- **Missing padding** — a message that ends before the zero padding of its last
  item (to a 4-byte boundary) is accepted, and each `OscReceiver` logs one warning
  the first time it gets one.

## Install

Add it to your project's `addons.make`:

```
tcxOsc
```

…then `trusscli update`. There's no build config to write — it's header-only (plus two
small `.cpp` files that TrussC auto-collects from `src/`).

## Quick start

```cpp
#include <TrussC.h>
#include <tcxOsc.h>
using namespace tc;
using namespace tcx;

class tcApp : public App {
    OscSender   sender_;
    OscReceiver receiver_;
    EventListener msgListener_;

    void setup() override {
        sender_.setup("127.0.0.1", 9000);   // send to host:port
        receiver_.setup(9000);              // listen on port

        // Event style: callback runs on the RECEIVE THREAD (see note below)
        msgListener_ = receiver_.onMessageReceived.listen([](OscMessage& m) {
            logNotice("osc") << "got " << m.toString();
        });
    }

    void update() override {
        OscMessage m("/wek/inputs");
        m.addFloat(getMouseX() / (float)getWidth());
        m.addFloat(getMouseY() / (float)getHeight());
        sender_.send(m);
    }
};
```

## Port ownership and multicast

`setup(port)` owns the port exclusively. If another receiver already uses it,
setup returns `false`, logs one error and reports it through `onParseError`.
Starting the event example twice now reaches its "Failed to bind port" branch.

For several receivers on one multicast port, use
`setupMulticast(9000, "239.0.0.1")`. It enables reuse, binds and joins the group,
then starts receiving. The optional `iface` is an IPv4 interface address
(`""` or `"0.0.0.0"` selects the default route). A failed step reports an error,
closes the socket and returns `false`.

`joinMulticast()` and `leaveMulticast()` still work on a receiver that is already
set up. Joining after `setup()` keeps the port exclusive. For v0.8.0, apps that
previously shared a multicast port with `setup()` plus `joinMulticast()` must use
`setupMulticast()` instead.

## Threading: event vs polling

Reception happens on a **background thread**, so the two consume styles differ in where
your code runs:

| | Event style | Polling style |
|---|---|---|
| Where your code runs | receive thread | main thread (your `update()`) |
| Mutex needed for shared data | **yes** | no |
| Setup | store an `EventListener` | just call `hasNewMessage()` / `getNextMessage()` |
| Latency | lowest (fires on arrival) | one frame |

Polling enables an internal buffer on the first `hasNewMessage()` or `getNextMessage()`
call. Pick polling unless you specifically need lowest latency — it's the simpler,
footgun-free path.

Untrusted senders or a stalled reader can fill this queue; the message cap and the retained-storage budget below keep memory finite. Overflow drops the oldest whole messages and is counted.

The queue defaults to 1024 messages and also has a fixed **64 MiB retained-storage
budget**, including message/argument objects, string and blob capacities, and
queue nodes. Storage is allocated on demand. Both limits evict the oldest whole
messages before admission, using the copy's measured capacity; a single message
over the storage budget is rejected whole. `setBufferSize(0)` keeps zero messages.
Shrinking also drops the oldest whole messages. `getDroppedMessageCount()` counts queue losses since
construction and never resets on query or close/reopen. Queue loss does not
suppress inline events.

The first queue loss automatically logs one Warning on the main thread, even
without another poll. It names the store, limit, needed amount and units; its
once gate survives close/reopen. Drain more often, or raise `setBufferSize()` if
the message-count limit is the constraint; the storage budget stays fixed.

`setBufferSize()` / `getBufferSize()` reason: untrusted senders or a stalled
reader can fill the queue; finite count and storage bounds keep memory finite.
Overflow evicts oldest whole messages and increments the counter.
日本語: 外部の送信者や停止した読み手によるメモリ増加を抑えるため、件数と保存量を制限します。
上限超過時と縮小時は古いメッセージを丸ごと破棄し、カウンターに加算します。
한국어: 외부 송신자나 멈춘 소비자로 인한 메모리 증가를 막기 위해 메시지 수와 저장량을 제한합니다.
초과하거나 한도를 줄이면 오래된 메시지 전체를 버리고 카운터에 더합니다.

Incoming packets (including the public `fromBytes()` parsers) are preflighted
without allocation before decoding: at most **65,536 bytes** and **16 bundle
levels**. There is no argument or work-unit limit; a 5000-float message fits.
Over-limit packets are rejected whole; the next valid packet can proceed.
The receiver reports the first limit loss with a separate lifetime Warning.
Rejected datagrams are not queue-message losses. Main-thread event delivery
uses a separate store from the polling queue and is unlimited by default.

日本語: 受信パケットの 65536 バイトと入れ子 16 段の上限は維持します。
引数数の上限はなく、5000 個の float を持つメッセージも受信できます。
Main 配送は既定で無制限で、イベントの `setMainThreadDeliveryLimit(count)`
で任意の件数上限を設定できます。0 は無制限です。超過コピーは破棄して内部で数えます。

한국어: 수신 패킷의 65536바이트 및 중첩 16단계 한도는 유지합니다.
인자 수 한도는 없으며 float 5000개를 담은 메시지도 수신합니다.
Main 전달은 기본 무제한이며 이벤트의 `setMainThreadDeliveryLimit(count)`로
선택적 개수 한도를 설정합니다. 0은 무제한입니다. 초과 복사본은 버리고 내부에서 집계합니다.

```cpp
// Polling style — all on the main thread, no locks:
void update() override {
    while (receiver_.hasNewMessage()) {
        OscMessage m;
        if (receiver_.getNextMessage(m)) {
            logNotice("osc") << m.toString();
        }
    }
}
```

## API

### OscMessage

```cpp
OscMessage msg("/address");
msg.addInt(42).addFloat(1.5f).addString("hi").addBool(true);
msg.addBlob(data, size);
msg.addInt64(1234567890123).addDouble(0.1);   // 'h', 'd'
msg.addTimetag(OscBundle::TIMETAG_IMMEDIATELY); // 't' (NTP format)
msg.addSymbol("name").addChar('A');           // 'S', 'c'
msg.addRgba({255, 128, 0, 255});              // 'r': osc::OscRgba, bytes 0-255
msg.addMidi({0, 0x90, 60, 100});              // 'm': osc::OscMidi {port, status, data1, data2}
msg.addNil().addImpulse();                    // 'N', 'I' (no data)
msg.addArrayBegin().addInt(1).addInt(2).addArrayEnd();  // '[' i i ']'

string addr   = msg.getAddress();
size_t n      = msg.getArgCount();          // one per type tag, '[' and ']' included
string tags   = msg.getTypeTags();          // e.g. "ifs"
char   t0     = msg.getArgType(0);          // the tag of argument 0
int    i      = msg.getArgAsInt(0);         // i / f / h / d coerce to each other
float  f      = msg.getArgAsFloat(1);
int64_t h     = msg.getArgAsInt64(0);
double d      = msg.getArgAsDouble(1);
string s      = msg.getArgAsString(2);      // 's' and 'S'
string sym    = msg.getArgAsSymbol(2);      // same as getArgAsString()
vector<uint8_t> b = msg.getArgAsBlob(0);
bool   flag   = msg.getArgAsBool(0);
uint64_t t    = msg.getArgAsTimetag(0);
char   c      = msg.getArgAsChar(0);
osc::OscRgba rgba = msg.getArgAsRgba(0);
osc::OscMidi midi = msg.getArgAsMidi(0);
string debug  = msg.toString();             // "/address i:42 f:1.5 s:\"hi\""
```

`N`, `I`, `[` and `]` carry no data; like `T` / `F` each is an argument of its own,
so `getArgType(i)` and the argument at index `i` always belong together. A
received message relays unchanged: `toBytes()` writes each argument by its tag.

### OscBundle

```cpp
OscBundle bundle;                  // immediate timetag by default
bundle.addMessage(msg);
bundle.addBundle(inner);           // bundles can nest
bundle.setTimetag(ntp);            // optional scheduled execution (NTP format)

size_t count = bundle.getElementCount();
if (bundle.isMessage(i)) OscMessage m = bundle.getMessageAt(i);
if (bundle.isBundle(i))  OscBundle  b = bundle.getBundleAt(i);   // copy
const OscBundle* child = bundle.bundleAt(i);  // no copy; nullptr if not a bundle

OscBundle::MAX_NESTING_DEPTH       // 16: deepest nesting the parser accepts
```

### OscSender

```cpp
bool setup(host, port);            // clear destinations + add one (convenience)
bool connect(host, port);          // add a destination (auto-broadcast for ".255")
void disconnect(host, port);       // remove one
void disconnect();                 // remove all (socket stays open)
void close();                      // close socket + clear destinations

bool send(const OscMessage&);      // send to every connected destination
bool send(const OscBundle&);
bool sendTo(host, port, const OscMessage&);   // one-off, ignores destination list
bool sendTo(host, port, const OscBundle&);

bool isConnected() const;
const vector<Destination>& getConnectedAddresses() const;
```

### OscReceiver

```cpp
bool setup(port);                  // exclusive bind + start receiving
bool setupMulticast(port, group, iface = ""); // shared bind + join + start receiving
bool joinMulticast(group, iface = "");       // join on an already set up receiver
bool leaveMulticast(group, iface = "");
void close();
int  getPort() const;
bool isListening() const;

// Message/bundle events fire on the receive thread; setup errors fire during setup.
Event<OscMessage> onMessageReceived;
Event<OscBundle>  onBundleReceived;   // see the note below
Event<string>     onParseError;

// Polling (call from update(); buffer turns on at the first
// hasNewMessage() / getNextMessage())
bool     hasNewMessage();
bool     getNextMessage(OscMessage& out);
void     setBufferSize(size_t);        // default 1024; oldest dropped when full
size_t   getBufferSize() const;
uint64_t getDroppedMessageCount() const;  // cumulative queue losses; first loss warns automatically
```

> **`onBundleReceived`** hands each listener the parsed bundle itself, not a copy,
> and that `OscBundle&` is valid only during the call. A listener may edit that
> bundle, but must not keep a reference to an enclosing bundle (from an earlier
> call for the same packet) and modify it (e.g. `addMessage()` / `clear()`) while a
> nested bundle of that packet is being dispatched.

## Examples

Both examples are self-contained ImGui apps that send to **and** receive from
themselves on `127.0.0.1`, so you can try OSC without a second machine. Type an address,
toggle int/float/string args, and watch them arrive in the receive pane.

- **`example-osc-event/`** (port 9000) — the **event** style. Registers
  `onMessageReceived` / `onParseError` listeners and shows the key gotcha: callbacks run
  on the receive thread, so the log buffer is guarded with a `mutex`. Also demonstrates
  building and sending a **bundle**.
- **`example-osc-polling/`** (port 9001) — the **polling** style. Drains messages with
  `hasNewMessage()` / `getNextMessage()` inside `update()` on the main thread — no mutex,
  simpler code. Start here if you're new to OSC.

```bash
cd example-osc-polling
trusscli update
trusscli run
```

To poke it from outside, send from any OSC tool (or another TrussC app) to the port the
example prints in its title bar.

## License

MIT (author: tettou771). No third-party dependencies — the OSC codec is implemented
here directly on top of the core `UdpSocket`.

OOM guarantees for public parser results apply to Release builds, or iterator debugging off (`_ITERATOR_DEBUG_LEVEL=0` on MSVC). Preflight itself allocates no storage, but constructing or returning an empty `OscMessage` or `OscBundle` can allocate STL iterator proxies in MSVC Debug. Persistent failure tests for those result constructions SKIP with a reason when iterator debugging is enabled.
