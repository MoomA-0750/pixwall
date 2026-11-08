// pixwall の QML 版の設定画面。並びと中身は GTK 版（settings/gtk/main.cpp）と同じ
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import Pixwall

ApplicationWindow {
    id: window
    width: 640
    height: snapshot ? 3200 : 860
    visible: true
    title: App.s.WindowTitle
    // 撮るときに、中身の高さで切る
    readonly property int naturalHeight: header.height + content.implicitHeight + 48

    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.accent: Theme.accent
    Material.primary: Theme.accent
    Material.background: Theme.background
    Material.foreground: Theme.foreground
    color: Theme.background

    header: ColumnLayout {
        spacing: 0
        // 見出しの帯
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 46
            color: Theme.raised
            Label {
                anchors.centerIn: parent
                text: window.title
                font.bold: true
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }
        // pixwall が動いていないときの帯
        Rectangle {
            visible: !App.running
            Layout.fillWidth: true
            implicitHeight: 48
            color: Theme.accent
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 10
                Label {
                    text: App.s.NotRunning
                    color: Theme.onAccent
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }
                PwButton {
                    text: App.s.Start
                    // 帯がアクセントの色なので、ボタンは窓の地の色にする
                    fill: Theme.background
                    ink: Theme.foreground
                    onClicked: App.start()
                }
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            id: content
            width: Math.min(600, scroll.availableWidth - 32)
            x: (scroll.availableWidth - width) / 2
            y: 24
            spacing: 24

            // 表示を更新
            Group {
                PrefRow {
                    title: App.s.RefreshLabel
                    PwButton {
                        text: App.s.RefreshButton
                        onClicked: App.refresh()
                    }
                }
            }

            // 画像の切り替え
            Group {
                title: App.s.SwitchLabel
                description: App.s.SwitchHint
                ComboRow {
                    title: App.s.SwitchBy
                    model: App.switchModes
                    currentIndex: App.switchBy
                    onActivated: index => App.setSwitchBy(index)
                }
                ButtonGroup { id: timeGroup }
                Repeater {
                    model: App.switchBy === 1 ? App.times : null
                    delegate: TimeRow { group: timeGroup }
                }
                // 時刻を足す（GTK 版の AdwButtonRow。行の真ん中に置く）
                Item {
                    visible: App.switchBy === 1
                    Layout.fillWidth: true
                    implicitHeight: 52
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.border
                    }
                    PwButton {
                        anchors.centerIn: parent
                        text: "+  " + App.s.AddTime
                        flat: true
                        onClicked: App.addTime()
                    }
                }
                PrefRow {
                    visible: App.switchBy === 2
                    title: App.s.EditAppearance
                    Row {
                        spacing: 0
                        PwButton {
                            text: App.s.Light
                            joinEnd: true
                            accent: App.editingLight
                            onClicked: App.selectAppearance(false)
                        }
                        PwButton {
                            text: App.s.Dark
                            joinStart: true
                            accent: App.editingDark
                            onClicked: App.selectAppearance(true)
                        }
                    }
                }
            }

            // 画像
            Group {
                title: App.imageHeading
                PrefRow {
                    title: App.s.AllDisplays
                    subtitle: App.imageSubtitle
                    PwButton {
                        visible: App.canChoose
                        text: App.s.Choose
                        onClicked: imageDialog.open()
                    }
                }
                // 見本
                Item {
                    visible: App.paired
                    Layout.fillWidth: true
                    implicitHeight: App.previewHeight + 24
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.border
                    }
                    Image {
                        anchors.centerIn: parent
                        source: App.previewSource
                        cache: false
                        smooth: false
                        width: App.previewWidth
                        height: App.previewHeight
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: App.previewWidth + 2
                        height: App.previewHeight + 2
                        color: "transparent"
                        border.color: Theme.border
                    }
                }
                SliderRow {
                    visible: App.paired && App.frameCount > 1
                    title: App.frameTitle
                    from: 1
                    to: Math.max(2, App.frameCount)
                    value: App.frame
                    showValue: false
                    onMoved: value => App.setFrame(value)
                }
                PrefRow {
                    visible: App.paired
                    title: App.s.StillLabel
                    PwButton {
                        text: App.s.ExportButton
                        onClicked: {
                            const name = App.exportName()
                            if (name === "") return
                            exportDialog.selectedFile = exportDialog.currentFolder + "/" + name
                            exportDialog.open()
                        }
                    }
                }
            }

            // 表示
            Group {
                title: App.s.DisplayHeading
                ComboRow {
                    visible: App.paired
                    title: App.s.Fit
                    model: App.fitLabels
                    currentIndex: App.fit
                    onActivated: index => App.setFit(index)
                }
                SliderRow {
                    visible: App.paired && (App.fit === 1 || App.fit === 2)
                    title: App.s.Position
                    subtitle: App.s.PositionHint
                    value: App.position
                    onMoved: value => App.setPosition(value)
                }
                SliderRow {
                    visible: App.paired && App.fit === 3
                    title: App.s.FreeSize
                    subtitle: App.s.FreeSizeHint
                    from: 10
                    to: 300
                    value: App.freeSize
                    onMoved: value => App.setFreeSize(value)
                }
                SliderRow {
                    visible: App.paired && App.fit === 3
                    title: App.s.FreeX
                    subtitle: App.s.FreeXHint
                    value: App.freeX
                    onMoved: value => App.setFreeX(value)
                }
                SliderRow {
                    visible: App.paired && App.fit === 3
                    title: App.s.FreeY
                    subtitle: App.s.FreeYHint
                    value: App.freeY
                    onMoved: value => App.setFreeY(value)
                }
                ComboRow {
                    title: App.s.Background
                    subtitle: App.s.BackgroundHint
                    model: [App.s.BackgroundAuto, App.s.BackgroundCustom]
                    currentIndex: App.customBackground ? 1 : 0
                    onActivated: index => App.setCustomBackground(index === 1)
                    extra: Rectangle {
                        visible: App.customBackground
                        width: 40
                        height: 28
                        radius: Theme.smallRadius
                        color: App.backgroundColor
                        border.color: Theme.border
                        Accessible.name: App.s.Background
                        Accessible.role: Accessible.Button
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                colorDialog.selectedColor = App.backgroundColor
                                colorDialog.open()
                            }
                        }
                    }
                }
                ComboRow {
                    title: App.s.Speed
                    model: App.speedLabels
                    currentIndex: App.speedIndex
                    onActivated: index => App.setSpeed(index)
                }
            }

            // 止める
            Group {
                title: App.s.PauseHeading
                SwitchRow {
                    title: App.s.LowPower
                    checked: App.pauseOnLowPower
                    onToggled: on => App.setPauseOnLowPower(on)
                }
                SwitchRow {
                    title: App.s.Pause
                    enabled: App.running
                    checked: App.paused
                    onToggled: on => App.setPaused(on)
                }
            }

            // 起動（GNOME では拡張機能がログインのたびに動くので出さない）
            Group {
                visible: !App.gnome
                title: App.s.StartupHeading
                SwitchRow {
                    title: App.s.Login
                    subtitle: App.s.LoginHint
                    checked: App.launchAtLogin
                    onToggled: on => App.setLaunchAtLogin(on)
                }
            }

            // 終了。終えたときに GNOME の壁紙を戻さないか（GNOME だけ。ほかには入れ替える OS の壁紙が無い）
            Group {
                SwitchRow {
                    visible: App.gnome
                    title: App.s.Keep
                    checked: App.keepWallpaperOnQuit
                    onToggled: on => App.setKeepWallpaperOnQuit(on)
                }
                PrefRow {
                    title: App.quitLabel
                    PwButton {
                        text: App.s.QuitButton
                        destructive: true
                        onClicked: App.quit()
                    }
                }
            }
        }
    }

    FileDialog {
        id: imageDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [App.s.Images + " (*.gif *.png *.webp *.jpg *.jpeg *.apng *.bmp)"]
        onAccepted: App.chooseImage(selectedFile)
    }
    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG (*.png)"]
        onAccepted: App.exportStill(selectedFile)
    }
    ColorDialog {
        id: colorDialog
        onAccepted: App.setBackgroundColor(selectedColor)
    }

    Toast { id: toast }
    Connections {
        target: App
        function onToast(text) { toast.show(text) }
    }
}
