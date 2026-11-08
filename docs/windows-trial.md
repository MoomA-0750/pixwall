# Windows 版の試し方

to-fu（Windows 11）で試す。Visual Studio 2022 の Build Tools（C++ のビルド ツール）と CMake、設定画面には .NET 9 SDK が要る（設定画面は Native AOT で publish するので、C++ の Build Tools も使う）。to-fu には入っている（Build Tools 17.14、MSVC 14.44、CMake は `C:\Program Files\CMake\bin`、.NET SDK 9.0.319）。

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
g++ -std=c++20 -I windows/core windows/core/core.cpp windows/core/config.cpp windows/core/cli.cpp windows/tests/core_tests.cpp -o core_tests && ./core_tests
```

## 動かす

ビルドしたら入れる。設定画面も `dotnet publish` して一緒に `%LOCALAPPDATA%\Programs\pixwall` に置き、スタートメニューに「pixwall」を作って、その画像で始める（動いていた pixwall と設定画面は終わらせる）:

```bat
powershell -ExecutionPolicy Bypass -File install.ps1 -Image C:\path\to\samples\dot-64x64-30f.gif
```

画像は `%APPDATA%\pixwall\config.json` に書かれるので、次からはスタートメニューから（引数なしで）始められる。動いているときにスタートメニューからもう一度開くと、設定画面が出る。設定画面だけ試すなら `dotnet build -c Release -p:Platform=x64`（`windows\settings` で）で `bin\x64\Release\...\PixwallSettings.exe` ができる。外すときは `install.ps1 -Uninstall`（`config.json` は残す）。入れずに試すだけなら `build\pixwall.exe <画像のパス>`。

ssh から入れるときは、ssh のセッションには画面が無いので、`install.ps1` は画面のあるセッションで動かす（下の注意を参照）。

終えるときは `taskkill /im pixwall.exe`（`/f` を付けない。付けると片付けずに終わる）。

## 見ること

- Windows の版（`winver`）。24H2 以降か
- デスクトップのアイコンの後ろに出ている。アイコンをクリック・ドラッグできる
- タスクバーの下まで描かれている（タスクバーを透明にする設定で見る）
- タスクバーにも Alt+Tab にも pixwall が出ない
- ドットがぼやけていない（表示スケール 100% と 150% などで、ドットの大きさが物理ピクセルでそろう）
- 解像度を変えたら描き直す。Explorer を作り直しても（タスクマネージャーで再起動）付け直す
- 動かしている間は OS の壁紙も1枚目になっている（`%LOCALAPPDATA%\pixwall\desktop` に PNG と `original.txt`。`taskkill /f` で落とすと見える）。`taskkill`（`/f` なし）で終えると元の壁紙と置き方に戻り、`desktop` の中が空になる。`taskkill /f` で落としてから起動し直して終えても元に戻る。動かしている間に壁紙を替えると、終えたあともその壁紙のまま
- 設定画面の「静止画を書き出す…」で、書き出す絵のつまみで選んだ枚が、画面の大きさの PNG に書かれる（既定の名前は `<画像の名前>-<幅>x<高さ>.png`）。保存の画面は `windows\tools\save-default.ps1` で、名前を変えずに「保存」を押せる
- 設定画面: スタートメニューから開き直すと出る。2回開いても1つだけ。並べ方・速さ・一時停止・省エネモード・ログイン時に起動を変えるとすぐ壁紙と `config.json`（ログイン時に起動は Run）に反映する。読めない画像を選ぶと「読めませんでした」と出て、設定は変わらない。「pixwall を終了」で壁紙も設定画面も終わる。`config.json` が無い・画像が無いときは、起動すると設定画面が出る
- 設定画面は、画面のあるセッションで `windows\tools\settings-ui.ps1`（部品を名前で動かす。例 `-Fit fill`、`-Toggle 一時停止`、`-Speed 2`、`-Position 25`（0〜100）、`-Background custom`（`auto` か `custom`）、`-Frame 16`、`-Button 表示を更新`。部品は日本語の名前で探すので、表示言語が英語の Windows では設定画面を `PIXWALL_LANGUAGE=ja-JP` を付けて開いておく）と `windows\tools\pick-file.ps1 -Path <画像>`（開いているファイルを選ぶ画面で選ぶ）で動かせる。止まったか・速さが変わったかは `windows\tools\cycles.ps1`（10 秒のサイクル数）で見る
- 起動したあと、`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` の `pixwall` に exe と画像のパスが入っている。サインアウトしてサインインし直すと pixwall が始まる。外すときは `reg delete HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v pixwall /f`

## コマンドライン

`install.ps1` で `%LOCALAPPDATA%\Programs\pixwall\pixwallctl.exe` に入る（`build\pixwallctl.exe` でもよい）。ssh のセッションからは画面のあるセッションの pixwall の窓が見えず `stopped` になるので、画面のあるセッションで動かす。試す前に `config.json` を写しておき、終わったら戻す。

- `status` が `running`。`set fit integer background custom backgroundColor "#203040"` で `%LOCALAPPDATA%\pixwall\desktop` の PNG が描き直され、`export out.png --frame 2` の PNG の左上が (32, 48, 64)
- `pause` の間は `tools\cycles.ps1` のサイクル数が下がり、`status` が `paused`。`resume` で戻る
- `set speed 9`・`set image nope.gif`・`export out.png --frame 999` は断られ（終わりの値 2・1・2）、`show` が変わらない
- 設定画面を開いたまま `set speed 4 fit fit position 0` をすると、設定画面の並べ方・位置・速さがすぐ変わる。そのあと設定画面で何か変えても（UI Automation で「Pause in Energy Saver」を切り替えるなど）、`config.json` に `speed 4`・`fit`・`position 0` が残る
- `quit` で常駐側が終わり、`status` が `stopped`、`%LOCALAPPDATA%\pixwall\desktop` が空

## 終了しても壁紙を残す

画面のあるセッションで:

```powershell
$c = "$env:LOCALAPPDATA\Programs\pixwall\pixwallctl.exe"
& $c set keepWallpaperOnQuit true; & $c quit
(Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Wallpapers').BackgroundHistoryPath0   # pixwall の PNG のまま
# pixwall.exe を起動してから
& $c set keepWallpaperOnQuit false; & $c quit    # 元の壁紙と置き方に戻る
```

2026-11-09 に to-fu で確かめた（2画面とも元の写真と「塗りつぶし」に戻った）。設定画面の「起動」の下に「終了しても壁紙を残す」が出て、オンにすると終了の行の説明が「壁紙を止めて、今の画像の1枚目を OS の壁紙に残す」に変わる（ほかの窓に触らないよう、UI オートメーションで一番下まで送って切り替え、PrintWindow で設定の窓だけを撮った）

## リソースを測る

メモリはタスクマネージャーの「詳細」で pixwall.exe のプライベート ワーキング セット（「メモリ」の列）を見る。PowerShell では `Win32_PerfFormattedData_PerfProc_Process` の `WorkingSetPrivate`。`Get-Process` の `WorkingSet64` はドライバーの DLL などほかのプロセスと分け合う分も含むので、目安とは比べない

CPU はタスクマネージャーや `TotalProcessorTime` だと 15.6ms 刻みでしか数えず、pixwall くらい少ないと大きくぶれる。`QueryProcessCycleTime` で 60 秒のサイクル数を数える（PowerShell から `Add-Type` で呼ぶ）。CPU のクロックで割ると 1 コアの何 % かになる（3.6GHz なら 36 百万サイクル/秒が 1%）。

目安は `SPEC.md` の「常駐の目安」。`samples/dot-64x64-30f.gif` で測る。

夜に試すときは、画面が消えると止まる（「画面が消えている」）ので、測っている間に画面が消えていないかに気をつける。
