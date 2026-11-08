#!/bin/bash
# Linux 版を自分のユーザーにだけ入れる（管理者の権限は要らない）。linux/ で
#   ./install.sh            本体: ~/.local/bin に pixwall（常駐側）・pixwallctl・pixwall-settings（設定画面の選び役）・
#                           pixwall-settings-gtk（GTK 版の設定画面）、~/.local/share/applications にランチャー
#                           （アプリの一覧の「pixwall」）、~/.local/share/gnome-shell/extensions に GNOME の拡張機能
#   ./install.sh --qt       QML 版の設定画面 pixwall-settings-qt だけを足す（Omarchy・Hyprland・KDE などで選ばれる）
#   ./install.sh --remove-qt  QML 版だけを外す（選び役は GTK 版に戻る）
# 要るもの（Fedora）: gcc-c++ make pkgconf wayland-devel mesa-libEGL-devel mesa-libGLES-devel gtk4-devel
#                     libadwaita-devel gdk-pixbuf2-devel glib2-devel
#   --qt にはさらに（Arch）qt6-base qt6-declarative（Wayland で開くなら qt6-wayland）、（Fedora）qt6-qtbase-devel
#   qt6-qtdeclarative-devel
set -euo pipefail
cd "$(dirname "$0")"

BIN="$HOME/.local/bin"

# 動いているプログラムの上書きで落ちないよう、別の名前で置いてから入れ替える
put() {
    install -m 755 "build/$1" "$BIN/.$1.new"
    mv -f "$BIN/.$1.new" "$BIN/$1"
}

case "${1:-}" in
    --qt)
        make -j"$(nproc)" qt
        mkdir -p "$BIN"
        put pixwall-settings-qt
        echo "pixwall: installed the QML settings to $BIN/pixwall-settings-qt"
        echo "         (pixwall-settings opens it outside GNOME; PIXWALL_UI=gtk or qt chooses one)"
        exit 0
        ;;
    --remove-qt)
        rm -f "$BIN/pixwall-settings-qt"
        echo "pixwall: removed the QML settings; pixwall-settings opens the GTK settings"
        exit 0
        ;;
    "") ;;
    *)
        echo "usage: $0 [--qt | --remove-qt]" >&2
        exit 2
        ;;
esac

make -j"$(nproc)" all
APPS="$HOME/.local/share/applications"
UUID="pixwall@mooma0750.xyz"
EXT="$HOME/.local/share/gnome-shell/extensions/$UUID"
mkdir -p "$BIN" "$APPS" "$EXT"

for name in pixwall pixwallctl pixwall-settings pixwall-settings-gtk; do
    put "$name"
done

# アプリの一覧から開く。動いていれば設定画面が開き、動いていなければ始める（Mac・Windows と同じ）
cat > "$APPS/xyz.mooma0750.pixwall.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=pixwall
Comment=Animated GIF wallpaper with sharp pixels
Comment[ja]=GIF を動く壁紙にする
Exec=$BIN/pixwall
Icon=preferences-desktop-wallpaper
Categories=Settings;DesktopSettings;
StartupNotify=false
DESKTOP

cp gnome-extension/$UUID/{metadata.json,extension.js,lib.js} "$EXT/"

echo "pixwall: installed to $BIN"
if [[ "${XDG_CURRENT_DESKTOP:-}" == *GNOME* ]]; then
    gnome-extensions enable "$UUID" 2>/dev/null || true
    echo "GNOME: the extension is turned on. If the wallpaper doesn't move, log out and log in once"
    echo "       (GNOME on Wayland loads a new extension only at login)."
else
    echo "Hyprland and others: run $BIN/pixwall. It starts at login by itself with uwsm (Omarchy and others);"
    echo "       otherwise start it from your Hyprland config (exec-once = $BIN/pixwall, or launch_on_start in an Omarchy Lua config)."
fi
