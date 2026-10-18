import AppKit

// 起動の引数で画像を渡すと、それを動かして設定にも書く: open pixwall.app --args <画像のパス>
// 引数が無ければ設定（config.json）の画像を動かす。--settings は設定画面のプロセス（常駐側が起動する）
let arguments = Array(CommandLine.arguments.dropFirst())
let app = NSApplication.shared
let delegate: NSApplicationDelegate
if arguments.contains(Resident.settingsArgument) {
    delegate = SettingsApp(paused: arguments.contains(Resident.pausedArgument))
} else {
    // Info.plist の LSUIElement と同じ。.app にせずそのまま動かしたときも Dock に出さない
    app.setActivationPolicy(.accessory)
    delegate = AppDelegate(imagePath: arguments.first { !$0.hasPrefix("-") })
}
app.delegate = delegate
app.run()
