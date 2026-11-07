# Linux 版の試し方

この PC（Fedora 44、GNOME 50）でビルドして試す。要るもの: `gcc-c++ make pkgconf wayland-devel mesa-libEGL-devel mesa-libGLES-devel gtk4-devel libadwaita-devel gdk-pixbuf2-devel glib2-devel`（テストの期待値を作り直すときだけ Python の Pillow）。

## ビルドと単体テスト

```sh
cd linux
make            # build/ に pixwall・pixwallctl・pixwall-settings
make test       # core_tests（Windows と共通の core）・linux_tests（GIF を読む・設定のファイル）・拡張機能の計算
```

- `linux_tests` は、`tests/fixtures/expected.txt`（Pillow で重ねた絵の印）と、こちらで読んだ GIF のフレームを突き合わせる。GIF を足したら `python3 -I tests/fixtures/make_expected.py`
- `gnome-extension/tests/run.js` は、拡張機能の `lib.js`（並べ方・覆われ方・待ち時間）を `build/layout_dump`（C++ の core）とばらばらな値で突き合わせ、`pixwallctl --frames`・`--resolve` の出力を読めるかも確かめる

## 入れる

```sh
linux/install.sh
```

`~/.local/bin` に3つ、アプリの一覧に「pixwall」、`~/.local/share/gnome-shell/extensions/pixwall@mooma0750.xyz` に拡張機能を入れる。GNOME（Wayland）は新しく入れた拡張機能をログインし直すまで読まないので、初めて入れたときは一度ログアウトする。

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

## Hyprland で試す

まだ試していない（この PC に Hyprland が無い。Fedora 44 の公式のリポジトリには無く、COPR の `solopasha/hyprland` などから入れる）。layer-shell を持つ合成器なら同じ仕組みで動くので、公式のリポジトリにある Sway を入れ子で動かして試す手もある。

```sh
# Hyprland のセッションで
linux/build/pixwall ../samples/dot-64x64-30f.gif
linux/build/pixwallctl status        # running
```

見ること: すべての画面の端から端まで（バーの下も）出る、クリックがデスクトップに素通しされる、ドットがぼけない（拡大率 1.25 などでも）、全画面の窓（`fullscreen`）にすると CPU 時間が増えない（`ps -o time= -p $(pgrep -x pixwall)` を数秒おきに見る）、画面をつなぐ・外すと面が増える・消える、`pixwallctl quit` で終わる。
