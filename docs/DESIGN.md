# pixwall の設計

動きの正本は `SPEC.md`。ここには仕組みを書く。

## Windows 版

プロセスを2つに分ける。

| プロセス | 言語・部品 | 寿命 |
|---|---|---|
| `pixwall.exe`（常駐） | C++ / Win32 / DirectComposition / WIC | ログインから終了まで |
| `settings\PixwallSettings.exe`（設定画面） | C# / WinUI 3（Windows App SDK、非パッケージ） | 開いている間だけ |

WinUI 3 は読み込むだけで数十MBになるため、常駐側には入れない。

ビルドは CMake（`windows/CMakeLists.txt`）。OS に触らない計算（並べ方・待ち時間・止める条件・GIF の重ね方）は `windows/core/` に分け、Windows でも Linux の g++ でも単体テストできるようにする。試し方は `docs/windows-trial.md`。

### 壁紙の後ろに窓を出す

- `Progman` に `0x052C`（wParam `0xD`、lParam `1`）を送って `WorkerW` を作らせ、その子にした窓へ描く（Lively Wallpaper などと同じやり方）
- **Windows 11 24H2 から並びが変わった**: `WorkerW` が `Progman` の子になり、アイコンの `SHELLDLL_DefView` との前後も違う。古い並び（`WorkerW` が最上位）と新しい並びの両方を探して、アイコンより後ろに入れる。新しい並びは `Progman` に `SHELLDLL_DefView` と `WorkerW` の両方の子があることで見分け、`Progman` の子にして `SHELLDLL_DefView` のすぐ後ろ（`WorkerW` より手前）に入れる。古い並びは `SHELLDLL_DefView` を子に持つ最上位の窓の次の `WorkerW` の子にする。to-fu（26H2、ビルド 26300）で、新しい並びの位置に入ることを確かめた
- **24H2 の `Progman` は `WS_EX_NOREDIRECTIONBITMAP`**: 子の窓の絵を受け止める面を持たないので、ふつうの子の窓に GDI や `ID2D1HwndRenderTarget` で描いても画面に出ない。pixwall の窓は DirectComposition で絵を渡すので出る（窓も `WS_EX_NOREDIRECTIONBITMAP`）
- Explorer が落ちて作り直されたら（`TaskbarCreated` のメッセージ）、窓を付け直す。作り直された直後はアイコンの窓も `WorkerW` もまだ無いことがあるので、見つからなければ 1 秒ごとに最大 30 回探し直す
- 窓は `WS_CHILD` の子窓で、`WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`、`WM_NCHITTEST` に `HTTRANSPARENT` を返してマウスを素通しする。子窓はタスクバーにも Alt+Tab にも出ない
- OS の通知（`WM_DISPLAYCHANGE`・電源・セッション）は、表示しない最上位の窓（`WS_EX_TOOLWINDOW`）で受ける。子窓や message-only の窓には `WM_DISPLAYCHANGE` が来ないため
- 画面ごとに1枚の窓。大きさは作業領域（`rcWork`）ではなく画面全体（`MONITORINFO.rcMonitor`）にして、タスクバーの下も描く
- 窓は Per-Monitor V2 の DPI 対応にして、物理ピクセルで大きさと倍率を決める
- 解像度などの変化: `WM_DISPLAYCHANGE`・`WM_DPICHANGED`・`WM_SETTINGCHANGE`（`SPI_SETWORKAREA`）を受けたら、少し（200ms ほど）待って変化が落ち着いてから、`EnumDisplayMonitors` で画面を数え直し、窓ごと作り直す。フレームの絵は元の大きさで持っているので読み直さない
- 手での更新: 設定画面やトレイのメニューから `WM_COPYDATA` で「表示を更新」を送ると、上と同じ作り直しに加えて `WorkerW` も探し直す

### 描く

