import AppKit
import PixwallCore

/// 常駐側。壁紙の窓を動かす。設定画面は別のプロセス（SettingsApp）で開く
final class AppDelegate: NSObject, NSApplicationDelegate {
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
    /// 並べ方・位置を変えてから OS の壁紙を描き直すまで待つ。位置のつまみを動かしている間は窓だけを動かし、
    /// PNG を描いて OS に入れる重い処理は止まってから1回だけ行う
    private var osWallpaperWork: DispatchWorkItem?
    private var coverageTimer: DispatchSourceTimer?
    /// どこかでマウスのボタンを離したことを知る（NSEvent のグローバルモニター）
    private var mouseUpMonitor: Any?
    private var mouseUpChecks: [DispatchWorkItem] = []
    /// Dock に出るアプリの pid。アプリが起動・終了したときに数え直す
    private var regularApps: Set<pid_t>?

    /// imagePath は起動の引数で渡された画像。あれば設定より優先して、設定にも書く
    private let imagePath: String?
    private let osWallpaper = OsWallpaper()
    /// ログアウト・再起動・電源を切るところ。OS の壁紙は戻さない（次に始まったときも同じ絵を入れるので）
    private var poweringOff = false

    init(imagePath: String?) {
        config = Config.load(from: Resident.configURL)
        self.imagePath = imagePath
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        pause.lowPower = ProcessInfo.processInfo.isLowPowerModeEnabled
        observe()
        var first = config
        if let imagePath {
            first.image = imagePath
            try? first.save(to: Resident.configURL)
        }
        apply(first)
        // 画像をまだ選んでいない・読めないときは、何も見えないので設定画面を出す
        if image == nil { showSettings() }
    }

    func applicationWillTerminate(_ notification: Notification) {
        windows.forEach { $0.stop() }
        if !poweringOff { osWallpaper.restore() }
    }

