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

- 隠れているか: すべての画面の作業領域（`rcWork`、タスクバーを除く）が、ほかの窓で覆われているか（見える部分が 5% 以下か、`core` の `IsCovered`）で決める。窓とタイマーは全画面で1つなので、どれか1つの画面でも見えていれば動かす。数えるのは、見えていて最小化しておらず cloaked でない（`DWMWA_CLOAKED`）トップレベルの窓のうち、`WS_EX_TRANSPARENT`・ツールの窓（`WS_EX_TOOLWINDOW` で `WS_EX_APPWINDOW` でない）・不透明度 0 の重ね絵・`Progman`/`WorkerW` を除いたもの。枠は `DWMWA_EXTENDED_FRAME_BOUNDS`
  - 見張りは別のスレッドで、動かす必要があるときだけ。`SetWinEventHook`（out-of-context）で前面の切り替え・ドラッグの終わり・最小化・窓の表示/非表示/破棄・cloak を受けたら 50ms 後に数え直す。`EVENT_OBJECT_LOCATIONCHANGE` は受けない（カーソルが動くたびにも来て、捨てるだけでも重い）。最大化・元に戻すは出来事が来ないので、0.25 秒ごとに前面の窓（`GetForegroundWindow`）の位置だけを見て、替わった・動いたら数え直す。すべての窓の数え直しは to-fu で 1 回 220 万サイクルかかるので、一定の間隔ではしない。変わったら `WM_APP + 2` で主のスレッドに知らせる
- 画面の消灯: `RegisterPowerSettingNotification(GUID_CONSOLE_DISPLAY_STATE)`
- ロック・ユーザー切り替え: `WTSRegisterSessionNotification`
- 省エネモード・バッテリー: `GUID_POWER_SAVING_STATUS`、`GUID_ACDC_POWER_SOURCE`

### 静止画を描く（OS の壁紙と「静止画を書き出す」）

- 描くのは常駐側だけ。並べ方の計算と画素は `core` の `RenderStill`（最近傍、余白と透けたところは余白の色、Linux でもテストする）。書くのは WIC の PNG エンコーダー（24bit、`.tmp` に書いてから入れ替える）。`src/still.cpp`
- 設定画面は、常駐側が読み込んでいるフレーム（GIF の重ね方を当てた絵）をそのまま使いたいので、自分で描かずに `WM_COPYDATA` で頼む: 6 = 書き出す（「フレームの番号\n保存先」）、7 = プレビューを描く（「フレームの番号\n長い辺の上限\n保存先」、上限に収まる整数倍）、8 = フレームの数。中身は UTF-16。C# と C++ に並べ方の計算を二重に持たずに済む
- 保存先は WinUI 3 の `FileSavePicker`。既定の名前の主の画面の大きさは `DisplayArea.Primary`
- Native AOT では、`FileTypeChoices.Add` にコレクション式（`[".png"]`）を渡すと落ちる（`0x80004002`、WinRT のインターフェイスが作られない）。`new List<string>` を渡す

### OS の壁紙

- `IDesktopWallpaper` で、画面（`GetMonitorDevicePathAt`）ごとに `GetMonitorRECT` の大きさの PNG を `%LOCALAPPDATA%\pixwall\desktop` に描いて `SetWallpaper`、置き方は `SetPosition(DWPOS_CENTER)`（拡大しない）。同じ名前だと Explorer が前の絵を使い回すことがあるので、名前に時刻を入れる
- 入れる前の壁紙のパスと置き方は `desktop\original.txt`（画面の ID<TAB>パス）に書く。自分の PNG が入っている画面は覚え直さない（落ちて戻せなかったとき）
- 窓を作り直すたび（`Rebuild`）と、並べ方・位置を変えて少し待ったあとに入れる。画像・並べ方・余白の色・画面の大きさの印が前と同じで、自分の PNG が入ったままなら何もしない
- 戻すのは `WM_DESTROY`（`taskkill`・「pixwall を終了」）と、画像が無くなったとき。今の壁紙が自分の PNG の画面だけ戻す。置き方は、自分の中央のままなら戻す
- 戻せなかった画面（つないでいない）は `original.txt` に残す。PNG は、どれかの画面（つないでいない画面も含む）の壁紙になっている間は消さない。置き方は全画面で1つなので、中央（自分の置き方）は前に覚えた値を優先する