- WIC で GIF を全フレーム読み、GIF の重ね方（disposal）を当てて元の大きさの完成した絵にする。重ね方は `core` の `GifCompositor`（WIC は GIF のフレームを重ねずに返すため）。`Background` は背景色ではなく透明に戻す（ブラウザと同じ）
- **DirectComposition の面を差し替える**（Mac で layer を切り替えるのと同じく、絵を送り直さない）: 最初に全フレームを、元の大きさのまま `IDCompositionSurface`（premultiplied）にして GPU に置く。絵が変わる瞬間には、絵の visual の `SetContent` で面を差し替えて `Commit` するだけで、Direct3D の描画も `Present` もしない。拡大は visual の変換（拡大と位置）で行い、`SetBitmapInterpolationMode(NEAREST_NEIGHBOR)` と `SetBorderMode(HARD)` でぼかさない。余白は 1×1 の面を画面いっぱいに広げた visual を絵の下に置く
- Direct3D 11 の装置は全画面で1つ（`Gpu`）。`SINGLETHREADED` と `PREVENT_INTERNAL_THREADING_OPTIMIZATIONS` を付ける。GPU が失われたら（`GetDeviceRemovedReason`）装置から作り直す。GPU が使えないときは WARP
- 今は窓ごとに面を作るので、フレームの面も窓（画面）の数だけ GPU に置く。画面ごとに別の画像になれば同じことなので、そのままにする。全画面に同じ画像のとき、タイマーは1つで全部の窓を進める
- 次のフレームまでは `CreateWaitableTimerEx`（高精度）で待ち、`MsgWaitForMultipleObjectsEx` でメッセージと一緒に眠る
- 窓を作り終えたら `SetProcessWorkingSetSize(-1, -1)` でワーキング セットを手放す。起動時に触るメモリのほとんどはドライバーの初期化の分で、動いている間は触らない

### 止める

- 隠れているか: 前面の窓がその画面を全部覆っているかを `SetWinEventHook(EVENT_SYSTEM_FOREGROUND / EVENT_OBJECT_LOCATIONCHANGE)` で見張って判断する。`SHQueryUserNotificationState` の全画面判定も合わせる
- 画面の消灯: `RegisterPowerSettingNotification(GUID_CONSOLE_DISPLAY_STATE)`
- ロック・ユーザー切り替え: `WTSRegisterSessionNotification`
- 省エネモード・バッテリー: `GUID_POWER_SAVING_STATUS`、`GUID_ACDC_POWER_SOURCE`

### 静止画を書き出す

- 設定画面（C#）から、常駐側と同じ並べ方の計算で PNG を描く。並べ方の計算は C++ と C# で二重に持たないよう、常駐側の C++ を小さな DLL（`pixwall-core.dll`）に分けて両方から呼ぶ。描くのは WIC の `IWICBitmap` へニアレストネイバーで、`IWICBitmapEncoder`（PNG）で書く
- 保存先は WinUI 3 の `FileSavePicker`

### 設定画面とのやり取り

- 設定画面は `config.json` を書いてから、常駐側の隠れた窓（クラス名 `pixwall-message`）へ `WM_COPYDATA` で頼む。`dwData` が頼みごと: 1 読み直す、2 表示を更新、3 一時停止（`lpData` の1バイト）、4 終わる、5 一時停止しているか（2 している・1 していない を返す）。値は `main.cpp` の `Request` と `settings/Resident.cs` でそろえる
- 一時停止は設定に書かない（Mac と同じく、起動し直すと動く）。設定画面は開いたときに 5 で今の状態を聞く
- 読み直しでは、画像のパスが変わったときだけ画像を読み直す。窓は画像を参照しているので、先に窓を消してから画像を替える
- 設定画面で「読み直して」と頼んだときに常駐側がいなければ、`..\pixwall.exe` を起動する（起動すると `config.json` を読む）
- 常駐側の2回目の起動は、名前付きミューテックスで既にいると分かったら、設定画面を開いて自分は終わる。設定画面も名前付きミューテックスで1つだけにし、2つ目は開いている方を前に出して終わる
- 画像をまだ選んでいない・読めないときは、常駐側は窓を出さずに待ち（OS の壁紙のまま）、設定画面を開く

### 設定画面

