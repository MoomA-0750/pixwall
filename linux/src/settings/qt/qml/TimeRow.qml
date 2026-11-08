// 時刻の行。左の丸で編集する時刻を選び、時と分の欄で始まる時刻を変える。最後の1つは消せない
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Pixwall

PrefRow {
    id: row
    required property int index
    required property string clock
    required property string file
    required property int hour
    required property int minute
    required property bool now
    required property bool editing
    property ButtonGroup group
    title: clock
    subtitle: file
    clickable: true
    onClicked: App.selectTime(index)
    prefix: RadioButton {
        checked: row.editing
        ButtonGroup.group: row.group
        onClicked: App.selectTime(row.index)
        Accessible.name: row.clock
    }
    Label {
        visible: row.now
        text: App.s.Now
        color: Theme.accent
        font.bold: true
        Layout.rightMargin: 6
    }
    component Clock: SpinBox {
        id: clock
        editable: true
        wrap: true
        implicitWidth: 112
        textFromValue: (value, locale) => (value < 10 ? "0" : "") + value
        valueFromText: (text, locale) => parseInt(text, 10) || 0
        background: FieldBackground { control: clock }
    }
    Clock {
        id: hourBox
        to: 23
        value: row.hour
        Accessible.name: App.s.StartTime
        onValueModified: App.setTimeStart(row.index, value, minuteBox.value)
    }
    Label { text: ":" }
    Clock {
        id: minuteBox
        to: 59
        value: row.minute
        onValueModified: App.setTimeStart(row.index, hourBox.value, value)
    }
    PwButton {
        text: App.s.RemoveTime
        flat: true
        enabled: App.canRemoveTime
        ink: Theme.red
        onClicked: App.removeTime(row.index)
    }
}
