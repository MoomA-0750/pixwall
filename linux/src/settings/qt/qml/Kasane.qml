// 書き出したもの。直すのは kasane の tokens.css と themes/personal.css（scripts/build.mjs）
// kasane 1.1.0  source 5cf2e411d01aaae4  seed #FF5A7A #1E1B2E
import QtQuick

// kasane の値。登録はアプリの仕組みのまま（singleton にするか、1つ作って渡す）。明暗はアプリの Theme.dark で選ぶ
// 角丸は控え: Omarchy（Hyprland の decoration:rounding）の丸さが分かるときは、アプリがそちらを使う
QtObject {
    component Radii: QtObject {
        readonly property real sm: 6
        readonly property real base: 10
        readonly property real lg: 16
        readonly property real min: 2
        readonly property real control: 10
        readonly property real field: 10
        readonly property real card: 26
        readonly property real row: 26
    }
    component Spaces: QtObject {
        readonly property real s1: 4
        readonly property real s2: 8
        readonly property real s3: 12
        readonly property real s4: 16
        readonly property real s5: 20
        readonly property real s6: 24
        readonly property real s8: 32
        readonly property real s10: 40
    }
    component Palette: QtObject {
        required property color bg
        required property color surface
        required property color surface2
        required property color raised
        required property color input
        required property color fg
        required property color muted
        required property color faint
        required property color line
        required property color lineStrong
        required property color hover
        required property color active
        required property color accent
        required property color accentStrong
        required property color accentSoft
        required property color onAccent
        required property color fill
        required property color onFill
        required property color fillHover
        required property color link
        required property color focus
        required property color danger
        required property color dangerSoft
        required property color dangerFill
        required property color dangerFillHover
        required property color onDanger
        required property color warn
        required property color warnSoft
        required property color success
        required property color successSoft
        required property color codeBg
        required property color codeFg
        required property color backdrop
        required property color scrim
        required property color onMedia
        required property color mediaControl
    }

    readonly property Radii radius: Radii {}
    readonly property Spaces space: Spaces {}
    // 控えの色（Omarchy のテーマが無いとき）。色は #AARRGGBB
    readonly property Palette light: Palette {
        bg: "#FFF4F4F7"
        surface: "#FFFDFDFE"
        surface2: "#FFECECF0"
        raised: "#FFFEFEFF"
        input: "#FFFEFEFF"
        fg: "#FF2B2B2F"
        muted: "#FF5A5A61"
        faint: "#FF8B8B93"
        line: "#213A3944"
        lineStrong: "#8F2D2D37"
        hover: "#0F3A3944"
        active: "#1A3A3944"
        accent: "#FFB93D56"
        accentStrong: "#FFA12543"
        accentSoft: "#24B93D56"
        onAccent: "#FFFFFFFF"
        fill: "#FFB93D56"
        onFill: "#FFFFFFFF"
        fillHover: "#FFA83B51"
        link: "#FFB93D56"
        focus: "#FFB93D56"
        danger: "#FFC52940"
        dangerSoft: "#1AC52940"
        dangerFill: "#FFC52940"
        dangerFillHover: "#FFB2293E"
        onDanger: "#FFFFFFFF"
        warn: "#FFA14F09"
        warnSoft: "#1FA14F09"
        success: "#FF177553"
        successSoft: "#1F177553"
        codeBg: "#247F7F87"
        codeFg: "#FFB6303C"
        backdrop: "#660B0B0E"
        scrim: "#99000000"
        onMedia: "#FFFFFFFF"
        mediaControl: "#26FFFFFF"
    }
    readonly property Palette dark: Palette {
        bg: "#FF171719"
        surface: "#FF1F1F21"
        surface2: "#FF262629"
        raised: "#FF2B2B2D"
        input: "#FF1A1A1C"
        fg: "#FFE7E7EA"
        muted: "#FFB0B0B5"
        faint: "#FF7A7A7E"
        line: "#1AEEEEF4"
        lineStrong: "#5CEEEEF4"
        hover: "#0FEEEEF4"
        active: "#1AEEEEF4"
        accent: "#FFE77686"
        accentStrong: "#FFFF8C9C"
        accentSoft: "#24E77686"
        onAccent: "#FF250E11"
        fill: "#FFE77686"
        onFill: "#FF250E11"
        fillHover: "#FFE78492"
        link: "#FFE77686"
        focus: "#FFE77686"
        danger: "#FFFF757C"
        dangerSoft: "#1AFF757C"
        dangerFill: "#FFFF757C"
        dangerFillHover: "#FFFD838A"
        onDanger: "#FF2B0B09"
        warn: "#FFFFA156"
        warnSoft: "#1FFFA156"
        success: "#FF53AD81"
        successSoft: "#1F53AD81"
        codeBg: "#337F7F86"
        codeFg: "#FFFF757C"
        backdrop: "#99000000"
        scrim: "#99000000"
        onMedia: "#FFFFFFFF"
        mediaControl: "#26FFFFFF"
    }

    function colors(isDark: bool): Palette { return isDark ? dark : light }
    // 同心の角丸: 中の角丸 = max(radius-min, 外 − 余白)。外がピルなら使わない
    function innerRadius(outer: real, padding: real): real { return Math.max(radius.min, outer - padding) }
    // Hyprland の丸さに従うとき: 下限もアプリが渡す（Omarchy の既定の角張った 0 なら 0 まで）
    function innerRadiusWith(outer: real, padding: real, minimum: real): real { return Math.max(minimum, outer - padding) }
}
