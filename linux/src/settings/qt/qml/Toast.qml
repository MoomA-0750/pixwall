// 下に少しだけ出す知らせ（GTK 版の AdwToast）
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Pixwall

Popup {
    id: toast
    property alias text: label.text
    function show(message) {
        label.text = message
        open()
        timer.restart()
    }
    x: (parent.width - width) / 2
    y: parent.height - height - 24
    padding: Look.space.s3
    leftPadding: 18
    rightPadding: 18
    closePolicy: Popup.CloseOnPressOutside
    Material.foreground: Look.foreground
    background: Rectangle {
        color: Look.raised
        radius: Look.toastRadius
        border.color: Look.border
    }
    contentItem: Label {
        id: label
        wrapMode: Text.Wrap
        width: Math.min(implicitWidth, toast.parent.width - 80)
    }
    Timer {
        id: timer
        interval: 3000
        onTriggered: toast.close()
    }
}
