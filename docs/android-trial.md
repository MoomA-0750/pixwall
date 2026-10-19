# Android 版の試し方

この Linux の PC でビルドし、USB でつないだ Pixel 3（Android 12）で試す。JDK は Android Studio に入っているものを使う。

## ビルドと単体テスト

```sh
cd android
export JAVA_HOME=/opt/android-studio/jbr
./gradlew :app:testDebugUnitTest   # 並べ方・待ち時間・設定・GIF の読み込み
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

2026-10-19 に上のすべてを確かめた。エミュレーターは GPU をソフトで真似るので、CPU の値（閉じたホーム画面で 20 秒に約 100 tick）は実機の目安にならない
