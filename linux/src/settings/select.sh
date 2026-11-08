#!/bin/sh
# pixwall-settings: 設定画面の選び役。同じフォルダーの GTK 版（pixwall-settings-gtk）か
# QML 版（pixwall-settings-qt）に exec で置き換わる。
#   1. PIXWALL_UI=gtk か qt があれば、それ
#   2. QML 版が入っていて、XDG_CURRENT_DESKTOP に GNOME が無ければ QML 版（Hyprland・Omarchy・KDE など）
#   3. それ以外は GTK 版
# 選んだ方が無ければ、もう一方を出す。どちらも無いときだけ失敗する
self=$(readlink -f "$0")
dir=${self%/*}
gtk="$dir/pixwall-settings-gtk"
qt="$dir/pixwall-settings-qt"

case "${PIXWALL_UI:-}" in
    gtk) first=$gtk second=$qt ;;
    qt) first=$qt second=$gtk ;;
    *)
        case ":${XDG_CURRENT_DESKTOP:-}:" in
            *GNOME*) first=$gtk second=$qt ;;
            *) first=$qt second=$gtk ;;
        esac
        ;;
esac

[ -x "$first" ] && exec "$first" "$@"
[ -x "$second" ] && exec "$second" "$@"
echo "pixwall-settings: neither $gtk nor $qt is installed" >&2
exit 1
