// スイッチの行（GTK 版の AdwSwitchRow）。行を押しても切り替わる。toggled は手で変えたときだけ来る
import QtQuick
import QtQuick.Controls
import Pixwall

PrefRow {
    id: row
    property bool checked
    signal toggled(bool on)
    clickable: true
    onClicked: row.toggled(!row.checked)
    Switch {
        checked: row.checked
        onToggled: row.toggled(checked)
    }
}
