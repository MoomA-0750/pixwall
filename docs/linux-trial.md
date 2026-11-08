# Linux 版の試し方

この PC（Fedora 44、GNOME 50）でビルドして試す。要るもの: `gcc-c++ make pkgconf wayland-devel mesa-libEGL-devel mesa-libGLES-devel gtk4-devel libadwaita-devel gdk-pixbuf2-devel glib2-devel`（テストの期待値を作り直すときだけ Python の Pillow）。

## ビルドと単体テスト

```sh
cd linux
make            # build/ に pixwall・pixwallctl・pixwall-settings（選び役）・pixwall-settings-gtk
make test       # core_tests（Windows と共通の core）・linux_tests（GIF を読む・設定のファイル）・選び役・拡張機能の計算
make qt         # QML 版の設定画面 pixwall-settings-qt（Qt 6 の開発用ファイルが要る。この PC には無いので cramgent で）
make test-qt    # theme_tests（Omarchy の色を読む・見張る）・backend_tests（QML 版が書く config.json）
```

- `linux_tests` は、`tests/fixtures/expected.txt`（Pillow で重ねた絵の印）と、こちらで読んだ GIF のフレームを突き合わせる。GIF を足したら `python3 -I tests/fixtures/make_expected.py`
- `gnome-extension/tests/run.js` は、拡張機能の `lib.js`（並べ方・覆われ方・待ち時間）を `build/layout_dump`（C++ の core）とばらばらな値で突き合わせ、`pixwallctl --frames`・`--resolve` の出力を読めるかも確かめる

## 入れる

```sh
linux/install.sh
```

`~/.local/bin` に4つ（常駐側・`pixwallctl`・選び役 `pixwall-settings`・GTK 版の設定画面）、アプリの一覧に「pixwall」、`~/.local/share/gnome-shell/extensions/pixwall@mooma0750.xyz` に拡張機能を入れる。GNOME（Wayland）は新しく入れた拡張機能をログインし直すまで読まないので、初めて入れたときは一度ログアウトする。

QML 版の設定画面は `linux/install.sh --qt` で `~/.local/bin/pixwall-settings-qt` を足す（`--remove-qt` で外す）。入っていれば、GNOME の外（Hyprland・Omarchy・KDE など）では選び役がこちらを開く。`PIXWALL_UI=gtk` か `qt` を付けて開くと、その版になる。

## GNOME の拡張機能を、本物のデスクトップと分けて試す

画面の無い GNOME Shell（1280×720 の画面が1つ）を、試験用の HOME・設定の場所・D-Bus で動かす。試験用の拡張機能（`tests/gnome-headless/pixwall-test@mooma0750.xyz`）が、シェルの中で JS を評価する・画面を撮る・押す口を出す。

```sh
cd linux
make
tests/gnome-headless/start.sh > /tmp/pxh.log 2>&1 &
source tests/gnome-headless/env.sh   # 以後のコマンドを試験のシェルに向ける
build/pixwallctl set image ../samples/dot-64x64-30f.gif
px_eval 'Main.overview.hide(); 1'    # 起動したときは概観が出ている（概観の間は止まる）
px_eval "JSON.stringify({frames: $PX_WALL._image.contents.length, index: $PX_WALL._index, timer: $PX_WALL._timer > 0})"
px_shot /tmp/shot.png
build/pixwall-settings &             # 設定画面も試験のシェルに出る。px_click x y で押す
pkill -f '[g]nome-shell --headless'  # 止める
```

**必ず `start.sh` から動かす。** `dbus-run-session` の中で HOME を変えても、D-Bus が起こす設定の保存役（dconf-service）は本物の HOME のまま `~/.config/dconf/user` に書き、本物のデスクトップの拡張機能の一覧などを書き換える（2026-11-08 に一度起きた。`docs/decisions.md`）。試す前後で `stat -c %Y ~/.config/dconf/user` が変わらないことを見る。

確かめたこと（2026-11-08、GNOME 50.5）:

- 1280×720 で 64×64 の GIF が 11 倍・中央・余白は左上の色で動き、`_index` が進む
- `pixwallctl pause`・`resume`・`status`、`set fit free …` で面がすぐ動く
- 外観で切り替え: `gsettings set org.gnome.desktop.interface color-scheme prefer-dark`（試験のシェルの中で）ですぐダークの画像に替わり、戻すとライトに戻る
- 窓を最大化すると止まり（タイマーが無くなり `_index` が進まない）、元に戻す・最小化で動き出す
- 設定画面: 時刻の行を選ぶと「画像（06:00 から）」に移る、時の「＋」で 07:00 になる、「時刻を足す」で今の時刻の行が増える。`pixwallctl quit` で拡張機能が止まり、GNOME の壁紙が見えて「pixwall が動いていません」が出る。「開始」で動き出す

GNOME の壁紙に1枚目を入れる・戻す（2026-11-09、GNOME 50）:

