// つまみの行。値は from〜to（1 刻み）で、動かしている間も壁紙がついてくる。moved は手で動かしたときだけ来る
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Pixwall

PrefRow {
    id: row
    property int value
    property int from: 0
    property int to: 100
    property bool showValue: true
    signal moved(int value)
    Label {
        visible: row.showValue
        text: Math.round(slider.value)
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: 32
    }
    Slider {
        id: slider
        from: row.from
        to: row.to
        stepSize: 1
        snapMode: Slider.SnapAlways
        value: row.value
        Layout.preferredWidth: 220
        Accessible.name: row.title
        onMoved: row.moved(Math.round(value))
    }
}
