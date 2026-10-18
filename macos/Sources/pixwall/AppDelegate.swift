import AppKit
import PixwallCore
import ServiceManagement
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    static let configURL = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("pixwall/config.json")

    private var config: Config
    private var image: AnimatedImage?
    private var loadedPath: String?
    private var windows: [WallpaperWindow] = []
    private var pause = PauseReasons() {
        didSet {
            windows.forEach { $0.sharedPause = pause }
            updateCoverageWatch()
        }
    }
    private var rebuildWork: DispatchWorkItem?
    private var coverageTimer: DispatchSourceTimer?
    /// Dock に出るアプリの pid。アプリが起動・終了したときに数え直す
    private var regularApps: Set<pid_t>?
    private lazy var settings = SettingsModel(config: config)
    private var settingsWindow: NSWindow?

    /// imagePath は起動の引数で渡された画像。あれば設定より優先して、設定にも書く
    init(imagePath: String?) {
        config = Config.load(from: Self.configURL)
        if let imagePath { config.image = imagePath }
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        pause.lowPower = ProcessInfo.processInfo.isLowPowerModeEnabled
        settings.onChange = { [weak self] in self?.apply($0) }
        settings.onPause = { [weak self] in self?.pause.userPaused = $0 }
        settings.onRefresh = { [weak self] in self?.rebuild() }
        observe()
        apply(config)
        // 画像をまだ選んでいない・読めないときは、何も見えないので設定画面を出す
        if image == nil { showSettings() }
    }

    func applicationWillTerminate(_ notification: Notification) {
        windows.forEach { $0.stop() }
    }

    /// もう一度起動された（Finder・Launchpad・Spotlight・open から）
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        showSettings()
        return false
    }

    // MARK: - 設定

    private func apply(_ new: Config) {
        config = new
        try? config.save(to: Self.configURL)
        pause.pauseOnLowPower = config.pauseOnLowPower
        updateLoginItem()
        if config.image != loadedPath {
            loadedPath = config.image
            image = config.image.flatMap { AnimatedImage(url: URL(fileURLWithPath: $0)) }
            settings.loadError = (config.image != nil && image == nil) ? config.image : nil
        }
        rebuild()
    }

    /// ログイン項目に入れる・外す。入れたあと、システム設定で本人が切ったときは requiresApproval になり、入れ直さない
    private func updateLoginItem() {
        let service = SMAppService.mainApp
        do {
            if config.launchAtLogin, service.status == .notRegistered || service.status == .notFound {
                try service.register()
            } else if !config.launchAtLogin, service.status == .enabled || service.status == .requiresApproval {
                try service.unregister()
            }
        } catch {
            NSLog("pixwall: ログイン項目を変えられない: \(error)")
        }
        settings.loginItemNeedsApproval = config.launchAtLogin && service.status == .requiresApproval
    }

    private func showSettings() {
        if settingsWindow == nil {
            let window = NSWindow(contentViewController: NSHostingController(rootView: SettingsView(model: settings)))
            window.title = "pixwall"
            window.styleMask = [.titled, .closable, .miniaturizable]
            window.isReleasedWhenClosed = false
            window.delegate = self
            window.center()
            settingsWindow = window
        }
        // 設定画面を開いている間だけ Dock に出す
        NSApp.setActivationPolicy(.regular)
        NSApp.mainMenu = makeMainMenu()
        settingsWindow?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func windowWillClose(_ notification: Notification) {
        guard (notification.object as? NSWindow) === settingsWindow else { return }
        NSApp.setActivationPolicy(.accessory)
    }

    private func makeMainMenu() -> NSMenu {
        let main = NSMenu()
        let appItem = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "設定を閉じる", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "pixwall を終了", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu
        main.addItem(appItem)
        return main
    }

    // MARK: - 壁紙の窓

    /// 画面を数え直して、画面ごとに窓を作り直す。フレームは読み直さない
    private func rebuild() {
        windows.forEach { $0.stop() }
        windows = []
        guard let image else { return }
        windows = NSScreen.screens.map { screen in
            let w = WallpaperWindow(screen: screen, image: image, fit: config.fit, speed: config.speed)
            w.sharedPause = pause
            return w
        }
        windows.forEach { $0.start() }
        updateCoverageWatch()
        checkCoverage()
    }

    // MARK: - ほかの窓に覆われているか

    /// 最大化した窓の後ろでは、メニューバーと Dock のまわりに壁紙が見えていても OS は「見えている」と扱う。
    /// そこで、ほかの窓の位置を時々読んで、覆われた画面では止める。
    /// 動かす必要があるときだけ見張る（ほかの理由で止めている間は見ない）
    private func updateCoverageWatch() {
        let need = !windows.isEmpty && (image?.frames.count ?? 0) > 1 && !pause.shouldPause
        if need, coverageTimer == nil {
            let t = DispatchSource.makeTimerSource(queue: .main)
            // 窓を広げた・縮めたは通知が無いので、時々見る。窓の一覧を読むと 1 回 4〜5ms かかるので（M1）、
            // 3 秒ごとにする。アプリや操作スペースの切り替えは通知ですぐ見る
            t.schedule(deadline: .now() + 3, repeating: 3, leeway: .seconds(1))
            t.setEventHandler { [weak self] in self?.checkCoverage() }
            coverageTimer = t
            t.resume()
        } else if !need, let t = coverageTimer {
            t.cancel()
            coverageTimer = nil
            windows.forEach { $0.covered = false }
        }
    }

    private func checkCoverage() {
        guard coverageTimer != nil else { return }
        if regularApps == nil {
            regularApps = Set(NSWorkspace.shared.runningApplications
                .filter { $0.activationPolicy == .regular }.map(\.processIdentifier))
        }
        let frames = otherWindowFrames(regularApps: regularApps ?? [])
        windows.forEach { $0.covered = isCovered($0.coverageTarget, by: frames) }
    }

    /// 変化が落ち着くまで少し待ってから1回だけ作り直す
    private func scheduleRebuild() {
        rebuildWork?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.rebuild() }
        rebuildWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2, execute: work)
    }

    private func observe() {
        let nc = NotificationCenter.default
        nc.addObserver(forName: NSApplication.didChangeScreenParametersNotification, object: nil, queue: .main) {
            [weak self] _ in self?.scheduleRebuild()
        }
        nc.addObserver(forName: .NSProcessInfoPowerStateDidChange, object: nil, queue: .main) { [weak self] _ in
            self?.pause.lowPower = ProcessInfo.processInfo.isLowPowerModeEnabled
        }

        let ws = NSWorkspace.shared.notificationCenter
        // アプリや操作スペースが切り替わったら、1秒待たずに見直す
        for name in [NSWorkspace.didActivateApplicationNotification, NSWorkspace.activeSpaceDidChangeNotification,
                     NSWorkspace.didHideApplicationNotification, NSWorkspace.didUnhideApplicationNotification,
                     NSWorkspace.didTerminateApplicationNotification] {
            ws.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in self?.checkCoverage() }
        }
        for name in [NSWorkspace.didLaunchApplicationNotification, NSWorkspace.didTerminateApplicationNotification] {
            ws.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in self?.regularApps = nil }
        }
        ws.addObserver(forName: NSWorkspace.screensDidSleepNotification, object: nil, queue: .main) { [weak self] _ in
            self?.pause.screensAsleep = true
        }
        ws.addObserver(forName: NSWorkspace.screensDidWakeNotification, object: nil, queue: .main) { [weak self] _ in
            self?.pause.screensAsleep = false
        }
        ws.addObserver(forName: NSWorkspace.sessionDidResignActiveNotification, object: nil, queue: .main) {
            [weak self] _ in self?.pause.locked = true
        }
        ws.addObserver(forName: NSWorkspace.sessionDidBecomeActiveNotification, object: nil, queue: .main) {
            [weak self] _ in self?.pause.locked = false
        }

        let dnc = DistributedNotificationCenter.default()
        dnc.addObserver(forName: .init("com.apple.screenIsLocked"), object: nil, queue: .main) { [weak self] _ in
            self?.pause.locked = true
        }
        dnc.addObserver(forName: .init("com.apple.screenIsUnlocked"), object: nil, queue: .main) { [weak self] _ in
            self?.pause.locked = false
        }
    }
}
