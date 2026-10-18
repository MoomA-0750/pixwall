# Windows 版の試し方

to-fu（Windows 11）で試す。Visual Studio 2022 の Build Tools（C++ のビルド ツール）と CMake が要る。to-fu には入っている（Build Tools 17.14、MSVC 14.44、CMake は `C:\Program Files\CMake\bin`）。

## ビルドと単体テスト

「x64 Native Tools Command Prompt」（または `vcvars64.bat` を呼んだあと）で:

```bat
cd windows
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

ssh から試すときは、ssh のセッションには画面が無いので、pixwall.exe は画面のあるセッションで起動する（to-fu では cua-computer-server の `computer_run_command` から `Start-Process` で起動した）。

`core` の単体テストだけなら Linux でも動かせる:

```sh
g++ -std=c++20 -I windows/core windows/core/core.cpp windows/tests/core_tests.cpp -o core_tests && ./core_tests
```

## 動かす

ビルドしたら入れる。`%LOCALAPPDATA%\Programs\pixwall` に置き、スタートメニューに「pixwall」を作って、その画像で始める（動いていた pixwall は終わらせる）:

```bat
powershell -ExecutionPolicy Bypass -File install.ps1 -Image C:\path\to\samples\dot-64x64-30f.gif
```

画像は `%APPDATA%\pixwall\config.json` に書かれるので、次からはスタートメニューから（引数なしで）始められる。外すときは `install.ps1 -Uninstall`（`config.json` は残す）。入れずに試すだけなら `build\pixwall.exe <画像のパス>`。

ssh から入れるときは、ssh のセッションには画面が無いので、`install.ps1` は画面のあるセッションで動かす（下の注意を参照）。

終えるときは `taskkill /im pixwall.exe`（`/f` を付けない。付けると片付けずに終わる）。

## 見ること

- Windows の版（`winver`）。24H2 以降か
- デスクトップのアイコンの後ろに出ている。アイコンをクリック・ドラッグできる
- タスクバーの下まで描かれている（タスクバーを透明にする設定で見る）
- タスクバーにも Alt+Tab にも pixwall が出ない
- ドットがぼやけていない（表示スケール 100% と 150% などで、ドットの大きさが物理ピクセルでそろう）
- 解像度を変えたら描き直す。Explorer を作り直しても（タスクマネージャーで再起動）付け直す
- 終えたら OS の壁紙に戻る
- 起動したあと、`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` の `pixwall` に exe と画像のパスが入っている。サインアウトしてサインインし直すと pixwall が始まる。外すときは `reg delete HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v pixwall /f`

## リソースを測る

メモリはタスクマネージャーの「詳細」で pixwall.exe のプライベート ワーキング セット（「メモリ」の列）を見る。PowerShell では `Win32_PerfFormattedData_PerfProc_Process` の `WorkingSetPrivate`。`Get-Process` の `WorkingSet64` はドライバーの DLL などほかのプロセスと分け合う分も含むので、目安とは比べない

CPU はタスクマネージャーや `TotalProcessorTime` だと 15.6ms 刻みでしか数えず、pixwall くらい少ないと大きくぶれる。`QueryProcessCycleTime` で 60 秒のサイクル数を数える（PowerShell から `Add-Type` で呼ぶ）。CPU のクロックで割ると 1 コアの何 % かになる（3.6GHz なら 36 百万サイクル/秒が 1%）。

目安は `SPEC.md` の「常駐の目安」。`samples/dot-64x64-30f.gif` で測る。

夜に試すときは、画面が消えると止まる（「画面が消えている」）ので、測っている間に画面が消えていないかに気をつける。