### 設定画面とのやり取り

- 設定画面は `config.json` を書いてから、常駐側の隠れた窓（クラス名 `pixwall-message`）へ `WM_COPYDATA` で頼む。`dwData` が頼みごと: 1 読み直す、2 表示を更新、3 一時停止（`lpData` の1バイト）、4 終わる、5 一時停止しているか（2 している・1 していない を返す）。値は `main.cpp` の `Request` と `settings/Resident.cs` でそろえる。`WM_COPYDATA` は相手が返事を待つ送り方なので、その処理の中ではほかのプロセスの COM（`IDesktopWallpaper`）を呼べない（`RPC_E_CANTCALLOUT_ININPUTSYNCCALL`）。OS の壁紙に触る 1・2・4 は、返事をしてから自分宛てに `WM_APP + 1` で送り直して行う
- 一時停止は設定に書かない（Mac と同じく、起動し直すと動く）。設定画面は開いたときに 5 で今の状態を聞く
- 余白の色は `app.cornerColor`（画像の左上の1ドット）と設定から `BackgroundFor` で決める。設定画面は、初めて「自分で選ぶ」にしたときの色を、頼みごと 9（自動の色を `0x1RRGGBB` で返す）で常駐側に聞く。設定画面のプレビューは常駐側が描くので、色を変えたら 300ms 待ってから描き直してもらう（常駐側が読み直すのは返事をしてからなので）
- 読み直しでは、画像のパスが変わったときだけ画像を読み直す。窓は画像を参照しているので、先に窓を消してから画像を替える。画像が同じなら窓は作り直さず、`WallWindow::SetLayout` で絵の visual の変換と余白の 1×1 の面だけを替える（位置のつまみを 1 目盛り動かすたびに全フレームの面を作り直していたので、重かった）。OS の壁紙は、並べ方・位置が変わってから 500ms 待ち（`kWallpaperTimer`）、続けて変わればまた待って、止まってから1回だけ描き直す
- 設定画面で「読み直して」と頼んだときに常駐側がいなければ、`..\pixwall.exe` を起動する（起動すると `config.json` を読む）
- 常駐側の2回目の起動は、名前付きミューテックスで既にいると分かったら、設定画面を開いて自分は終わる。設定画面も名前付きミューテックスで1つだけにし、2つ目は開いている方を前に出して終わる
- 画像をまだ選んでいない・読めないときは、常駐側は窓を出さずに待ち（OS の壁紙のまま）、設定画面を開く

### 設定画面

- `windows/settings/`（`PixwallSettings.csproj`）。WinUI 3 で、中身は Mac の設定画面とそろえる。Windows の設定アプリと同じ角の丸いカードの行に並べ、背景は Mica。題名の帯も窓の中身として描く（`ExtendsContentIntoTitleBar` と `TitleBar`）
- MSIX に包まず（`WindowsPackageType=None`）、.NET と Windows App SDK を一緒に置く（`SelfContained`・`WindowsAppSDKSelfContained`）。入れる先に .NET が無くても動く
- 参照するのは `Microsoft.WindowsAppSDK.WinUI` だけ。全部入りの `Microsoft.WindowsAppSDK` は AI・検索の部品（onnxruntime・DirectML など）まで連れてくる。言語ごとのフォルダは日本語と英語だけ残す
- publish は Native AOT（`PublishAot`）で、前もって機械語にする。起動時の JIT が無くなり、使っていない .NET も削られる。ビルドには Visual Studio Build Tools の C++（リンカー）が要る
- AOT では、XAML に書いた `Tag` などの値を C# でキャストすると落ちることがある（`InvalidCastException`）。並べ方の選択肢は `SelectedIndex` と C# 側の並び（`Fits`）で対応させる
- `EnableMsixTooling` を付けないと、publish に画面の `PixwallSettings.pri` が入らず、起動してすぐ落ちる
- プレビューは `BitmapDecoder` で最近傍の整数倍に広げてから出す（`Image` は拡大するとぼかすため）
- 読めない画像を選んだときは設定に書かず、その場で「読めませんでした」と出す
- `config.json` は `Utf8JsonWriter` で、キーを名前順に、日本語は `\u` にせずそのまま書く
- ログイン時の起動は `HKCU\...\Run`。起動するたびに、`config.json` の `launchAtLogin` がオンなら `"<exe>"` を書き、オフなら消す（入れ直すと exe の場所が変わっても追いつく）
- `config.json` は `core/config.cpp`（OS に触らない）で読み書きする。Mac の `JSONEncoder` と同じく、キーを名前順に並べて字下げする。書くときは `.tmp` に書いてから `MoveFileEx` で入れ替える
- 入れるのは `windows/install.ps1`。`build\pixwall.exe` と、設定画面を `dotnet publish` したものを `%LOCALAPPDATA%\Programs\pixwall`（設定画面は `settings` フォルダ）に置き、`%APPDATA%\Microsoft\Windows\Start Menu\Programs\pixwall.lnk` を作って、入れた exe から始める（Run もそれを指すよう書き直される）