- `windows/settings/`（`PixwallSettings.csproj`）。WinUI 3 で、中身は Mac の設定画面とそろえる。Windows の設定アプリと同じ角の丸いカードの行に並べ、背景は Mica。題名の帯も窓の中身として描く（`ExtendsContentIntoTitleBar` と `TitleBar`）
- MSIX に包まず（`WindowsPackageType=None`）、.NET と Windows App SDK を一緒に置く（`SelfContained`・`WindowsAppSDKSelfContained`）。入れる先に .NET が無くても動く
- 参照するのは `Microsoft.WindowsAppSDK.WinUI` だけ。全部入りの `Microsoft.WindowsAppSDK` は AI・検索の部品（onnxruntime・DirectML など）まで連れてくる。さらに `PublishTrimmed` で使っていない .NET を削り、言語ごとのフォルダは日本語と英語だけ残す
- `EnableMsixTooling` を付けないと、publish に画面の `PixwallSettings.pri` が入らず、起動してすぐ落ちる
- プレビューは `BitmapDecoder` で最近傍の整数倍に広げてから出す（`Image` は拡大するとぼかすため）
- 読めない画像を選んだときは設定に書かず、その場で「読めませんでした」と出す
- `config.json` は `Utf8JsonWriter` で、キーを名前順に、日本語は `\u` にせずそのまま書く
- ログイン時の起動は `HKCU\...\Run`。起動するたびに、`config.json` の `launchAtLogin` がオンなら `"<exe>"` を書き、オフなら消す（入れ直すと exe の場所が変わっても追いつく）
- `config.json` は `core/config.cpp`（OS に触らない）で読み書きする。Mac の `JSONEncoder` と同じく、キーを名前順に並べて字下げする。書くときは `.tmp` に書いてから `MoveFileEx` で入れ替える
- 入れるのは `windows/install.ps1`。`build\pixwall.exe` と、設定画面を `dotnet publish` したものを `%LOCALAPPDATA%\Programs\pixwall`（設定画面は `settings` フォルダ）に置き、`%APPDATA%\Microsoft\Windows\Start Menu\Programs\pixwall.lnk` を作って、入れた exe から始める（Run もそれを指すよう書き直される）

## Mac 版

1つのアプリ（Swift）。Xcode のプロジェクトは持たず、SwiftPM（`macos/Package.swift`）でビルドし、`macos/build-app.sh` で `.app` に包んで ad-hoc 署名する。OS に触らない計算（並べ方・待ち時間・止める条件）は `PixwallCore` に分けて XCTest で確かめ、AppKit を使う部分は `pixwall` に置く。試し方は `docs/macos-trial.md`。`Info.plist` の `LSUIElement = YES` で Dock とアプリの切り替えに出さない。

### 壁紙の後ろに窓を出す

- 画面ごとに枠なしの `NSWindow`。`level` は `CGWindowLevelForKey(.desktopWindow)`、`collectionBehavior` は `[.canJoinAllSpaces, .stationary, .ignoresCycle]`、`ignoresMouseEvents = true`
- **メニューバーの下も描く**: 窓の大きさは `visibleFrame` ではなく `NSScreen.frame` にする。`NSWindow` はふつう、窓がメニューバーにかからないように位置を直してしまう（ほかのアプリでメニューバーの下が描かれないのはたぶんこれ）。`constrainFrameRect(_:to:)` を上書きして、渡された枠をそのまま返す。ノッチのある画面も `frame` で全体を覆う
- `level` は `desktopWindow` のままで、メニューバーの下・Dock の下まで絵が見え、デスクトップのウィジェットとアイコンは手前に出ることを MacBook Air M1（macOS 27）で確かめた
- 解像度などの変化: `NSApplication.didChangeScreenParametersNotification` と、窓ごとの `NSWindow.didChangeBackingPropertiesNotification` を受けたら、少し待ってから画面を数え直し、窓の枠・`contentsScale`・倍率を作り直して1回描く
- 手での更新: 設定画面とメニューバーのメニューの「表示を更新」で、上と同じ作り直しをする

### OS の壁紙に1枚目を入れる

