#!/usr/bin/env python3
# 本物の GNOME の画面を撮る: python3 -I linux/tests/portal_shot.py <出す.png> [<上> <高さ>]
# GNOME Shell の撮影の窓口（org.gnome.Shell.Screenshot）は GNOME 自身の撮影アプリ・ポータルからしか受けない
# （ほかから呼ぶと AccessDenied）。そこで xdg-desktop-portal の Screenshot を確かめの窓なし（interactive=false）で呼ぶ。
# 初めての1回だけ「許可しますか」が出る（許可はポータルの PermissionStore の screenshot に残る）。
# ポータルは ~/Pictures に書くので、言われた場所へ移す。<上> <高さ> を渡すと、その帯だけを残す（ほかの窓を写さない）
import os
import shutil
import sys
import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

bus = Gio.bus_get_sync(Gio.BusType.SESSION)
token = "pixwallshot"
sender = bus.get_unique_name()[1:].replace(".", "_")
handle = f"/org/freedesktop/portal/desktop/request/{sender}/{token}"
loop = GLib.MainLoop()
result = {}

def on_response(conn, sender_name, path, iface, signal, params):
    code, results = params.unpack()
    result["code"] = code
    result["uri"] = results.get("uri")
    loop.quit()

bus.signal_subscribe("org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request", "Response",
                     handle, None, Gio.DBusSignalFlags.NO_MATCH_RULE, on_response)
bus.call_sync("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
              "org.freedesktop.portal.Screenshot", "Screenshot",
              GLib.Variant("(sa{sv})", ("", {"handle_token": GLib.Variant("s", token),
                                             "interactive": GLib.Variant("b", False)})),
              None, Gio.DBusCallFlags.NONE, -1, None)
GLib.timeout_add_seconds(20, loop.quit)
loop.run()
if result.get("code") != 0 or not result.get("uri"):
    print(f"portal_shot: failed {result}", file=sys.stderr)
    sys.exit(1)
taken = Gio.File.new_for_uri(result["uri"]).get_path()
out = sys.argv[1]
if len(sys.argv) >= 4:
    gi.require_version("GdkPixbuf", "2.0")
    from gi.repository import GdkPixbuf
    full = GdkPixbuf.Pixbuf.new_from_file(taken)
    top, height = int(sys.argv[2]), int(sys.argv[3])
    height = max(1, min(height, full.get_height() - top))
    full.new_subpixbuf(0, top, full.get_width(), height).savev(out, "png", [], [])
    os.remove(taken)
else:
    shutil.move(taken, out)
print(out)