## Mac 版

1つのアプリ（Swift）。同じ `pixwall.app` を、常駐側（壁紙を動かす）と設定画面（`--settings` を付けて起動、閉じたら終わる）の2つのプロセスで動かす。Xcode のプロジェクトは持たず、SwiftPM（`macos/Package.swift`）でビルドし、`macos/build-app.sh` で `.app` に包んで ad-hoc 署名する。OS に触らない計算（並べ方・待ち時間・止める条件）は `PixwallCore` に分けて XCTest で確かめ、AppKit を使う部分は `pixwall` に置く。試し方は `docs/macos-trial.md`。`Info.plist` の `LSUIElement = YES` で Dock とアプリの切り替えに出さない。

### 壁紙の後ろに窓を出す

- 画面ごとに枠なしの `NSWindow`。`level` は `CGWindowLevelForKey(.desktopWindow)`、`collectionBehavior` は `[.canJoinAllSpaces, .stationary, .ignoresCycle]`、`ignoresMouseEvents = true`
- **メニューバーの下も描く**: 窓の大きさは `visibleFrame` ではなく `NSScreen.frame` にする。`NSWindow` はふつう、窓がメニューバーにかからないように位置を直してしまう（ほかのアプリでメニューバーの下が描かれないのはたぶんこれ）。`constrainFrameRect(_:to:)` を上書きして、渡された枠をそのまま返す。ノッチのある画面も `frame` で全体を覆う
- `level` は `desktopWindow` のままで、メニューバーの下・Dock の下まで絵が見え、デスクトップのウィジェットとアイコンは手前に出ることを MacBook Air M1（macOS 27）で確かめた
- 解像度などの変化: `NSApplication.didChangeScreenParametersNotification` と、窓ごとの `NSWindow.didChangeBackingPropertiesNotification` を受けたら、少し待ってから画面を数え直し、窓の枠・`contentsScale`・倍率を作り直して1回描く
- 手での更新: 設定画面とメニューバーのメニューの「表示を更新」で、上と同じ作り直しをする

### OS の壁紙に1枚目を入れる

