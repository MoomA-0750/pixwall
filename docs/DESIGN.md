# pixwall の設計

動きの正本は `SPEC.md`。ここには仕組みを書く。

## Windows 版

プロセスを2つに分ける。

| プロセス | 言語・部品 | 寿命 |
|---|---|---|
| `pixwall.exe`（常駐） | C++ / Win32 / Direct2D / WIC | ログインから終了まで |
| `pixwall-settings.exe`（設定画面） | C# / WinUI 3（Windows App SDK、非パッケージ） | 開いている間だけ |

WinUI 3 は読み込むだけで数十MBになるため、常駐側には入れない。

### 壁紙の後ろに窓を出す

- `Progman` に `0x052C` を送って `WorkerW` を作らせ、その子にした窓へ描く（Lively Wallpaper などと同じやり方）
- **Windows 11 24H2 から並びが変わった**: `WorkerW` が `Progman` の子になり、アイコンの `SHELLDLL_DefView` との前後も違う。古い並び（`WorkerW` が最上位）と新しい並びの両方を探して、アイコンより後ろに入れる。to-fu の版で先に確かめる
- Explorer が落ちて作り直されたら（`TaskbarCreated` のメッセージ）、窓を付け直す
- 窓は `WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW` でマウスを素通しし、タスクバーにも Alt+Tab にも出さない
- 画面ごとに1枚の窓。大きさは作業領域（`rcWork`）ではなく画面全体（`MONITORINFO.rcMonitor`）にして、タスクバーの下も描く
- 窓は Per-Monitor V2 の DPI 対応にして、物理ピクセルで大きさと倍率を決める
- 解像度などの変化: `WM_DISPLAYCHANGE`・`WM_DPICHANGED`・`WM_SETTINGCHANGE`（`SPI_SETWORKAREA`）を受けたら、少し（200ms ほど）待って変化が落ち着いてから、`EnumDisplayMonitors` で画面を数え直し、窓の大きさ・スワップチェーン（`ResizeBuffers`）・倍率を作り直して1回描く。フレームの絵は元の大きさで持っているので読み直さない
- 手での更新: 設定画面やトレイのメニューから `WM_COPYDATA` で「表示を更新」を送ると、上と同じ作り直しに加えて `WorkerW` も探し直す

### 描く

- WIC で GIF を全フレーム読み、GIF の重ね方（disposal）を当てて元の大きさの完成した絵にしてから、`ID2D1Bitmap` で GPU に置く
- 描くのは `ID2D1DeviceContext::DrawBitmap` を `D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR` で。フリップモデルのスワップチェーンで、フレームが変わったときだけ `Present`
- 次のフレームまでは `CreateWaitableTimerEx`（高精度）で待ち、`MsgWaitForMultipleObjectsEx` でメッセージと一緒に眠る

### 止める

- 隠れているか: 前面の窓がその画面を全部覆っているかを `SetWinEventHook(EVENT_SYSTEM_FOREGROUND / EVENT_OBJECT_LOCATIONCHANGE)` で見張って判断する。`SHQueryUserNotificationState` の全画面判定も合わせる
- 画面の消灯: `RegisterPowerSettingNotification(GUID_CONSOLE_DISPLAY_STATE)`
- ロック・ユーザー切り替え: `WTSRegisterSessionNotification`
- 省エネモード・バッテリー: `GUID_POWER_SAVING_STATUS`、`GUID_ACDC_POWER_SOURCE`

### 設定画面とのやり取り

- 設定画面は `config.json` を書いてから、常駐側の隠れた窓へ `WM_COPYDATA` で「読み直して」と送る
- 常駐側の2回目の起動は、名前付きミューテックスで既にいると分かったら、設定画面を起こして自分は終わる
- ログイン時の起動は `HKCU\...\Run`

## Mac 版

1つのアプリ（Swift）。`Info.plist` の `LSUIElement = YES` で Dock とアプリの切り替えに出さない。

### 壁紙の後ろに窓を出す

