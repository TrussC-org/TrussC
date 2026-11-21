# tcxInputReplay

[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

TrussC 창 하나의 입력을 기록하고 실시간으로 재생하여 코드 변경 후에도 같은 조작을 다시 확인하는 디버깅 애드온입니다. 장면 시간이 달라지면 mark로 앱이 도착할 때까지 기다립니다. 입력만으로 앱 상태를 결정론적으로 재현할 수는 없습니다. 전체 상태 스냅샷 API는 없습니다.

`addons.make`에 `tcxInputReplay`를 추가하고 `<tcxInputReplay.h>`를 포함합니다. 모든 API는 메인 스레드에서 호출합니다. `tcApp::setup()`에서 `setup()`을 한 번 호출하면 해당 창을 선택하며, 앱 정리 시 범위 기반 리스너를 해제합니다.

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
}
```

## 입력 기록과 재생

경로는 `getDataPath()`로 해석합니다. 저장 경로를 전달하여 기록을 시작하고, 중지하면 end 레코드를 씁니다.

```cpp
void tcApp::keyPressed(int key) {
    if (key == 'R') tcx::inputreplay::startRecording("session.jsonl");
    if (key == 'S') tcx::inputreplay::stopRecording();
}
```

원래 좌표로 재생하거나 크기 조절 모드를 선택합니다.

```cpp
void tcApp::keyPressed(int key) {
    if (key == 'P') tcx::inputreplay::play("session.jsonl", tcx::inputreplay::ScaleMode::Fit);
}
```

- `play(path)`의 기본값은 `ScaleMode::None`입니다. 창 크기가 다르면 Warning을 기록합니다.
- `ScaleMode::Stretch`는 기록 당시 크기와 현재 크기를 기준으로 x와 y를 따로 조절합니다.
- `ScaleMode::Fit`은 종횡비를 유지하고 기록된 캔버스를 가운데 배치합니다. 기록된 캔버스 밖의 좌표는 여백으로 이동하지 않고 그대로 전달합니다.
- 일반 모드에서는 논리 포인트를, pixelPerfect에서는 프레임버퍼 픽셀을 사용합니다. 크기 변경은 다음 입력부터 적용합니다. 스크롤 양은 조절하지 않습니다.
- DPI 또는 pixelPerfect 설정이 달라도 Warning을 기록합니다. 애드온에는 DPI 또는 pixelPerfect를 변경하는 API가 없습니다. 시작 전에 같은 설정을 구성합니다. `None`은 기록된 좌표 값을 그대로 사용합니다.

```cpp
void tcApp::keyPressed(int key) {
    if (key == 'P') tcx::inputreplay::play("session.jsonl", tcx::inputreplay::ScaleMode::None);
}
```

마우스 이동, 드래그(이동으로 한 번 기록), 버튼, 스크롤, 반복을 포함한 키, 문자, 보조 키, 터치 ID·취소·압력, 파일 드롭 경로를 기록합니다. 주입된 입력은 `injected`와 `tag`를 유지하고 이 애드온 자체의 동기 재생 입력은 제외합니다. 터치에서 합성된 마우스 입력은 터치 주입이 다시 생성하므로 제외합니다. ImGui가 형식화된 입력을 소비하기 전에 기록합니다.

파일 드롭은 경로만 저장하며 파일 자체를 다시 만들지 않습니다. 애드온에는 파일 복원 API가 없습니다. core의 드롭 인자에는 출처 API가 없으므로 `injected: false`와 빈 tag로 기록합니다. 동기 재생 가드는 재기록을 방지합니다. 스크롤 위치는 데이터로 저장하지만 주입 API는 대상 창의 현재 포인터 위치를 사용합니다. 스크롤 위치를 직접 지정하는 API는 없습니다.

## mark로 장면 동기화하기

기록할 때와 재생할 때 모두 각 장면에 도착하면 같은 mark를 호출합니다.

```cpp
void tcApp::enterScene() {
    tcx::inputreplay::mark("scene2");
}
```

재생은 다음 기록된 mark에서 같은 이름의 앱 mark가 올 때까지 무기한 기다립니다. 일찍 도착하면 이전 장면의 남은 입력을 버리고, 늦게 도착하면 대기를 해제합니다. 두 경우 모두 해당 mark에서 시계를 다시 시작하며 이후 입력은 기록된 mark 기준 시간으로 전달합니다. 다른 이름은 재생을 진행하지 않습니다. mark는 상태를 복원하지 않습니다. 기록에서 장면을 재구성하는 API는 없습니다.

실제 입력은 재생 입력과 도착 순서대로 섞이며 재생을 자동 중지하지 않습니다. 앱에서 필요할 때 명시적으로 중지합니다.

```cpp
void tcApp::stopDemo() {
    tcx::inputreplay::stopPlayback();
}
```

자연스럽게 끝나면 `onPlaybackEnd()`를 알립니다. RAII 리스너를 `tcApp` 멤버로 유지합니다. 명시적 중지와 오류에서는 알리지 않습니다. 이 콜백에서 다시 재생하면 반복할 수 있습니다.

```cpp
void tcApp::setup() {
    tcx::inputreplay::setup();
    endListener = tcx::inputreplay::onPlaybackEnd().listen([] {
        tcx::inputreplay::play("session.jsonl");
    });
}
```

`tcApp`에 `tc::EventListener endListener;`를 선언합니다. 중복 setup·시작·중지는 매번 Warning을 기록합니다. 파일 또는 주입 실패는 Error를 기록하고 기록과 재생을 중지합니다. 정리 시 가능하면 end 레코드를 씁니다.

## 기록 파일을 민감한 정보로 취급하기

기록된 문자에 비밀번호가 포함될 수 있습니다. 파일을 민감한 정보로 취급하고 비밀을 입력하기 전에 기록을 중지합니다. 마스킹 또는 암호화 API는 없습니다.

```cpp
void tcApp::beforePasswordEntry() {
    tcx::inputreplay::stopRecording();
}
```

입력만으로 난수 상태, 시계, 네트워크, 카메라 또는 센서를 재현하지 못합니다. 앱별 조건을 별도로 복원합니다. 이 애드온에는 해당 상태의 스냅샷 API가 없습니다.

## JSON Lines 버전 1

UTF-8이며 한 줄에 객체 하나를 저장합니다. 첫 헤더는 `type: "header"`, `version: 1`, `app`, `trussc`, `started`(Unix 밀리초 문자열), `fps`와 `windows` 항목 하나를 가집니다. 창 항목은 `key: "main"`, `width`, `height`, `dpi`, `pixelPerfect`입니다. width와 height는 기록 당시 App 좌표의 크기입니다.

이후 모든 레코드에는 `type`, 경과 `ms`, 상대 `frame`이 있습니다. 입력에는 `window: "main"`, `injected`, `tag`도 있습니다. 마우스는 `x`, `y`, `mods`(shift=1, ctrl=2, alt=4, super=8), 버튼은 `button`(0–2), 스크롤은 `scrollX`, `scrollY`를 추가합니다. 키에는 `key`, `mods`, `repeat`, 문자에는 `codepoint`, `mods`, `repeat`가 있습니다. 터치는 `canceled`와 `touches`(`id`, `x`, `y`, `pressure`, `changed`), 드롭은 `x`, `y`, `files`, mark는 `name`, 마지막 레코드는 `type: "end"`입니다.

재생은 주입 전에 전체 파일을 검증하고 오류에 줄 번호를 표시합니다. 제한은 LF를 제외한 줄당 64 KiB, 줄바꿈을 포함한 파일당 128 MiB, header와 end를 포함한 1,000,000개 객체, `ms` 7일입니다. 시간과 프레임 번호는 감소하면 안 됩니다. 잘못되거나 누락된 필드, 유한하지 않거나 범위를 벗어난 값, 알 수 없는 버전·이벤트, end 누락 또는 end 이후 레코드를 거부합니다. 실패하거나 중단된 기록은 불완전할 수 있습니다. 복구 API는 없습니다.

## 예제와 테스트

`example-inputReplay`는 R로 기록, S로 기록 중지, P로 재생, M으로 mark를 보여 줍니다. `tests`는 화면 없이 파일 검증, 정확한 경계값, 왕복 변환, 좌표 변환 및 가짜 시계로 mark 동기화를 확인합니다. `tests-display --display-check`는 주입 입력을 기록하고 화면에서 재생 이벤트를 비교합니다. Linux에서는 Xvfb를 사용할 수 있습니다.

도구 모음, 단축키, 링 버퍼, 파일 대화상자, 여러 창 매핑 및 MCP 제어는 stage B에 포함합니다.
