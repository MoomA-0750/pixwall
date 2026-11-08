# Android 版の試し方

この Linux の PC でビルドし、USB でつないだ Pixel 3（Android 12）で試す。JDK は Android Studio に入っているものを使う。

## ビルドと単体テスト

```sh
cd android
export JAVA_HOME=/opt/android-studio/jbr
./gradlew :core:testDebugUnitTest  # 並べ方・待ち時間・設定・GIF の読み込みと書き出し・文字盤の組み立て
./gradlew :app:assembleDebug       # app/build/outputs/apk/debug/app-debug.apk
./gradlew :app:assembleRelease     # 測るときはこちら（デバッグの鍵で署名するので上書きで入る）
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

画面を書いたら `scripts/check_m3e.sh android` で Material 3 Expressive から外れていないかを見る（CC ではフックで自動で走る）。

## 動かす

```sh
adb push ../samples/dot-64x64-30f.gif /sdcard/Download/
adb shell am start -n xyz.mooma0750.pixwall/.SettingsActivity
```

設定画面の「画像を選ぶ」で GIF を選び、「壁紙に設定」で OS のプレビューを開く。プレビューは壁紙と同じエンジンで動くので、壁紙を替えずに試せる。プレビューだけを開くには:

```sh
adb shell am start -a android.service.wallpaper.CHANGE_LIVE_WALLPAPER \
  --ecn android.service.wallpaper.extra.LIVE_WALLPAPER_COMPONENT xyz.mooma0750.pixwall/.PixwallWallpaperService
