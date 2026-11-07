#!/bin/bash
# 画面の無い GNOME Shell（1280×720 の画面が1つ）で、pixwall の拡張機能を試す。linux/ で:
#   tests/gnome-headless/start.sh &        # 動かし続ける（止めるのは pkill -f '[g]nome-shell --headless'）
#   source tests/gnome-headless/env.sh     # 以後のコマンドを試験のシェルに向ける
# 本物のデスクトップの設定（dconf）に触れないよう、D-Bus を起こす前から HOME と XDG の場所を試験用にする。
# dbus-run-session の中で export しても、D-Bus が起こす dconf-service は本物の HOME のまま書くので、必ずこの形で
set -euo pipefail
LINUX="$(cd "$(dirname "$0")/../.." && pwd)"
T="${PIXWALL_TEST_DIR:-${TMPDIR:-/tmp}/pixwall-gnome-test}"
RUN=/tmp/pxrt-$UID  # Wayland のソケットのパスは 108 バイトまでなので短く
rm -rf "$T"
mkdir -p "$T"/{home,config/pixwall,cache,data/gnome-shell/extensions} "$RUN"
chmod 700 "$RUN"
EXT="$T/data/gnome-shell/extensions"
cp -r "$LINUX/tests/gnome-headless/pixwall-test@mooma0750.xyz" "$EXT/"
mkdir -p "$EXT/pixwall@mooma0750.xyz"
cp "$LINUX"/gnome-extension/pixwall@mooma0750.xyz/* "$EXT/pixwall@mooma0750.xyz/"
cat > "$LINUX/tests/gnome-headless/env.sh" <<ENV
export HOME=$T/home XDG_CONFIG_HOME=$T/config XDG_DATA_HOME=$T/data XDG_CACHE_HOME=$T/cache
export XDG_RUNTIME_DIR=$RUN WAYLAND_DISPLAY=wl-px XDG_CURRENT_DESKTOP=GNOME
export DBUS_SESSION_BUS_ADDRESS=\$(cat $T/bus)
# シェルの中で JS を評価する・画面を撮る・押す
px_eval() { gdbus call --session --dest xyz.mooma0750.pixwallTest --object-path /xyz/mooma0750/pixwallTest --method xyz.mooma0750.pixwallTest.Eval "\$1"; }
px_shot() { gdbus call --session --dest xyz.mooma0750.pixwallTest --object-path /xyz/mooma0750/pixwallTest --method xyz.mooma0750.pixwallTest.Shot "\$1"; }
px_click() { px_eval "(() => { const C = imports.gi.Clutter; const GLib = imports.gi.GLib; globalThis._vp ??= global.stage.context.get_backend().get_default_seat().create_virtual_device(C.InputDeviceType.POINTER_DEVICE); const t = GLib.get_monotonic_time(); _vp.notify_absolute_motion(t, \$1, \$2); _vp.notify_button(t + 1000, C.BUTTON_PRIMARY, C.ButtonState.PRESSED); _vp.notify_button(t + 80000, C.BUTTON_PRIMARY, C.ButtonState.RELEASED); return 'clicked'; })()"; }
PX_WALL='Main.extensionManager.lookup("pixwall@mooma0750.xyz").stateObj._wall'
ENV
exec env -i HOME="$T/home" XDG_DATA_HOME="$T/data" XDG_CONFIG_HOME="$T/config" XDG_CACHE_HOME="$T/cache" \
    XDG_RUNTIME_DIR="$RUN" PATH="$LINUX/build:/usr/bin:/bin" LANG="${LANG:-C.UTF-8}" \
    XDG_CURRENT_DESKTOP=GNOME XDG_SESSION_TYPE=wayland \
    dbus-run-session -- bash -c '
        case "$HOME" in /home/*) echo "refuse: HOME=$HOME" >&2; exit 1;; esac
        echo "$DBUS_SESSION_BUS_ADDRESS" > "'"$T"'/bus"
        gsettings set org.gnome.shell enabled-extensions "[\"pixwall-test@mooma0750.xyz\", \"pixwall@mooma0750.xyz\"]"
        gsettings set org.gnome.shell welcome-dialog-last-shown-version "999"
        exec gnome-shell --headless --wayland --no-x11 --virtual-monitor 1280x720 --wayland-display wl-px'