```sh
gsettings get org.gnome.desktop.background picture-uri   # 画像を選ぶと …/pixwall/desktop/wallpaper-a.png（並べ方を変えると -b）
px_eval 'Main.overview.show(); 1'; px_shot /tmp/ov.png   # 概観の背景も1枚目
px_eval 'Main.screenShield.lock(false); 1'               # ロック: 拡張機能は止まるが、壁紙は戻さない（試験用の拡張機能も止まる）
gdbus call --session --dest org.gnome.ScreenSaver --object-path /org/gnome/ScreenSaver --method org.gnome.ScreenSaver.SetActive false
ls --full-time "$XDG_DATA_HOME/pixwall/desktop"           # 解いても PNG は描き直さない（時刻が同じ）
build/pixwallctl quit                                    # 元の壁紙に戻り、PNG と original.ini が消える
```

本物の GNOME では、入れたあとにログインし直すと新しい拡張機能になる。blur-my-shell のバーのぼかしを「静的」に戻し、上のバーの裏が1枚目のぼかしになることを撮って見る

「終了しても壁紙を残す」（試験のシェルで）: `build/pixwallctl set keepWallpaperOnQuit true; build/pixwallctl quit` で `picture-uri` が pixwall の PNG のまま、`original.ini` も残る。拡張機能を有効にし直して `set keepWallpaperOnQuit false; quit` で元に戻る。設定画面の一番下に「終了しても壁紙を残す」が出て、終了の行の説明が変わる（2026-11-09 に確かめた）

## 本物の GNOME の画面を撮る

GNOME Shell の撮影の窓口は、ほかのプログラムからは断る。xdg-desktop-portal から撮る（初めての1回だけ「許可しますか」が出る）:

```sh
python3 -I linux/tests/portal_shot.py ~/shot.png          # 画面全体
python3 -I linux/tests/portal_shot.py ~/bar.png 0 160     # 上から 160 ピクセルの帯だけ（ほかの窓を写さない）
```

## Hyprland で試す

cramgent（Omarchy を入れた M1 MacBook Air、Arch Linux ARM）で試す。要るもの（gcc・make・wayland・egl・gtk4・libadwaita・gdk-pixbuf）は入っている。ssh から試すときは、画面の Hyprland に向ける:

```sh
# この PC から送ってビルドする
git ls-files linux windows/core windows/tests samples | tar -cf - -T - | ssh cramgent 'mkdir -p ~/pixwall-cli && tar -xf - -C ~/pixwall-cli'
ssh cramgent
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export HYPRLAND_INSTANCE_SIGNATURE=$(ls /run/user/1000/hypr/ | head -1)
export XDG_CONFIG_HOME=~/pixwall-cli/config   # 試すときは本物の設定・自動起動に触れない
cd ~/pixwall-cli/linux && make && make test
nohup ./build/pixwall ../samples/dot-64x64-30f.gif > ~/pixwall-cli/wall.log 2>&1 &
./build/pixwallctl status        # running
grim ~/pixwall-cli/shot.png      # 画面を撮る
./build/pixwallctl quit
```

cramgent の Hyprland（0.56）は設定が Lua なので、`hyprctl dispatch` の書き方が違う: 作業スペースは `hyprctl dispatch "hl.dsp.focus({workspace = 7})"`、全画面は `hyprctl dispatch "hl.dsp.window.fullscreen()"`（もう一度で戻る）。昔の書き方（`dispatch workspace 7`）はエラーになり、何も変わらない。`PIXWALL_DEBUG=1` を付けて起動すると、隠れていると判断した画面を出す。

確かめたこと（2026-11-08、Hyprland 0.56.2、2560×1600・拡大率 1.6）:

- 64×64 の GIF が 25 倍で中央に出て、バーの下まで描く。ドットのふちは1画素で色が切り替わる（ぼけない）
- CPU（10 秒の CPU 時間）: 窓の無い作業スペースで 0.06 秒（約 0.6%）、2つ並べた窓・1つの窓・全画面の窓のある作業スペースでは 0。メモリ（PSS）約 22MB
- 設定画面は、OS がダークなので libadwaita のダークで出る
- 時刻の切り替え: `config.json` の `timeSlots` の1つを1〜2分後に始まるようにすると、その時刻の 0.5 秒以内に替わる（`grim -g "0,100 200x200"` で前後を撮って色を比べた）

### Omarchy の壁紙に付いていくのを試す

本物の Omarchy の壁紙・テーマに触れないよう、`XDG_STATE_HOME` に試験用の `omarchy/current/background` を作り、別の D-Bus で起動する（動いている本物の pixwall と名前がぶつからない）:

```sh
T=~/pixwall-cli/omtest
mkdir -p $T/state/omarchy/current $T/bg/pixwall $T/config/pixwall
# $T/bg に a.png・b.png（静止画）、$T/bg/pixwall/a.gif（a.png と組）を置く
ln -nsf $T/bg/a.png $T/state/omarchy/current/background
echo '{"switchBy": "omarchy", "launchAtLogin": false}' > $T/config/pixwall/config.json
XDG_CONFIG_HOME=$T/config XDG_STATE_HOME=$T/state setsid dbus-run-session -- ./build/pixwall &
ln -nsf $T/bg/b.png $T/state/omarchy/current/background   # 組なし: pixwall の面が消える
hyprctl layers | grep -c "namespace: pixwall"               # 本物の分と合わせて数える
```