```

## 試すときの注意

- `adb shell am force-stop xyz.mooma0750.pixwall` をすると、OS が壁紙を既定のものに戻す。プロセスを作り直したいときは、`adb install -r` で入れ直す（壁紙は外れず、OS がつなぎ直す）。リリース版はデバッグできないので、シェルからの `kill` は効かない
- 選んである画像のファイルを `adb push` で上書きすると、メディアの仕組みがそのファイルへの読む権限を取り消し、壁紙が黒くなる（設定画面に「読めない」と出る）。違う画像で試すときは、別の名前で置いて設定画面から選び直す
- 再起動したあとは、PIN でロックを解くまで pixwall に替わらない（PIN は本人に頼む）

## 見ること

- ドットがぼやけていない: `adb exec-out screencap -p > a.png` で撮り、画像の範囲の色の種類が元の GIF の色の数と同じ（中間の色が無い）。64×64 のサンプルは、整数倍で 16 倍（1024×1024）、色は 4 つ
- 動いている: 0.35 秒ほど空けて2回撮ると違う
- 設定を変えるとすぐ追いつく（デバッグ版なら `adb shell run-as xyz.mooma0750.pixwall` で `files/config.json` を書き換えても試せる。書き換えは別のファイルに書いてから `mv`）
- 止まる: 設定画面などでプレビューを覆うと、壁紙のプロセスの CPU 時間（`/proc/<pid>/stat` の 14・15 番目）が増えない。省電力モードは充電中は入らないので、`adb shell dumpsys battery unplug` してから `adb shell settings put global low_power 1`。終わったら `settings put global low_power 0` と `dumpsys battery reset`

## 測る

リリース版で、ホーム画面に出したまま 30 秒の CPU 時間を測る（100 tick = 1 コア 1 秒）。ばらつきが大きい（同じ版でも 30 秒で 70〜120 tick）ので、何回か測る。比べるときは、2つの版を交互に入れて同じ条件で測る。端末が後でコンパイルし終えた状態にするには `adb shell cmd package compile -m speed -f xyz.mooma0750.pixwall` のあと `install -r`。どこで CPU を使っているかは、リリース版にも `profileable` を付けてあるので `adb shell simpleperf record --app xyz.mooma0750.pixwall --duration 20 -e cpu-clock -g -o /data/local/tmp/perf.data` と `simpleperf report -i /data/local/tmp/perf.data --children --sort symbol` で見られる

```sh
P=$(adb shell pidof xyz.mooma0750.pixwall)
a=$(adb shell cat /proc/$P/stat | awk '{print $14+$15}'); sleep 30
b=$(adb shell cat /proc/$P/stat | awk '{print $14+$15}'); echo $((b-a))
adb shell dumpsys meminfo $P | grep "TOTAL PSS"
```

2026-10-19 の値（64×64・30 フレーム・10 コマ/秒、ホーム画面）: 30 秒で約 100〜110 tick（1 コアの約 3〜3.7%）、PSS 約 46MB。同じ絵が 3 コマずつ続く 30 フレームの GIF（まとめて 10 枚・3.3 回/秒）は約 39 tick。覆っている間は 30 秒で 3 tick（JIT だけ）、画面が消えている・省電力モードの間は 0

## 折りたたみ

to-fu の Android Studio に、折りたたみのエミュレーター `Pixel_10_Pro_Fold`（Android 16、開くと 2076×2152、閉じると外側の画面 1080×2364）がある。kite-rebuild の端末の試験も同じエミュレーターを使う（`~/.local/state/kite-rebuild/check.lock` を握った `make check`）。使う前に `pgrep -af "make check|adb -P 5038"` で何も動いていないことを確かめ、動いていたら kite のセッションに声をかけて終わるのを待つ。使い終わったら pixwall を消し（壁紙は既定に戻る）、`adb emu kill` で止める（to-fu はメモリが少ない）。

- 起こす: to-fu の画面のあるセッションで `emulator.exe -avd Pixel_10_Pro_Fold -no-window -no-audio -no-snapshot-save`（ssh から起こすと ssh を閉じたときに道連れになる）。APK は `scp` で to-fu に送り、to-fu の `adb` で入れる
- 撮った画面は `/data/local/tmp` に置く（`/sdcard` に置くとメディアに載り、画像を選ぶ画面に出てくる）
- 折る: `adb shell cmd device_state state 0`（閉じる）・`1`（半開き）・`2`（開く）、戻すのは `state reset`。回すのは `settings put system accelerometer_rotation 0` と `settings put system user_rotation 1`
- 見ること: それぞれの形で、絵が画面の真ん中に整数倍で出ている（64×64 のサンプルは、開くと 32 倍の 2048×2048 で左右・上下に 14px の余白、閉じると 16 倍の 1024×1024、回すと左右に 52px）・2回撮ると違う（動いている）。閉じてロック画面でも動く。画面を消すと壁紙のプロセスの CPU 時間が増えない

- 設定画面の見本: 開いた状態・開いて回した状態では見本が左・設定が右、閉じた状態では見本が上・設定が下。見本を押した全画面の見本と、ホーム画面の壁紙で、絵の範囲（余白の赤でないところ）が同じ（開くと左右 14〜2061・上下 52〜2099、閉じると左右 28〜1051・上下 670〜1693、回すと左右 52〜2099・上下 14〜2061）

2026-10-19 に上のすべてを確かめた。エミュレーターは GPU をソフトで真似るので、CPU の値（閉じたホーム画面で 20 秒に約 100 tick）は実機の目安にならない

## 画像の切り替え

デバッグ版で `files/config.json` を書き換えて試す（設定画面は開いている間は書き換えを読み直さないので、ホーム画面に戻してから書く）。同じ画像で並べ方だけを変えると、黄色い点の大きさで見分けられる（64×64 のサンプルは、整数倍で約 96px、画面を埋めると約 200px）。

- 時刻: `switchBy` を `time` にし、`timeSlots` の1つを今の時刻より前（整数倍）、もう1つを1〜2分後（`fill`）にする。その時刻になると、ホーム画面の点が大きくなる
- ダーク・ライト: `switchBy` を `appearance` にし、`lightSlot` と `darkSlot` の並べ方を変える。`adb shell cmd uimode night no` / `yes` で、すぐ替わる
- 設定画面: 切り替え方の3つのつながったトグル、時刻の行（「変える」「消す」、今出している行に「表示中」）、「時刻を追加」、時刻を選ぶ画面（24 時間の文字盤）、ダーク・ライトのときの「ライト」「ダーク」のトグル
- Wave の画面の上の透明な窓が下の真ん中にあると、そこへの `adb shell input tap` は落とされる（logcat に `Untrusted touch due to occlusion`）。その範囲を避けて押す

2026-11-09 に Pixel 3 で上のすべてを確かめた

## Wear OS の文字盤

この Linux の PC は仮想マシンの中なので、時計のエミュレーターは起動の途中で落ちる（入れ子の仮想化）。to-fu の `pixwall_watch`（Wear OS 6、454×454 の丸。to-fu にある Wear OS 6 の像を使う）で試す。kite-rebuild の毎晩の試験（1:00〜4:30 ごろ）の時間は避け、使い終わったら `adb -s emulator-5570 emu kill` で止める。

```sh
cd android && ./gradlew :wear:assembleDebug   # wear/build/outputs/apk/debug/wear-debug.apk（文字盤の土台 wearface の release も中に入る）
```

- 起こす: to-fu の画面のあるセッションで `emulator.exe -avd pixwall_watch -no-window -no-audio -no-snapshot -port 5570`
- 入れて開く: `adb -s emulator-5570 install -r wear-debug.apk`、`adb -s emulator-5570 shell am start -n xyz.mooma0750.pixwall/xyz.mooma0750.pixwall.wear.MainActivity`
- 画面が暗くなって止まらないように: `svc power stayon true`。充電中の画面が出るときは `dumpsys battery unplug`
- GIF を足す: `adb push a.gif /data/local/tmp/` のあと `adb shell "run-as xyz.mooma0750.pixwall sh -c 'mkdir -p files/gifs && cp /data/local/tmp/a.gif files/gifs/'"`（デバッグ版だけ）
- 設定を直接書く: 同じく run-as で `files/config.json` を置き換える（`image` は `asset:samples/…` か `file:gifs/…`）
- ボタンを押す代わりに: デバッグ版は `am start -S -n …/.wear.MainActivity --ez install true` で開くと、読み込み次第「文字盤にする」と同じことをする。結果は `logcat -s pixwall:I` に `watch face: Active` などと出る
- アンビエントは `settings put global ambient_enabled 1` と `svc power stayon false` のあと `input keyevent KEYCODE_SLEEP`、戻すのは `KEYCODE_WAKEUP`
- 文字盤の土台だけを試すとき（時計のアプリを通さない）: `scripts/wear_face_gif.sh <GIF>` で `android/wearface` に入れて `./gradlew :wearface:assembleDebug`、`adb install` のあと `am broadcast -a com.google.android.wearable.app.DEBUG_SURFACE --es operation set-watchface --es watchFaceId xyz.mooma0750.pixwall.watchfacepush.gif`。Push で入れた同じ名前の文字盤があると、署名の鍵が違うので先に `adb uninstall` が要る。XML を書き換えたら、公式の検証ツール（github.com/google/watchface の `third_party/wff`、`./gradlew :specification:validator:executable-jar`）で `java -jar wff-validator.jar 1 watchface.xml`

見ること:

- 入る: 初めて押すと「今の文字盤にしてよいか」を聞かれ、許すと文字盤が pixwall になる（`logcat` に `WF Changed to … xyz.mooma0750.pixwall.watchfacepush.gif`）。もう一度押すと、同じ枠が入れ替わる（`Asset module broadcast after update successful`）
- 並べ方: 横長の 48×24（左が赤・右が青）で、整数倍は 9 倍の 432×216 が真ん中、画面に合わせる・位置 0 は上に 454×227 で下が余白、画面を埋める・位置 0 は赤だけ・位置 100 は青だけが見える
- ドットがぼやけていない: 丸の内側で時刻の字を避けた範囲の色が、元の GIF の色だけ。255 段のうち 1〜2 段だけずれた色が数十ドット出るのは、目では分からないのでよしとする
- 大きすぎる: 227×227・130 枚は整数倍で約 107MB になり、「約 107 MB 要り、上限の 100 MB を超えます」と出て入れない
- 今の文字盤にできない: 文字盤を `adb uninstall` してからもう一度押すと（今の文字盤にできるのは一度だけ）、「文字盤の一覧から pixwall を選んでください」と出る
- アンビエント: 黒地に細い時刻だけで、2回撮っても同じ。丸のふちに色が残らない。戻るとまた動く
- 動き: 0.15 秒ごとに `screencap` で撮って、点の色の移り変わりを見る。エミュレーターでは待ち時間が守られない（SPEC の未決事項）

2026-10-21 に上のすべてを確かめた
