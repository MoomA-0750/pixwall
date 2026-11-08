// 選ぶ行（GTK 版の AdwComboRow）。activated は手で選んだときだけ来る
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Pixwall

PrefRow {
    id: row
    property alias model: combo.model
    property int currentIndex
    property alias extra: extraBox.data
    signal activated(int index)
    ComboBox {
        id: combo
        currentIndex: row.currentIndex
        onActivated: index => row.activated(index)
        implicitWidth: Math.max(180, implicitContentWidth + leftPadding + rightPadding)
        background: FieldBackground { control: combo }
        popup.Material.foreground: Theme.foreground
        // 開いた一覧はポップオーバーの色（Material の影の効果は使わない）
        popup.background: Rectangle {
            color: Theme.raised
            radius: Theme.radius >= 0 ? Theme.radius : 4
            border.color: Theme.border
        }
    }
    // 選ぶ欄の後ろに置くもの（余白の色の見本など。GTK 版と同じ並び）
    Row {
        id: extraBox
        spacing: 6
        // 空のときは並びから外す（空でも並びの間の 6px を取り、選ぶ欄の右端がほかの行の部品よりずれるため）
        visible: visibleChildren.length > 0
    }
}
