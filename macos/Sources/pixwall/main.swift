import AppKit

// 起動の引数で画像を渡すと、それを動かして設定にも書く: open pixwall.app --args <画像のパス>
// 引数が無ければ設定（config.json）の画像を動かす
let imagePath = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : nil

let app = NSApplication.shared
// Info.plist の LSUIElement と同じ。.app にせずそのまま動かしたときも Dock に出さない
app.setActivationPolicy(.accessory)
let delegate = AppDelegate(imagePath: imagePath)
app.delegate = delegate
app.run()
