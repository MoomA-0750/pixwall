// 画面で使う色・角の丸み・余白。Omarchy のテーマがあれば Theme（theme.cpp）の色、無ければ kasane の控えの色（Kasane.qml。
// アイコンの色が種）。角の丸みは Hyprland に訊けたらそれ、訊けなければ kasane の角丸。明暗は Theme.dark
pragma Singleton
import QtQuick
import Pixwall

QtObject {
    // kasane の値（kasane-sync で写したもの。手で直さない）
    readonly property Kasane kasane: Kasane {}
    readonly property bool omarchy: Theme.omarchy
    readonly property bool dark: Theme.dark
    readonly property var k: kasane.colors(dark)

    readonly property color accent: omarchy ? Theme.accent : k.accent
    readonly property color onAccent: omarchy ? Theme.onAccent : k.onAccent
    readonly property color background: omarchy ? Theme.background : k.bg        // 窓の地
    readonly property color view: omarchy ? Theme.view : k.surface              // 一覧・入力欄・横の欄
    readonly property color raised: omarchy ? Theme.raised : k.surface2         // 見出しの帯・カード・ダイアログ
    readonly property color foreground: omarchy ? Theme.foreground : k.fg
    readonly property color border: omarchy ? Theme.border : k.lineStrong
    readonly property color red: omarchy ? Theme.red : k.danger
    readonly property color onRed: omarchy ? Theme.onRed : k.onDanger
    readonly property color green: omarchy ? Theme.green : k.success
    readonly property color yellow: omarchy ? Theme.yellow : k.warn

    // 角の丸み。hyprland: Hyprland の丸みが分かるか
    readonly property bool hyprland: Theme.radius >= 0
    readonly property int panelRadius: hyprland ? Theme.radius : kasane.radius.card     // 板（Group）
    readonly property int buttonRadius: hyprland ? Theme.radius : kasane.radius.control // ボタン
    readonly property int fieldRadius: hyprland ? Theme.smallRadius : kasane.radius.field  // 入力欄・選ぶ欄
    readonly property int smallRadius: hyprland ? Theme.smallRadius : kasane.radius.sm   // 小さい部品
    readonly property int toastRadius: hyprland ? Theme.radius : kasane.radius.lg

    // 余白の段（s4 = 16）
    readonly property var space: kasane.space
}