- 1枚目を、窓に描くのと同じ並べ方で画面の物理ピクセルの大きさの `CGContext`（`interpolationQuality = .none`）に描き（`StillImage.render`）、`~/Library/Application Support/pixwall/desktop/<画面の UUID>-<幅>x<高さ>-<時刻>.png` に書く。同じ名前だと OS が前の絵を使い回すので、時刻を入れる
- `NSWorkspace.shared.setDesktopImageURL(_:for:options:)` で、その画面に入れる。画面の大きさちょうどに描いたので `imageScaling = .scaleAxesIndependently`（物理ピクセルで 1 対 1）。余白の色も渡す（`OsWallpaper.swift`）
- 入れる前の `desktopImageURL(for:)` と `desktopImageOptions(for:)` を `desktop/original.json` に覚える（`config.json` は設定画面のプロセスも書くので別にする）
- **写真・ダイナミック・空撮などの壁紙は `desktopImageURL` では読めない**（`/System/Library/CoreServices/DefaultDesktop.heic` と返り、それを入れ直すと別の絵になる）。また、入れるのは今の操作スペースだけで、ほかの操作スペースの壁紙を替える方法が無い。そこで、入れるたびに macOS の壁紙の設定ファイル（`~/Library/Application Support/com.apple.wallpaper/Store/Index.plist`、WallpaperAgent が読む）を `desktop/wallpaper-store.plist` に控える。控えは項目ごと（`Spaces` の下の操作スペースごとと、`SystemDefault`・`Displays`・`AllSpacesAndDisplays` の上の段の項目ごと）で、自分の PNG（`/pixwall/desktop/` の文字）を含まない項目だけを写す。戻すときは、自分の PNG を含む項目だけを控えの値にし、控えが無い項目は中の `Desktop` の `Content` だけを本人の壁紙（一番目の操作スペースの控え）に差し替える。項目は消さない（消すと WallpaperAgent が設定ファイル全部を初期の状態に作り直す）。項目がそろわない・自分の PNG が残るときは書かない。書いたら `killall WallpaperAgent`（launchd がすぐ起こし直し、そのときに読む）。組み替えは `PixwallCore.WallpaperStore`。公開されていないファイルなので、無ければ `original.json` で今の操作スペースだけ戻す
- PNG は、設定ファイルとその控えが名前を挙げている間は消さない（ほかの操作スペースの壁紙になったままのことがある）
- 窓を作り直すたびと、並べ方・位置を変えて少し待ったあと（0.5 秒。続けて変わればまた待つ）と、操作スペースが切り替わったとき（`activeSpaceDidChangeNotification`）に入れる。画像・並べ方・画面の大きさの印が前と同じで、自分の PNG が入ったままなら何もしない
- 戻すのは `applicationWillTerminate`（⌘Q・「pixwall を終了」）と、画像が無くなったとき。`NSWorkspace.willPowerOffNotification`（ログアウト・再起動）のあとは戻さない。`pkill`（SIGTERM）では `applicationWillTerminate` が呼ばれないので戻らず、次に起動して終えたときに戻る
- 設定画面の「静止画を書き出す」も同じ描き方（`StillImage.export`）を使い、保存先だけ `NSSavePanel` で選ぶ。OS の壁紙は変えない。プレビューは ImageIO が重ね方を当てたフレームをそのまま出す

### 描く

- ImageIO（`CGImageSourceCreateWithURL`）で全フレームを `CGImage` にして持つ。待ち時間は `kCGImagePropertyGIFUnclampedDelayTime` など
- フレームごとの `CALayer` を最初に全部重ねて置き（`contents` は元の大きさの `CGImage`、`magnificationFilter = .nearest`）、絵が変わる瞬間には見せる layer の `isHidden` を切り替えるだけで描く。`contents` を差し替えると、そのたびに Core Animation が絵を WindowServer へ送り直す。切り替えたときの既定のふわっとした動きは `actions` で切る
- 次のフレームまでは `DispatchSourceTimer` を1つ使い回し、フレームごとに次の時刻を入れ直す
- 窓の root の layer は layer-hosting（`view.layer` を先に渡してから `wantsLayer`）にして、AppKit に中身を描かせない
- 次のフレームまでは `DispatchSourceTimer`（`leeway` を少し付けて、まとめて起こしてもらう）

### 止める

- 隠れているか: `NSWindow.occlusionState` の `.visible`（`didChangeOcclusionStateNotification`）。全画面（別の操作スペース）はこれで分かる
- 最大化した窓の後ろ: `occlusionState` は、メニューバーと Dock のまわりが見えていると `.visible` のままになる。そこで `CGWindowListCopyWindowInfo` でほかの窓の位置を読み（位置と大きさだけなら画面収録の許可は要らない）、画面の `visibleFrame` を覆っているかを `PixwallCore.isCovered` で決める。数えるのは、Dock に出るアプリ（`activationPolicy == .regular`）の layer 0 の窓だけ。`NSWorkspace` のアプリ・操作スペースの切り替えの通知と、どこかでマウスのボタンを離したとき（`NSEvent.addGlobalMonitorForEvents(matching: .leftMouseUp)`。マウスだけなら許可は要らない）の 0.1 秒後と 0.45 秒後、キーで広げたときのために 3 秒ごとに見る。窓の一覧を読むのは M1 で 1 回 0.9ms、窓1つだけでも 0.4ms かかるので、短い間隔で見続けることはしない。ほかの理由で止めている間は見ない
- 画面の消灯: `NSWorkspace.screensDidSleepNotification` / `screensDidWakeNotification`
- ロック: `com.apple.screenIsLocked` / `com.apple.screenIsUnlocked`（分散通知）
- 低電力モード: `ProcessInfo.isLowPowerModeEnabled`、`NSProcessInfoPowerStateDidChange`
- バッテリー: IOKit の `IOPSCopyPowerSourcesInfo`

