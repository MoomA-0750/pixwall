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

## 見ること

- ドットがぼやけていない: `adb exec-out screencap -p > a.png` で撮り、画像の範囲の色の種類が元の GIF の色の数と同じ（中間の色が無い）。64×64 のサンプルは、整数倍で 16 倍（1024×1024）、色は 4 つ
- 動いている: 0.35 秒ほど空けて2回撮ると違う
- 設定を変えるとすぐ追いつく（デバッグ版なら `adb shell run-as xyz.mooma0750.pixwall` で `files/config.json` を書き換えても試せる。書き換えは別のファイルに書いてから `mv`）
- 止まる: 設定画面などでプレビューを覆うと、壁紙のプロセスの CPU 時間（`/proc/<pid>/stat` の 14・15 番目）が増えない。省電力モードは充電中は入らないので、`adb shell dumpsys battery unplug` してから `adb shell settings put global low_power 1`。終わったら `settings put global low_power 0` と `dumpsys battery reset`

## 測る

リリース版で、プレビューを出したまま 30 秒の CPU 時間を測る（100 tick = 1 コア 1 秒）。

```sh
P=$(adb shell pidof xyz.mooma0750.pixwall)
a=$(adb shell cat /proc/$P/stat | awk '{print $14+$15}'); sleep 30
b=$(adb shell cat /proc/$P/stat | awk '{print $14+$15}'); echo $((b-a))
adb shell dumpsys meminfo $P | grep "TOTAL PSS"
```

2026-10-19 の値（64×64・30 フレーム・10 コマ/秒）: 30 秒で約 96 tick（1 コアの約 3.2%）、PSS 約 46MB。覆っている間は 30 秒で 3 tick（JIT だけ）、省電力モードの間は 0。
