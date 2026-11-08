#!/bin/sh
# 設定画面の選び役（src/settings/select.sh）が、PIXWALL_UI・XDG_CURRENT_DESKTOP と、入っている画面の組み合わせで
# 正しい方を選ぶか。偽の pixwall-settings-gtk・-qt（名前を出すだけ）を置いて試す。make test で動かす
set -u
here=$(cd "$(dirname "$0")" && pwd)
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cp "$here/../src/settings/select.sh" "$dir/pixwall-settings"
failures=0

# 置く画面（both / gtk / qt / none）、PIXWALL_UI、XDG_CURRENT_DESKTOP、期待（gtk / qt / fail）
check() {
    rm -f "$dir/pixwall-settings-gtk" "$dir/pixwall-settings-qt"
    for ui in gtk qt; do
        if [ "$1" = both ] || [ "$1" = "$ui" ]; then
            printf '#!/bin/sh\necho %s "$@"\n' "$ui" > "$dir/pixwall-settings-$ui"
            chmod +x "$dir/pixwall-settings-$ui"
        fi
    done
    got=$(env -u PIXWALL_UI -u XDG_CURRENT_DESKTOP ${2:+PIXWALL_UI=$2} ${3:+XDG_CURRENT_DESKTOP=$3} \
        "$dir/pixwall-settings" 2>/dev/null) || got=fail
    if [ "$got" != "$4" ]; then
        echo "FAIL: installed=$1 PIXWALL_UI=$2 XDG_CURRENT_DESKTOP=$3: got '$got', want '$4'" >&2
        failures=$((failures + 1))
    fi
}

check both ""  "Hyprland"     qt
check both ""  "KDE"          qt
check both ""  ""             qt
check both ""  "GNOME"        gtk
check both ""  "ubuntu:GNOME" gtk
check both ""  "GNOME-Classic:GNOME" gtk
check both gtk "Hyprland"     gtk
check both qt  "GNOME"        qt
check both xyz "Hyprland"     qt
check both xyz "GNOME"        gtk
check gtk  ""  "Hyprland"     gtk
check gtk  qt  "Hyprland"     gtk
check qt   ""  "GNOME"        qt
check qt   gtk "GNOME"        qt
check none ""  "Hyprland"     fail
check none gtk "GNOME"        fail

# 引数はそのまま渡す。リンクから呼んでも、リンクの先のフォルダーの画面を選ぶ
check both gtk "" gtk
got=$(PIXWALL_UI=qt "$dir/pixwall-settings" a b)
[ "$got" = "qt a b" ] || { echo "FAIL: arguments: got '$got'" >&2; failures=$((failures + 1)); }
mkdir "$dir/elsewhere"
ln -s "$dir/pixwall-settings" "$dir/elsewhere/pixwall-settings"
got=$(PIXWALL_UI=gtk "$dir/elsewhere/pixwall-settings")
[ "$got" = "gtk" ] || { echo "FAIL: symlink: got '$got'" >&2; failures=$((failures + 1)); }

if [ "$failures" -eq 0 ]; then
    echo "settings_select_test: ok"
else
    echo "settings_select_test: $failures failure(s)" >&2
    exit 1
fi