- 画面ごとに枠なしの `NSWindow`。`level` は `CGWindowLevelForKey(.desktopWindow)`、`collectionBehavior` は `[.canJoinAllSpaces, .stationary, .ignoresCycle]`、`ignoresMouseEvents = true`
- **メニューバーの下も描く**: 窓の大きさは `visibleFrame` ではなく `NSScreen.frame` にする。`NSWindow` はふつう、窓がメニューバーにかからないように位置を直してしまう（ほかのアプリでメニューバーの下が描かれないのはたぶんこれ）。`constrainFrameRect(_:to:)` を上書きして、渡された枠をそのまま返す。ノッチのある画面も `frame` で全体を覆う
- 描いたあと、メニューバーが透明な設定でメニューバーの下に絵が見えるかを、MacBook Air M1 で確かめる。見えなければ窓の `level` を変えて試す（`docs/decisions.md` に結果を書く）
- 解像度などの変化: `NSApplication.didChangeScreenParametersNotification` と、窓ごとの `NSWindow.didChangeBackingPropertiesNotification` を受けたら、少し待ってから画面を数え直し、窓の枠・`contentsScale`・倍率を作り直して1回描く
- 手での更新: 設定画面とメニューバーのメニューの「表示を更新」で、上と同じ作り直しをする

### OS の壁紙に1枚目を入れる

- 1枚目を、窓に描くのと同じ並べ方で画面の物理ピクセルの大きさの `CGContext`（`interpolationQuality = .none`）に描き、`~/Library/Application Support/pixwall/desktop/<画面のID>-<解像度>.png` に書く
- `NSWorkspace.shared.setDesktopImageURL(_:for:options:)` で、その画面に入れる。`options` は拡大なし（`imageScaling = .scaleNone` 相当）
- 入れる前に `desktopImageURL(for:)` と `desktopImageOptions(for:)` を `config.json` に覚え、外したとき・終えたとき（`applicationWillTerminate`）に戻す。落ちて戻せなかったときは、次に起動したときに覚えた値を見て判断する
- 確かめること: macOS の版によっては、今の操作スペースにしか入らない。すべての操作スペースに入るかを MacBook Air M1 で見る。入らなければ、操作スペースが切り替わったとき（`NSWorkspace.activeSpaceDidChangeNotification`）に入れ直す

### 描く

- ImageIO（`CGImageSourceCreateWithURL`）で全フレームを `CGImage` にして持つ。待ち時間は `kCGImagePropertyGIFUnclampedDelayTime` など
- `CALayer` の `contents` を差し替えるだけで描く。`magnificationFilter = .nearest`
- 次のフレームまでは `DispatchSourceTimer`（`leeway` を少し付けて、まとめて起こしてもらう）

### 止める

- 隠れているか: `NSWindow.occlusionState` の `.visible`（`didChangeOcclusionStateNotification`）
- 画面の消灯: `NSWorkspace.screensDidSleepNotification` / `screensDidWakeNotification`
- ロック: `com.apple.screenIsLocked` / `com.apple.screenIsUnlocked`（分散通知）
- 低電力モード: `ProcessInfo.isLowPowerModeEnabled`、`NSProcessInfoPowerStateDidChange`
- バッテリー: IOKit の `IOPSCopyPowerSourcesInfo`

### 設定画面

- SwiftUI の `Window` シーン。もう一度起動されたら（`applicationShouldHandleReopen`）出す。開いている間だけ `NSApp.setActivationPolicy(.regular)` にして Dock に出し、閉じたら `.accessory` に戻す
- メニューバーのアイコンは `MenuBarExtra`（設定で出したときだけ）
- ログイン時の起動は `SMAppService.mainApp`

## 確かめ方

- 描く順番・フレームの待ち時間・止める条件の判断は、OS に触らない部分に分けて単体テストする（Windows は C++ のテスト、Mac は XCTest）
- 後ろに出ているか・アイコンが出ていないか・リソースの目安は、実機で確かめる手順を `docs/` に書く
