# tcxWebSocket ingress limits

## English

`WebSocketClient::setMaxMessageSize(size_t bytes)` sets the message allowance
for the next connection (default **64 MiB**). Network input needs a finite
memory allowance. Native frame lengths and fragmented totals are checked before
payload buffering. Storage grows geometrically as bytes arrive, capped by the
admitted allowance; an advertised frame length alone does not reserve its
payload storage. Excess input is rejected with Close **1009**, one Warning,
and an internal rejection count. Zero or a value above `SIZE_MAX - 14` returns
`false`, leaves the setting unchanged, warns once and is counted.

`WebSocketClient::setMaxHandshakeSize(size_t bytes)` sets the native upgrade
header allowance for the next connection (default **64 KiB**, including
CRLFCRLF). Network metadata needs a finite memory allowance. The limit is
checked before adding bytes; excess input rejects the upgrade and closes once,
with one Warning and an internal rejection count. Values below 4 return `false`,
leave the setting unchanged, warn once and are counted. Bytes following a valid
upgrade are processed as frames independently of the header allowance.

Control frames retain the RFC 6455 **125-byte** allowance. Excess received
control payloads close with **1002**, one Warning and an internal rejection
count. `sendPing()` retains its truncation to 125 bytes; truncation now warns
once per owning client (including reconnects); every truncation is counted.

On Web, the browser assembles each message before delivering it to TrussC.
The message allowance is checked in JavaScript before the Emscripten callback
allocates or copies into WASM (text is measured in UTF-8 bytes, excluding the
terminator). Excess messages receive the same rejection count and close with application
code **4009**: browser `close()` only allows 1000 or 3000–4999
([WebSocket standard](https://websockets.spec.whatwg.org/#dom-websocket-close)). This
does not bound browser assembly memory or provide native frame/header ingress
guarantees. The browser owns upgrade and control-frame validation;
`setMaxHandshakeSize()` has no effect there. On wasm32, JavaScript normalizes
the allowance to unsigned, preserving accepted settings through `SIZE_MAX - 14`,
including values at or above 2 GiB.

## 日本語

`WebSocketClient::setMaxMessageSize(size_t bytes)` は次の接続のメッセージ上限を
設定します（既定 **64 MiB**）。ネットワーク入力のメモリ使用量を有限にするための
制限です。ネイティブではペイロードを保持する前にフレーム長と断片の合計を検証します。
超過時はメッセージを拒否して Close **1009** を送り、Warning を1回出し、内部の
拒否カウンターを加算します。0 または `SIZE_MAX - 14` を超える値は `false` を返し、
設定を維持し、Warning を1回出し、計数します。

`WebSocketClient::setMaxHandshakeSize(size_t bytes)` は次のネイティブ接続の
upgrade ヘッダー上限を設定します（既定 **64 KiB**、CRLFCRLF を含む）。
ネットワークのメタデータのメモリ使用量を有限にするための制限です。追加前に検証し、
超過時は upgrade を拒否して1回切断し、Warning を1回出し、内部の拒否カウンターを
加算します。4 未満は `false` を返し、設定を維持し、Warning を1回出し、計数します。
有効な upgrade の直後に届くフレームはヘッダー上限とは別に処理します。

制御フレームは RFC 6455 の **125 バイト**上限を維持します。受信時の超過は
Close **1002**、Warning 1回、内部カウンター加算となります。`sendPing()` は
125 バイトへの切り詰めを維持し、所有クライアントの最初の切り詰め時だけ Warning を出します
（再接続でも維持）。切り詰めは毎回計数します。

Web ではブラウザーがメッセージ全体を組み立てます。JavaScript でサイズを検証し、
超過時は Emscripten が WASM に割り当て・コピーする前に同じカウンターで
拒否し、Web 用の **4009** で切断します（ブラウザー API は 1009 を指定できません）。テキストは終端を除く UTF-8 バイト数で測ります。ブラウザー内部の
組み立て用メモリは制限せず、ネイティブのフレーム・ヘッダー受信保証は適用されません。
upgrade と制御フレームの検証はブラウザーが担当し、`setMaxHandshakeSize()` は
Web では効果がありません。wasm32 の JavaScript 側では上限を符号なしに正規化し、
2 GiB 以上を含む `SIZE_MAX - 14` までの有効な設定値を保持します。

## 한국어

`WebSocketClient::setMaxMessageSize(size_t bytes)`는 다음 연결의 메시지 한도를
설정합니다(기본 **64 MiB**). 네트워크 입력의 메모리 사용량을 유한하게 유지하기 위한
제한입니다. 네이티브에서는 페이로드를 저장하기 전에 프레임 길이와 조각의 합계를
검증합니다. 초과 메시지는 Close **1009**로 거부하고 Warning을 한 번 기록하며 내부
거부 카운터를 증가시킵니다. 0 또는 `SIZE_MAX - 14`보다 큰 값은 `false`를 반환하고
설정을 유지하며 Warning 한 번과 카운터 증가로 보고합니다.

`WebSocketClient::setMaxHandshakeSize(size_t bytes)`는 다음 네이티브 연결의
upgrade 헤더 한도를 설정합니다(기본 **64 KiB**, CRLFCRLF 포함). 네트워크 메타데이터의
메모리 사용량을 유한하게 유지하기 위한 제한입니다. 바이트를 추가하기 전에 검사하고,
초과하면 upgrade를 거부하고 한 번 연결을 닫으며 Warning 한 번과 내부 거부 카운터로
보고합니다. 4 미만은 `false`를 반환하고 설정을 유지하며 Warning 한 번과 카운터 증가로
보고합니다. 유효한 upgrade 직후의 프레임은 헤더 한도와 별도로 처리합니다.

제어 프레임은 RFC 6455의 **125바이트** 한도를 유지합니다. 수신 한도 초과는
Close **1002**, Warning 한 번, 내부 카운터 증가로 처리합니다. `sendPing()`은
125바이트로 자르는 동작을 유지하며 소유 클라이언트의 첫 잘림에만 Warning을 기록합니다
(재연결에도 유지). 잘림은 매번 집계합니다.

Web에서는 브라우저가 메시지 전체를 조립합니다. JavaScript에서 크기를 검사하여
초과 메시지를 Emscripten의 WASM 할당 및 복사 전에 같은 카운터로 거부하고 Web 전용 **4009**로 닫습니다(브라우저 API는 1009를 허용하지 않습니다).
텍스트는 종결 문자를 제외한 UTF-8 바이트 수로 측정합니다. 브라우저 내부 조립 메모리는
제한하지 않으며 네이티브 프레임 및 헤더 수신 보장은 적용되지 않습니다. upgrade와
제어 프레임 검증은 브라우저가 담당하며 `setMaxHandshakeSize()`는 Web에서 효과가
없습니다. wasm32의 JavaScript에서는 한도를 부호 없는 값으로 정규화하여
2 GiB 이상을 포함한 `SIZE_MAX - 14`까지의 유효한 설정을 유지합니다.
