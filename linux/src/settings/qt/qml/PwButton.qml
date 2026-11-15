// ボタン。地と文字の色は Omarchy のテーマから、角の丸みは Hyprland の丸みから（訊けなければ Material の既定の丸）。
// Material の影（layer の効果）は使わない（Omarchy の平らな見た目に合わせ、どの描き方でも同じに見えるように）
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Pixwall

Button {
    id: control
    property bool destructive: false
    property bool accent: false  // アクセントの色で塗る（選んでいるトグルなど）
    // つなげて並べるとき（GTK 版の linked）、隣と接する側の角を丸めない
    property bool joinStart: false
    property bool joinEnd: false
    property color fill: destructive ? Look.red
        : accent ? Look.accent
        : flat ? "transparent"
        : Qt.tint(Look.raised, Qt.alpha(Look.foreground, 0.10))
    property color ink: destructive ? Look.onRed
        : accent ? Look.onAccent
        : flat ? Look.accent
        : Look.foreground

    contentItem: Label {
        text: control.text
        font: control.font
        color: control.ink
        opacity: control.enabled ? 1 : 0.5
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        implicitWidth: 64
        implicitHeight: control.Material.buttonHeight
        radius: Look.buttonRadius
        topLeftRadius: control.joinStart ? 0 : radius
        bottomLeftRadius: control.joinStart ? 0 : radius
        topRightRadius: control.joinEnd ? 0 : radius
        bottomRightRadius: control.joinEnd ? 0 : radius
        color: Qt.tint(control.fill, Qt.alpha(control.ink, control.down ? 0.18 : control.hovered ? 0.08 : 0))
        opacity: control.enabled ? 1 : 0.5
        border.width: control.visualFocus ? 2 : 0
        border.color: Look.accent
    }
}
