import AppKit
import PixwallCore
import QuartzCore

/// 1つの画面の、デスクトップのアイコンの後ろに置く窓。絵が変わる瞬間だけ layer の contents を差し替える
final class WallpaperWindow: NSWindow {
    private let targetScreen: NSScreen
    private let image: AnimatedImage
    private let fit: Fit
    private let speed: Double
    private let imageLayer = CALayer()
    private var frameIndex = 0
    private var timer: DispatchSourceTimer?
    private var nextDeadline = DispatchTime.now()

    /// AppDelegate が持つ、全画面に共通の止める理由
    var sharedPause = PauseReasons() { didSet { updateRunning() } }
    private var occluded = false { didSet { updateRunning() } }

    init(screen: NSScreen, image: AnimatedImage, fit: Fit, speed: Double) {
        targetScreen = screen
        self.image = image
        self.fit = fit
        self.speed = speed
        super.init(contentRect: screen.frame, styleMask: .borderless, backing: .buffered, defer: false)

        level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.desktopWindow)))
        collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle]
        ignoresMouseEvents = true
        isOpaque = true
        hasShadow = false
        isReleasedWhenClosed = false
        backgroundColor = NSColor(cgColor: image.cornerColor) ?? .black

        let root = CALayer()
        root.backgroundColor = image.cornerColor
        imageLayer.magnificationFilter = .nearest
        imageLayer.minificationFilter = .nearest
        imageLayer.contentsGravity = .resize
        // 差し替えたときにふわっと切り替わる既定の動きを切る
        imageLayer.actions = ["contents": NSNull(), "bounds": NSNull(), "position": NSNull()]
        imageLayer.contents = image.frames[0]
        root.addSublayer(imageLayer)

        let view = NSView(frame: NSRect(origin: .zero, size: screen.frame.size))
        // 先に layer を渡してから wantsLayer にすると、AppKit が中身に手を出さない（layer-hosting）
        view.layer = root
        view.wantsLayer = true
        contentView = view

        setFrame(screen.frame, display: false)
        relayout()

        NotificationCenter.default.addObserver(self, selector: #selector(occlusionChanged),
                                               name: NSWindow.didChangeOcclusionStateNotification, object: self)
        NotificationCenter.default.addObserver(self, selector: #selector(backingChanged),
                                               name: NSWindow.didChangeBackingPropertiesNotification, object: self)
    }

    deinit {
        timer?.cancel()
        NotificationCenter.default.removeObserver(self)
    }

    /// メニューバーにかからないように位置を直す既定の動きを止め、画面の端から端まで覆う
    override func constrainFrameRect(_ frameRect: NSRect, to screen: NSScreen?) -> NSRect {
        frameRect
    }

    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }

    /// 今いる画面の物理ピクセルで並べ方を計算し直す
    func relayout() {
        let screen = self.screen ?? targetScreen
        let scale = screen.backingScaleFactor
        let size = screen.frame.size
        let rect = PixwallCore.layout(imageWidth: image.width, imageHeight: image.height,
                          screenWidth: Int((size.width * scale).rounded()),
                          screenHeight: Int((size.height * scale).rounded()),
                          fit: fit)
        // CALayer は左下が原点の点（ポイント）で置く
        imageLayer.contentsScale = scale
        imageLayer.frame = CGRect(x: CGFloat(rect.x) / scale, y: CGFloat(rect.y) / scale,
                                  width: CGFloat(rect.width) / scale, height: CGFloat(rect.height) / scale)
        contentView?.layer?.frame = CGRect(origin: .zero, size: size)
        contentView?.layer?.contentsScale = scale
    }

    func start() {
        orderFront(nil)
        updateRunning()
    }

    func stop() {
        timer?.cancel()
        timer = nil
        orderOut(nil)
    }

    @objc private func occlusionChanged() {
        occluded = !occlusionState.contains(.visible)
    }

    @objc private func backingChanged() {
        relayout()
    }

    private func updateRunning() {
        var reasons = sharedPause
        reasons.occluded = occluded
        let run = isVisible && image.frames.count > 1 && !reasons.shouldPause
        if run, timer == nil {
            nextDeadline = .now()
            scheduleNext()
        } else if !run, let t = timer {
            t.cancel()
            timer = nil
        }
    }

    private func scheduleNext() {
        let delay = frameDelay(image.delays[frameIndex], speed: speed)
        nextDeadline = nextDeadline + delay
        // 遅れすぎたら（止めていた・眠っていた）今から数え直す
        if nextDeadline < .now() { nextDeadline = .now() + delay }

        let t = DispatchSource.makeTimerSource(queue: .main)
        // まとめて起こしてもらえるよう、待ち時間の1割（最大 10ms）の遅れを許す
        let leeway = Int(min(delay * 0.1, 0.01) * 1_000_000)
        t.schedule(deadline: nextDeadline, repeating: .never, leeway: .microseconds(leeway))
        t.setEventHandler { [weak self] in self?.advance() }
        timer?.cancel()
        timer = t
        t.resume()
    }

    private func advance() {
        frameIndex = (frameIndex + 1) % image.frames.count
        imageLayer.contents = image.frames[frameIndex]
        scheduleNext()
    }
}
