import AppKit

// 試作の間は、動かす画像を起動の引数で渡す: open pixwall.app --args <画像のパス>
guard CommandLine.arguments.count > 1 else {
    FileHandle.standardError.write("使い方: pixwall <画像のパス>\n".data(using: .utf8)!)
    exit(2)
}
let url = URL(fileURLWithPath: CommandLine.arguments[1])

let app = NSApplication.shared
// Info.plist の LSUIElement と同じ。.app にせずそのまま動かしたときも Dock に出さない
app.setActivationPolicy(.accessory)
let delegate = AppDelegate(imageURL: url)
app.delegate = delegate
app.run()
