#!/bin/bash
# Linux 版を自分のユーザーにだけ入れる（管理者の権限は要らない）。linux/ で ./install.sh
#   ~/.local/bin に pixwall（常駐側）・pixwallctl・pixwall-settings（設定画面）
#   ~/.local/share/applications にランチャー（アプリの一覧の「pixwall」）
#   ~/.local/share/gnome-shell/extensions に GNOME の拡張機能
# 要るもの（Fedora）: gcc-c++ make pkgconf wayland-devel mesa-libEGL-devel mesa-libGLES-devel gtk4-devel
#                     libadwaita-devel gdk-pixbuf2-devel glib2-devel
set -euo pipefail
cd "$(dirname "$0")"
make -j"$(nproc)" all

BIN="$HOME/.local/bin"
APPS="$HOME/.local/share/applications"
UUID="pixwall@mooma0750.xyz"
EXT="$HOME/.local/share/gnome-shell/extensions/$UUID"
mkdir -p "$BIN" "$APPS" "$EXT"

# 動いている pixwall の上書きで落ちないよう、別の名前で置いてから入れ替える
for name in pixwall pixwallctl pixwall-settings; do
    install -m 755 "build/$name" "$BIN/.$name.new"
    mv -f "$BIN/.$name.new" "$BIN/$name"
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
    echo "Hyprland and others: run $BIN/pixwall, or add 'exec-once = $BIN/pixwall' to hyprland.conf."
fi