- 1枚目を、窓に描くのと同じ並べ方で画面の物理ピクセルの大きさの `CGContext`（`interpolationQuality = .none`）に描き、`~/Library/Application Support/pixwall/desktop/<画面のID>-<解像度>.png` に書く
- `NSWorkspace.shared.setDesktopImageURL(_:for:options:)` で、その画面に入れる。`options` は拡大なし（`imageScaling = .scaleNone` 相当）
- 入れる前に `desktopImageURL(for:)` と `desktopImageOptions(for:)` を `config.json` に覚え、外したとき・終えたとき（`applicationWillTerminate`）に戻す。落ちて戻せなかったときは、次に起動したときに覚えた値を見て判断する
- 設定画面の「静止画を書き出す」も同じ描き方を使い、保存先だけ `NSSavePanel` で選ぶ。OS の壁紙は変えない
- 確かめること: macOS の版によっては、今の操作スペースにしか入らない。すべての操作スペースに入るかを MacBook Air M1 で見る。入らなければ、操作スペースが切り替わったとき（`NSWorkspace.activeSpaceDidChangeNotification`）に入れ直す

### 描く

- ImageIO（`CGImageSourceCreateWithURL`）で全フレームを `CGImage` にして持つ。待ち時間は `kCGImagePropertyGIFUnclampedDelayTime` など
- フレームごとの `CALayer` を最初に全部重ねて置き（`contents` は元の大きさの `CGImage`、`magnificationFilter = .nearest`）、絵が変わる瞬間には見せる layer の `isHidden` を切り替えるだけで描く。`contents` を差し替えると、そのたびに Core Animation が絵を WindowServer へ送り直す。切り替えたときの既定のふわっとした動きは `actions` で切る
- 次のフレームまでは `DispatchSourceTimer` を1つ使い回し、フレームごとに次の時刻を入れ直す
- 窓の root の layer は layer-hosting（`view.layer` を先に渡してから `wantsLayer`）にして、AppKit に中身を描かせない
- 次のフレームまでは `DispatchSourceTimer`（`leeway` を少し付けて、まとめて起こしてもらう）

### 止める

- 隠れているか: `NSWindow.occlusionState` の `.visible`（`didChangeOcclusionStateNotification`）。全画面（別の操作スペース）はこれで分かる
- 最大化した窓の後ろ: `occlusionState` は、メニューバーと Dock のまわりが見えていると `.visible` のままになる。そこで `CGWindowListCopyWindowInfo` でほかの窓の位置を読み（位置と大きさだけなら画面収録の許可は要らない）、画面の `visibleFrame` を覆っているかを `PixwallCore.isCovered` で決める。数えるのは、Dock に出るアプリ（`activationPolicy == .regular`）の layer 0 の窓だけ。3 秒ごとと、`NSWorkspace` のアプリ・操作スペースの切り替えの通知で見る。ほかの理由で止めている間は見ない
- 画面の消灯: `NSWorkspace.screensDidSleepNotification` / `screensDidWakeNotification`
- ロック: `com.apple.screenIsLocked` / `com.apple.screenIsUnlocked`（分散通知）
- 低電力モード: `ProcessInfo.isLowPowerModeEnabled`、`NSProcessInfoPowerStateDidChange`
- バッテリー: IOKit の `IOPSCopyPowerSourcesInfo`

### 設定画面

- 中身は SwiftUI（`SettingsView`、`Form` の grouped）。入れ物は AppKit の `NSWindow` + `NSHostingController` にした（SwiftUI の `Window` シーンにすると App のライフサイクルを SwiftUI に渡すことになり、`main.swift` で決めている起動の流れと Dock の出し入れを扱いにくいため）。もう一度起動されたら（`applicationShouldHandleReopen`）出す。開いている間だけ `NSApp.setActivationPolicy(.regular)` にして Dock に出し、閉じたら（`windowWillClose`）`.accessory` に戻す
- 設定を変えると、`config.json` に書いて、画像が変わったときだけ読み直し、窓を作り直す
- メニューバーのアイコンは `MenuBarExtra`（設定で出したときだけ）
- ログイン時の起動は `SMAppService.mainApp`。設定の `launchAtLogin` がオンで `notRegistered` なら `register()`、オフで入っていれば `unregister()`。システム設定で切られると `requiresApproval` になるので、そのときは入れ直さず、設定画面にシステム設定を開くボタンを出す

## 確かめ方

- 描く順番・フレームの待ち時間・止める条件の判断は、OS に触らない部分に分けて単体テストする（Windows は C++ のテスト、Mac は XCTest）
- 後ろに出ているか・アイコンが出ていないか・リソースの目安は、実機で確かめる手順を `docs/` に書く
