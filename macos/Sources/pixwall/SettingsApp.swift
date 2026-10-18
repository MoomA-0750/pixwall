import AppKit
import PixwallCore
import SwiftUI

/// 設定画面のプロセス（`pixwall --settings`）。常駐側が起動し、窓を閉じたら終わる。
/// 変えたら config.json に書いて、常駐側に読み直してもらう（Windows の設定画面と同じ）
final class SettingsApp: NSObject, NSApplicationDelegate {
    /// 最後に書いた設定。読めない画像を選んだときはここへ戻す
    private var saved = Config.load(from: Resident.configURL)
    private lazy var model = SettingsModel(config: saved)
    private var window: NSWindow?
    private let startPaused: Bool

    init(paused: Bool) {
        startPaused = paused
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        // 設定画面を開いている間だけ Dock に出る（Info.plist では LSUIElement）
        NSApp.setActivationPolicy(.regular)
        NSApp.mainMenu = makeMainMenu()
        model.paused = startPaused
        if let image = saved.image {
            model.image = AnimatedImage(url: URL(fileURLWithPath: image))
            if model.image == nil { model.loadError = image }
        }
        model.loginItemNeedsApproval = Resident.updateLoginItem(saved.launchAtLogin)
        model.onChange = { [weak self] in self?.apply($0) }
        model.onPause = { Resident.post(.setPaused, userInfo: ["paused": $0]) }
        model.onRefresh = { Resident.post(.refresh) }
        model.onQuit = { [weak self] in self?.quit() }
        Resident.observe(.showSettings) { [weak self] _ in self?.show() }

        let window = NSWindow(contentViewController: NSHostingController(rootView: SettingsView(model: model)))
        window.title = "pixwall"
        window.styleMask = [.titled, .closable, .miniaturizable]
        window.center()
        self.window = window
        show()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        show()
        return false
    }

    private func show() {
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func apply(_ new: Config) {
        if new == saved { return }
        if new.image != saved.image, let image = new.image {
            // 読めない画像は設定に書かない。壁紙はそのまま
            guard let loaded = AnimatedImage(url: URL(fileURLWithPath: image)) else {
                model.loadError = image
                let saved = self.saved
                DispatchQueue.main.async { [weak self] in self?.model.config = saved }
                return
            }
            model.loadError = nil
            model.image = loaded
            model.exportResult = nil
        }
        saved = new
        try? new.save(to: Resident.configURL)
        model.loginItemNeedsApproval = Resident.updateLoginItem(new.launchAtLogin)
        Resident.post(.reloadConfig)
    }

    /// 壁紙ごと終える
    @objc private func quit() {
        Resident.post(.quit)
        NSApp.terminate(nil)
    }

    private func makeMainMenu() -> NSMenu {
        let main = NSMenu()
        let appItem = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "設定を閉じる", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        appMenu.addItem(.separator())
        let quit = appMenu.addItem(withTitle: "pixwall を終了", action: #selector(quit), keyEquivalent: "q")
        quit.target = self
        appItem.submenu = appMenu
        main.addItem(appItem)
        return main
    }
}
