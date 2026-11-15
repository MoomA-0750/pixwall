// 入力欄（選ぶ欄・時と分の欄）の地。色は「入力欄」の色（dark_background）、角は小さい部品の丸み（min(4, 丸み)。分からなければ kasane の field）。
// Material の入力欄の枠は角の丸みが 4 に決まっているので、自分で描く
import QtQuick
import QtQuick.Controls.Material
import Pixwall

Rectangle {
    property Item control
    implicitWidth: 120
    implicitHeight: control ? control.Material.textFieldHeight : 40
    radius: Look.fieldRadius
    color: Look.view
    border.width: control && control.activeFocus ? 2 : 1
    border.color: control && control.activeFocus ? Look.accent
        : control && control.hovered ? Look.foreground : Look.border
}