確かめたこと（2026-11-09）: 組ありの静止画・GIF を直接 → 描く、組なし → 0.6 秒以内に面が消え、CPU 0・PSS 22MB のまま。設定画面は組の名前を出し、組が無ければ見本・並べ方を出さない（`PIXWALL_SNAPSHOT` で撮った）。

ほかに見ること: すべての画面の端から端まで（バーの下も）出る、クリックがデスクトップに素通しされる、ドットがぼけない（拡大率 1.25 などでも）、全画面の窓（`fullscreen`）にすると CPU 時間が増えない（`ps -o time= -p $(pgrep -x pixwall)` を数秒おきに見る）、画面をつなぐ・外すと面が増える・消える、`pixwallctl quit` で終わる。

## QML 版の設定画面

cramgent で組む（Qt 6.12 の qt6-base・qt6-declarative がある）。本人の画面・設定・動いている pixwall に触れないよう、窓は画面に出さず（`QT_QPA_PLATFORM=offscreen`）、設定と Omarchy のテーマは一時的な場所に置き、別の D-Bus で開く。`PIXWALL_SNAPSHOT=<png>` を付けると、開いて 1.5 秒で窓の絵を書いて閉じる（中身の高さで切る）:

```sh
# この PC から送って組む（build/ は送らない）
rsync -a --delete --exclude build/ --exclude .git/ ~/Documents/git-cloned/pixwall-qml/ cramgent:build/pixwall-qml/
ssh cramgent
cd ~/build/pixwall-qml/linux && make qt && make test-qt
T=$(mktemp -d); mkdir -p $T/config/pixwall $T/state/omarchy/current/theme
cp /usr/share/omarchy/themes/catppuccin/colors.toml $T/state/omarchy/current/theme/   # 明るいテーマは flexoki-light
echo '{"image": "'$HOME'/build/pixwall-qml/samples/dot-64x64-30f.gif"}' > $T/config/pixwall/config.json
XDG_CONFIG_HOME=$T/config XDG_STATE_HOME=$T/state QT_QPA_PLATFORM=offscreen LANGUAGE=ja \
  HYPRLAND_INSTANCE_SIGNATURE=$(ls /run/user/1000/hypr/ | head -1) PIXWALL_SNAPSHOT=/tmp/qml.png \
  dbus-run-session -- build/pixwall-settings-qt
```

- 画面の大きさ（見本の形）を本物に合わせるときは、`QT_QPA_PLATFORM=offscreen:configfile=screens.json`（`{"screens": [{"name": "eDP-1", "x": 0, "y": 0, "width": 1600, "height": 1000, "dpr": 1.6}]}`）
- 角の丸みは `hyprctl getoption decoration:rounding -j` を読むだけ。0 のときを見るには、`{"int": 0}` を出す偽の `hyprctl` を `PATH` の前に置く
- GTK 版を並べて撮るときは、broadway（`gtk4-broadwayd :17` と `GDK_BACKEND=broadway BROADWAY_DISPLAY=:17`）で開き、`PIXWALL_SNAPSHOT_CONTENT=1` で中身を撮る。色は `omarchy-gtk-tint <colors.toml> <出力先>` で作った CSS を、一時的な `XDG_CONFIG_HOME/gtk-4.0/gtk.css` から読ませ、明暗は `ADW_DEBUG_COLOR_SCHEME=prefer-dark`
- Material の部品の影（layer の効果）は、offscreen では描かれない（ソフトウェアで描くため）。QML 版はボタン・選ぶ欄・ポップオーバーの地を自分で描き、影を使わないので、offscreen でも本物でも同じに見える

確かめたこと（2026-11-09、cramgent、Qt 6.12）:

- catppuccin（暗い）・flexoki-light（明るい）・Omarchy なし（Material の既定）で、切り替えない・時刻・ダーク／ライト・Omarchy の壁紙（組あり・なし）、日本語と英語を撮り、GTK 版と並べた。文字の切れ・はみ出しは無い
- 開いている間に `current/theme` を丸ごと入れ替える（Omarchy のテーマ切り替えと同じ）と、閉じずに色が替わる。`pixwallctl set fit fill` で書くと、開いたまま並べ方が替わる
- 丸みは cramgent の Hyprland の 10 でカード・ボタンが丸く、入力欄は 4。偽の `hyprctl` で 0 にすると全部角張る
- 同じ D-Bus で、QML 版を開いたまま QML 版・GTK 版を開くと、どちらもすぐ終わる（開いている方が前に出る）。GTK 版を開いたまま QML 版を開いても同じ
- 確かめていないこと: 本物の Hyprland の窓として開くこと（本人の画面に窓を出さないため）、ファイル・色を選ぶダイアログ、選ぶ欄を開いた一覧の見た目、pixwall が動いているとき（「一時停止」が押せる）の見た目

