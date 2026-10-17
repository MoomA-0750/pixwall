import AppKit
import PixwallCore

final class AppDelegate: NSObject, NSApplicationDelegate {
    private let imageURL: URL
    private var image: AnimatedImage?
    private var windows: [WallpaperWindow] = []
    private var pause = PauseReasons() {
        didSet { windows.forEach { $0.sharedPause = pause } }
    }
    private var rebuildWork: DispatchWorkItem?

    init(imageURL: URL) {
        self.imageURL = imageURL
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        guard let image = AnimatedImage(url: imageURL) else {
            NSLog("pixwall: 画像を読めない: %@", imageURL.path)
            NSApp.terminate(nil)
            return
        }
        self.image = image
        pause.lowPower = ProcessInfo.processInfo.isLowPowerModeEnabled
        observe()
        rebuild()
    }

    func applicationWillTerminate(_ notification: Notification) {
        windows.forEach { $0.stop() }
    }

    /// 画面を数え直して、画面ごとに窓を作り直す。フレームは読み直さない
    private func rebuild() {
        guard let image else { return }
        windows.forEach { $0.stop() }
        windows = NSScreen.screens.map { screen in
            let w = WallpaperWindow(screen: screen, image: image, fit: .integer, speed: 1)
            w.sharedPause = pause
            return w
        }
        windows.forEach { $0.start() }
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
        nc.addObserver(forName: ProcessInfo.powerStateDidChangeNotification, object: nil, queue: .main) { [weak self] _ in
            self?.pause.lowPower = ProcessInfo.processInfo.isLowPowerModeEnabled
        }

        let ws = NSWorkspace.shared.notificationCenter
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
