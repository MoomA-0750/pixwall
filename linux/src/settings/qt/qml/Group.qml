// 項目のまとまり（GTK 版の AdwPreferencesGroup）。見出しと説明の下に、行を縦に並べたカードを置く。
// 行どうしの境目の線は各行の上に引き、一番上の行の線はカードの外に出して見せない
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Pixwall

ColumnLayout {
    id: group
    property string title
    property string description
    default property alias rows: column.data
    spacing: 6
    Layout.fillWidth: true

    Label {
        visible: group.title !== ""
        text: group.title
        font.bold: true
        Layout.fillWidth: true
        Layout.leftMargin: 4
        wrapMode: Text.Wrap
    }
    Label {
        visible: group.description !== ""
        text: group.description
        opacity: 0.7
        font.pixelSize: 13
        Layout.fillWidth: true
        Layout.leftMargin: 4
        wrapMode: Text.Wrap
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: column.implicitHeight
        radius: Theme.radius >= 0 ? Theme.radius : 12
        color: Theme.raised
        Item {
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            ColumnLayout {
                id: column
                y: -1
                width: parent.width
                spacing: 0
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.color: Theme.border
            border.width: 1
        }
    }
}