### 設定画面

- 別のプロセス（`SettingsApp`）。常駐側がもう一度起動されたら（`applicationShouldHandleReopen`）、`NSWorkspace.openApplication`（`createsNewApplicationInstance`、引数 `--settings`）で起動する。もう開いていれば、`showSettings` を送って前に出てもらう。SwiftUI は一度窓を出すと、窓を捨ててもメモリ（18MB ほど）が細切れに残り OS に返らないため、常駐側では SwiftUI を動かさない
- 中身は SwiftUI（`SettingsView`、`Form` の grouped）。入れ物は AppKit の `NSWindow` + `NSHostingController` にした（SwiftUI の `Window` シーンにすると App のライフサイクルを SwiftUI に渡すことになり、`main.swift` で決めている起動の流れを扱いにくいため）。設定画面のプロセスは `.regular` で Dock に出て、窓を閉じたら終わる
- やり取りは `DistributedNotificationCenter`（`dev.mooma.pixwall.<名前>`、`Resident.swift`）。設定画面 → 常駐側: `reloadConfig`・`refresh`・`setPaused`（userInfo の `paused`）・`quit`。常駐側 → 設定画面: `showSettings`。今一時停止しているかは、起動の引数 `--paused` で渡す
- 設定を変えると、設定画面が `config.json` に書いてから `reloadConfig` を送る。常駐側は読み直し、画像が変わったときだけ画像を読み直して、窓を作り直す。画像が同じなら窓は作り直さず、`WallpaperWindow.update` で layer を置き直すだけにする（Windows と同じ理由）。読めない画像は設定画面が先に読んで確かめ、書かない
- 「pixwall を終了」と ⌘Q は、`quit` を送ってから設定画面も終わる
- メニューバーのアイコンは `MenuBarExtra`（設定で出したときだけ）
- ログイン時の起動は `SMAppService.mainApp`（常駐側の起動時と、設定画面で変えたとき）。設定の `launchAtLogin` がオンで `notRegistered` なら `register()`、オフで入っていれば `unregister()`。システム設定で切られると `requiresApproval` になるので、そのときは入れ直さず、設定画面にシステム設定を開くボタンを出す

## 確かめ方

- 描く順番・フレームの待ち時間・止める条件の判断は、OS に触らない部分に分けて単体テストする（Windows は C++ のテスト、Mac は XCTest）
- 後ろに出ているか・アイコンが出ていないか・リソースの目安は、実機で確かめる手順を `docs/` に書く

## 画面の文字の言語

- Mac: SwiftUI の `Text`・`Button` などに書いた日本語の文がそのままキーになる。`Resources/en.lproj/Localizable.strings` に英語を置き、`build-app.sh` が `Contents/Resources` に写す。`Info.plist` は `CFBundleDevelopmentRegion` を `en`、`CFBundleLocalizations` を `en`・`ja` にする（どちらでもない言語では英語）。`ja.lproj` は中身の無い表で、日本語を選んでもらうためだけに置く。`String` で組み立てる文字（書き出しの結果・メニュー・「中央」など）は `String(localized:)` で引く
- Windows の設定画面: XAML の部品に `x:Uid` を付け、`Strings/en-US/Resources.resw`・`Strings/ja-JP/Resources.resw` から引く（MRT Core。`PixwallSettings.pri` に入る）。コードの文字は `Strings.Get`・`Strings.Format`（`ResourceLoader`）。csproj の `DefaultLanguage` を `en-US` にして、どちらでもない言語では英語。同じ `x:Uid` を型の違う部品に付けると、無い・違う意味のプロパティ（`ComboBox.Text` など）にも当ててしまうので、`AutomationProperties.Name` だけの部品は `〜Control` の名前にする。試すときは環境変数 `PIXWALL_LANGUAGE` で `ApplicationLanguages.PrimaryLanguageOverride` を入れる（MSIX に包まないので、次に開いたときには残らない）
- Windows の常駐側: 出す文字は設定画面が見つからないときのメッセージだけなので、`GetUserDefaultUILanguage` が日本語かで選ぶ
