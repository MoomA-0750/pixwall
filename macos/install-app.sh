#!/bin/sh
# pixwall.app をビルドして /Applications に入れ、入れたものから始める。
# 前に ~/Applications に入れていたものは消す（ログイン項目も、入れた /Applications のものに替わる）
set -eu
cd "$(dirname "$0")"

./build-app.sh
dest=/Applications/pixwall.app
# 動いている pixwall は、片付けてから終わるようにふつうに終わらせる
if pgrep -x pixwall >/dev/null; then
    pkill -x pixwall
    while pgrep -x pixwall >/dev/null; do sleep 0.2; done
fi
rm -rf "$dest" "$HOME/Applications/pixwall.app"
cp -R build/pixwall.app "$dest"
open "$dest"
echo "$dest"
