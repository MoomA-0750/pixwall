# Windows 版の試し方

to-fu（Windows 11）で試す。Visual Studio 2022 以降（C++ のデスクトップ開発）と CMake が要る。

## ビルドと単体テスト

「x64 Native Tools Command Prompt」などで:

```bat
cd windows
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`core` の単体テストだけなら Linux でも動かせる:

```sh
g++ -std=c++20 -I windows/core windows/core/core.cpp windows/tests/core_tests.cpp -o core_tests && ./core_tests
```

## 動かす

```bat
build\Release\pixwall.exe C:\path\to\samples\dot-64x64-30f.gif
```

終えるときは `taskkill /im pixwall.exe`（`/f` を付けない。付けると片付けずに終わる）。

## 見ること

- Windows の版（`winver`）。24H2 以降か
- デスクトップのアイコンの後ろに出ている。アイコンをクリック・ドラッグできる
- タスクバーの下まで描かれている（タスクバーを透明にする設定で見る）
- タスクバーにも Alt+Tab にも pixwall が出ない
- ドットがぼやけていない（表示スケール 100% と 150% などで、ドットの大きさが物理ピクセルでそろう）
- 解像度を変えたら描き直す。Explorer を作り直しても（タスクマネージャーで再起動）付け直す
- 終えたら OS の壁紙に戻る

## リソースを測る

タスクマネージャーの「詳細」で pixwall.exe の CPU とメモリ（プライベート ワーキング セット）を見る。目安は `SPEC.md` の「常駐の目安」。`samples/dot-64x64-30f.gif` で測る。
