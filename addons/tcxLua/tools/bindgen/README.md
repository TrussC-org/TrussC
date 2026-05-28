# bindgen

libclang (Python binding) で `core/include/TrussC.h` 等のフリー関数を解析し、Lua バインディング (`trussc_generated.cpp`) を生成するスクリプト。

クラスやメソッドのバインディング (`AudioEngine`, `Image`, `Fbo` …) は対象外で、これらは `addons/tcxLua/src/tcxLua.cpp` の `setBindings()` で手書きする。

## Generate

```bash
pip install uv         # 初回のみ
uv sync                # 初回 / 依存更新時
uv run main.py ../../../../core/include/TrussC.h
cp trussc_generated.cpp ../../src/generated
```

## 対象ヘッダ

- `TrussC.h` (グローバル関数)
- `tcMath.h`, `tcPrimitives.h`, `tc3DGraphics.h`, `tcLog.h`

新しい無視ルール (`ignore_functions`, `ignore_namespaces`, `ignore_classes`) や手動オーバーロード (`additional_overloads`) は `main.py` 冒頭の定数を編集。

## libclang のシステムヘッダ依存

`<string>` などの C++ 標準ライブラリ参照を解決させるため、`discoverCompileArgs()` で
- macOS: `xcrun --show-sdk-path` + `clang++ -print-resource-dir`
- Linux: `clang++ -print-resource-dir`

を渡している。WindowsはCIで未検証。

**注意**: libclang 18 (PyPI で取得可能な最新版) と clang 21 (Xcode 16+ の system clang) の組み合わせでは、C++20 stdlib 内部のテンプレート (`__builtin_ctzg` 等) を理解できず、`std::vector<Vec3>` のような型解決が失敗して `int` にフォールバックする場合がある。スクリプトはこれを検知して、ソーストークンから型を復元するフォールバック (`getParamType`) を行う。

regen 後は **diff を必ず確認**して、見覚えのない `const int &` への置換が無いことをチェックすること。

## 開発時のチェックフロー

1. C++ ヘッダで新規 free function を追加
2. `uv run main.py ...` を再実行
3. `cp` でコピー
4. `git diff` で意図した差分か確認
5. 必要なら `ignore_functions` や `additional_overloads` を調整して再生成
