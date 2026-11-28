# tcxInputReplay

이 애드온은 실험적이며, JSONL 형식은 향후 버전에서 변경될 수 있습니다.

일반 Node로 구성한 작은 입력 녹화 및 재생 도구 모음입니다. App에 `Toolbar`를 추가하면 해당 창의 마우스, 키보드, 터치, 파일 드롭 입력을 녹화합니다. 재생은 기록된 시간에 입력을 주입하고 장면 마크에서 기다립니다. 기존 단일 창 JSONL 버전 1과 호환되며 ImGui가 필요하지 않습니다.

이 애드온은 `Window::injectInput`을 사용합니다. 직접 입력 주입 도구를 만들려면 `Player.h`를 읽어 보시기 바랍니다.

예제는 시간이 지나면 사라지는 하늘색 마우스 궤적과 주황색 클릭 점을 그려 재생 중인 입력을 눈으로 확인할 수 있게 합니다.

## App에 도구 모음 추가하기

`<tcxInputReplay.h>`를 포함하고 예제의 `using namespace std;`, `using namespace tc;`, `using namespace tcx;`를 사용합니다. `tcApp`에 다음 멤버를 선언합니다.

```cpp
shared_ptr<inputreplay::Toolbar> replay;
vector<Vec2> dots;
```

`setup()`에서 도구 모음을 추가합니다. App의 마우스 처리에서 `isPointerOver()`를 사용하면 도구 모음 클릭을 그리기에서 제외합니다.

```cpp
void tcApp::setup() {
    // F9 shows and hides the toolbar. With no argument, no key is assigned.
    replay = make_shared<inputreplay::Toolbar>(KEY_F9);
    // Optional: start and stop recording from the keyboard (no key by default).
    // replay->setRecordKey(KEY_F8);
    addChild(replay);
}
void tcApp::mousePressed(const MouseEventArgs& e) {
    if (replay->isPointerOver()) return;   // the click is for the toolbar
    dots.push_back(e.pos);
}
```

도구 모음은 부모의 상단 중앙에 한 번 배치됩니다. 이동에는 일반 Node API를 사용합니다. 녹화 중에는 Record가 빨간 Stop으로, 재생 중에는 Play가 파란 Stop으로 바뀝니다. Open 또는 `.jsonl` 파일 드롭으로 녹화를 선택하며, 파일명 위에 포인터를 놓으면 전체 경로를 표시합니다. 도구 모음의 Stop(링 녹화 내보내기 포함)을 누르면 즉시 OS 저장 대화상자가 열립니다. 저장하면 데이터 디렉터리의 `input-replay` 폴더에 있는 임시 파일을 선택한 경로로 이동합니다. 다른 볼륨으로 이동할 때는 복사한 후 임시 파일을 삭제합니다. 취소하면 임시 파일을 삭제합니다. 저장에 실패한 경우에만 Save가 표시되어 다시 시도할 수 있습니다. 코드에서 `stopRecording()`이나 `flush()`를 호출하면 대화상자를 열지 않습니다.

저장 대화상자는 이름에 일치하는 확장자가 없으면 기존 파일 덮어쓰기를 확인하기 전에 `.jsonl`을 붙입니다. 확장자는 대소문자를 구분하지 않습니다.

## 파일 또는 링에 입력 녹화하기

파일 경로는 App의 데이터 디렉터리를 기준으로 해석합니다. 파일 녹화는 종료 레코드를 쓰고 닫습니다.

```cpp
replay->startRecording("session.jsonl");
replay->stopRecording();
```

링 녹화는 최근 지정 시간의 입력을 메모리에 유지합니다. Flush는 해당 구간을 저장하고 녹화를 중지합니다. 링 녹화 중 `stopRecording()`은 내용을 버립니다.

```cpp
replay->startRecording(inputreplay::RecordingMode::Ring, chrono::seconds(30));
replay->flush("last-30-seconds.jsonl");
```

[example-ringRecording](example-ringRecording)은 빠르게 드래그하면 공이 벽을 통과하는 버그를 포착하고 자동으로 저장한 뒤 재생 시 재현합니다.

`isRecording()`은 두 모드의 녹화 상태를 반환합니다. `getRecordedCount()`는 헤더, 마크, 종료 레코드를 제외한 입력 수를 반환하며 링에서는 보관 중인 입력만 셉니다. 각 Toolbar는 자신의 창만 녹화하고 도구 모음 클릭도 포함합니다. 주입된 입력은 녹화하지 않습니다.

## 입력 재생 및 장면 동기화하기

재생 대상은 Toolbar와 같은 창입니다. `ScaleMode::None`은 좌표를 유지하고, `Stretch`는 각 축을 조정하며, `Fit`은 종횡비를 유지하여 중앙에 배치합니다.

```cpp
replay->play("session.jsonl", inputreplay::ScaleMode::Fit);
replay->mark("scene");
```

녹화 및 재생 중 대응하는 장면 전환에서 `mark("scene")`을 호출합니다. 재생은 다음 일치하는 마크까지 기다리며, 마크에 일찍 도달하면 이전 장면의 나머지를 건너뜁니다. `stopPlayback()`은 재생을 중지하고 `isPlaying()`은 상태를 반환합니다. 재생 입력에는 `injected = true` 및 소스 태그 `"replayed"`가 붙으며 버튼과 도구 모음 단축키는 반응하지 않습니다.

## MCP 제어 명시적으로 활성화하기

애드온의 MCP 도구는 `registerMcpTools()`를 호출한 Toolbar만 제어합니다.

```cpp
replay->registerMcpTools();
```

Control 수준 도구는 `tcx_inputreplay_start_recording`(선택적 `path`, 생략 시 링과 선택적 `durationMs`), `tcx_inputreplay_stop_recording`, `tcx_inputreplay_play`(`path`, 선택적 `scale`), `tcx_inputreplay_stop_playback`, `tcx_inputreplay_flush`(`path`)의 다섯 가지입니다. 마지막으로 명시적으로 등록한 인스턴스를 제어합니다. 그 외 녹화 및 재생 처리는 애드온의 전역 상태를 사용하지 않습니다.
