// 1行（GTK 版の AdwActionRow）。左に題と小さな説明、右に部品（suffix）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Pixwall

Item {
    id: row
    property string title
    property string subtitle
    property alias prefix: prefixBox.data
    default property alias suffix: suffixBox.data
    property bool separator: true
    signal clicked()
    property bool clickable: false
    Layout.fillWidth: true
    implicitHeight: Math.max(52, layout.implicitHeight + 16)

    Rectangle {
        visible: row.separator
        width: parent.width
        height: 1
        color: Theme.border
    }
    MouseArea {
        anchors.fill: parent
        enabled: row.clickable && row.enabled
        onClicked: row.clicked()
    }
    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 10
        RowLayout {
            id: prefixBox
            visible: children.length > 0
            spacing: 0
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2
            Label {
                text: row.title
                visible: text !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                text: row.subtitle
                visible: text !== ""
                opacity: 0.7
                font.pixelSize: 13
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
        RowLayout {
            id: suffixBox
            spacing: 6
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
