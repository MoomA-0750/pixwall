import AppKit
import PixwallCore
import SwiftUI

/// 設定画面のプロセス（`pixwall --settings`）。常駐側が起動し、窓を閉じたら終わる。
/// 変えたら config.json に書いて、常駐側に読み直してもらう（Windows の設定画面と同じ）
@MainActor
final class SettingsApp: NSObject, NSApplicationDelegate {
    /// 最後に書いた設定
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
        model.loginItemNeedsApproval = Resident.updateLoginItem(saved.launchAtLogin)
        model.onChange = { [weak self] in self?.apply($0) }
        model.onPause = { Resident.post(.setPaused, userInfo: ["paused": $0]) }
        model.onRefresh = { Resident.post(.refresh) }
        model.onQuit = { [weak self] in self?.quit() }
        Resident.observe(.showSettings) { [weak self] _ in self?.show() }
        // pixwallctl で変えたとき。古い設定のまま次に書いて、変えた分を消さないように読み直す
        Resident.observe(.reloadConfig) { [weak self] _ in self?.reload() }
        Resident.observe(.setPaused) { [weak self] n in
            let paused = (n.userInfo?["paused"] as? Bool) ?? false
            if self?.model.paused != paused { self?.model.paused = paused }
        }
        Resident.observe(.quit) { _ in NSApp.terminate(nil) }

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
        // 読めない画像は、選ぶときに断っている（SettingsModel.chooseImage）
        if new == saved { return }
        saved = new
        try? new.save(to: Resident.configURL)
        model.loginItemNeedsApproval = Resident.updateLoginItem(new.launchAtLogin)
        Resident.post(.reloadConfig)
    }

    /// 自分で書いたあとにも届くが、そのときは saved と同じなので何もしない
    private func reload() {
        let disk = Config.load(from: Resident.configURL)
        if disk == saved { return }
        saved = disk
        model.config = disk
        model.loadPreview()
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
        appMenu.addItem(withTitle: String(localized: "設定を閉じる"), action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        appMenu.addItem(.separator())
        let quit = appMenu.addItem(withTitle: String(localized: "pixwall を終了"), action: #selector(quit), keyEquivalent: "q")
        quit.target = self
        appItem.submenu = appMenu
        main.addItem(appItem)
        return main
    }
}