    /// もう一度起動された（Finder・Launchpad・Spotlight・open から）
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        showSettings()
        return false
    }

    // MARK: - 設定

    /// 設定は書かない（書くのは設定画面のプロセスと、起動の引数で画像を渡されたとき）。
    /// 画像が同じなら窓は作り直さず、並べ方・位置・余白の色・速さだけを変え、OS の壁紙は少し待ってから描き直す
    private func apply(_ new: Config) {
        let old = config
        config = new
        pause.pauseOnLowPower = config.pauseOnLowPower
        Resident.updateLoginItem(config.launchAtLogin)
        if config.image != loadedPath {
            loadedPath = config.image
            image = config.image.flatMap { AnimatedImage(url: URL(fileURLWithPath: $0)) }
        } else if let image, !windows.isEmpty {
            let background = config.fillColor(for: image)
            windows.forEach {
                $0.update(fit: config.fit, position: config.position, background: background, speed: config.speed)
            }
            if config.fit != old.fit || config.position != old.position || config.customBackground != old.customBackground {
                scheduleOsWallpaper()
            }
            return
        }
        rebuild()
    }

    private func scheduleOsWallpaper() {
        osWallpaperWork?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.applyOsWallpaper() }
        osWallpaperWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5, execute: work)
    }

    /// OS の壁紙にも1枚目を入れる（Mission Control・メニューバーの色・操作スペースの切り替えで見える）
    private func applyOsWallpaper() {
        osWallpaperWork?.cancel()
        osWallpaperWork = nil
        guard let image, let path = config.image else { return }
        osWallpaper.apply(image, imagePath: path, fit: config.fit, position: config.position,
                          background: config.fillColor(for: image))
    }

    /// 設定画面のプロセスを起動する。もう開いていれば、前に出てもらう
    private func showSettings() {
        let others = NSRunningApplication.runningApplications(withBundleIdentifier: Bundle.main.bundleIdentifier ?? "")
            .filter { $0.processIdentifier != getpid() && !$0.isTerminated }
        if !others.isEmpty {
            Resident.post(.showSettings)
            return
        }
        let arguments = [Resident.settingsArgument] + (pause.userPaused ? [Resident.pausedArgument] : [])
        if Bundle.main.bundleIdentifier != nil {
            let launch = NSWorkspace.OpenConfiguration()
            launch.createsNewApplicationInstance = true
            launch.arguments = arguments
            NSWorkspace.shared.openApplication(at: Bundle.main.bundleURL, configuration: launch) { _, error in
                if let error { NSLog("pixwall: 設定画面を起動できない: \(error)") }
            }
        } else if let exe = Bundle.main.executableURL {
            // .app にせずそのまま動かしているとき
            _ = try? Process.run(exe, arguments: arguments)
        }
    }

    // MARK: - 壁紙の窓

    /// 画面を数え直して、画面ごとに窓を作り直す。フレームは読み直さない
    private func rebuild() {
        osWallpaperWork?.cancel()
        osWallpaperWork = nil
        windows.forEach { $0.stop() }
        windows = []
        guard let image, config.image != nil else {
            // 画像をまだ選んでいない・読めない。OS の壁紙も入れ替えていたら戻す
            osWallpaper.restore()
            return
        }
        windows = NSScreen.screens.map { screen in
            let w = WallpaperWindow(screen: screen, image: image, fit: config.fit, position: config.position,
                                    background: config.fillColor(for: image), speed: config.speed)
            w.sharedPause = pause
            return w
        }
        windows.forEach { $0.start() }
        applyOsWallpaper()
        updateCoverageWatch()
        checkCoverage()
    }

    // MARK: - ほかの窓に覆われているか

    /// 最大化した窓の後ろでは、メニューバーと Dock のまわりに壁紙が見えていても OS は「見えている」と扱う。
    /// そこで、ほかの窓の位置を読んで、覆われた画面では止める。
    /// 窓を広げた・縮めたことは通知が来ないので、マウスのボタンを離したとき（緑のボタン・メニューの「Fill」・
    /// タイトルバーのダブルクリック・ふちのドラッグ）に見直し、キーで広げたときのために 3 秒ごとにも見る。
    /// 窓の一覧を読むのは 1 回 0.9ms ほどかかるので（M1）、短い間隔で見続けることはしない。
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
            // マウスのボタンだけなので、アクセシビリティや入力監視の許可は要らない
            mouseUpMonitor = NSEvent.addGlobalMonitorForEvents(matching: .leftMouseUp) { [weak self] _ in
                self?.checkCoverageAfterMouseUp()
            }
        } else if !need, let t = coverageTimer {
            t.cancel()
            coverageTimer = nil
            if let m = mouseUpMonitor { NSEvent.removeMonitor(m) }
            mouseUpMonitor = nil
            mouseUpChecks.forEach { $0.cancel() }
            mouseUpChecks = []
            windows.forEach { $0.covered = false }
        }
    }

    /// 離した直後は、窓が広がる・縮む動きの途中のことがある。少し後と、動き終わるころの2回見る
    /// （M1 で、元に戻すのは離して 0.1 秒、広げるのは 0.37 秒ほどで終わった）
    private func checkCoverageAfterMouseUp() {
        mouseUpChecks.forEach { $0.cancel() }
        mouseUpChecks = [0.1, 0.45].map { delay in
            let work = DispatchWorkItem { [weak self] in self?.checkCoverage() }
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
            return work
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
        // OS の壁紙は操作スペースごとのことがある。切り替えた先にまだ入っていなければ入れる
        ws.addObserver(forName: NSWorkspace.activeSpaceDidChangeNotification, object: nil, queue: .main) {
            [weak self] _ in
            guard let self, let image = self.image, let path = self.config.image, !self.windows.isEmpty else { return }
            self.osWallpaper.apply(image, imagePath: path, fit: self.config.fit, position: self.config.position,
                                   background: self.config.fillColor(for: image))
        }
        ws.addObserver(forName: NSWorkspace.willPowerOffNotification, object: nil, queue: .main) { [weak self] _ in
            self?.poweringOff = true
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

        Resident.observe(.reloadConfig) { [weak self] _ in self?.apply(Config.load(from: Resident.configURL)) }
        Resident.observe(.refresh) { [weak self] _ in self?.rebuild() }
        Resident.observe(.setPaused) { [weak self] n in
            self?.pause.userPaused = (n.userInfo?["paused"] as? Bool) ?? false
        }
        Resident.observe(.quit) { _ in NSApp.terminate(nil) }
        Resident.observe(.ping) { [weak self] _ in
            Resident.post(.pong, userInfo: ["paused": self?.pause.userPaused ?? false])
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
