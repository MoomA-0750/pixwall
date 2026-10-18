import AppKit
import PixwallCore
import QuartzCore

/// 1つの画面の、デスクトップのアイコンの後ろに置く窓。フレームごとの layer を最初に全部置いておき、
/// 絵が変わる瞬間には、見せる layer を切り替えるだけ（contents を差し替えると、そのたびに絵を WindowServer へ送り直す）
final class WallpaperWindow: NSWindow {
    private let targetScreen: NSScreen
    private let image: AnimatedImage
    private var fit: Fit
    private var position: Double
    private var speed: Double
    private let rootLayer = CALayer()
    private let imageLayer = CALayer()
    private var frameLayers: [CALayer] = []
    private var frameIndex = 0
    private var timer: DispatchSourceTimer?
    private var nextDeadline = DispatchTime.now()

    /// AppDelegate が持つ、全画面に共通の止める理由
    var sharedPause = PauseReasons() { didSet { updateRunning() } }
    private var occluded = false { didSet { updateRunning() } }
    /// ほかのアプリの窓が、メニューバーと Dock を除いたところをほぼ覆っている（AppDelegate が見張る）
    var covered = false { didSet { if covered != oldValue { updateRunning() } } }

    /// 窓に覆われたかを見る範囲。メニューバーと Dock のまわりは、隠れていなくても見ない
    var coverageTarget: CGRect { (screen ?? targetScreen).visibleFrame }

    init(screen: NSScreen, image: AnimatedImage, fit: Fit, position: Double, background: CGColor, speed: Double) {
        targetScreen = screen
        self.image = image
        self.fit = fit
        self.position = position
        self.speed = speed
        super.init(contentRect: screen.frame, styleMask: .borderless, backing: .buffered, defer: false)

        level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.desktopWindow)))
        collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle]
        ignoresMouseEvents = true
        isOpaque = true
        hasShadow = false
        isReleasedWhenClosed = false
        backgroundColor = NSColor(cgColor: background) ?? .black

        let root = rootLayer
        root.backgroundColor = background
        // 余白の色を変えたときに、ふわっと変わる既定の動きを切る
        root.actions = ["backgroundColor": NSNull()]
        imageLayer.actions = ["bounds": NSNull(), "position": NSNull()]
        frameLayers = image.frames.enumerated().map { i, frame in
            let layer = CALayer()
            layer.magnificationFilter = .nearest
            layer.minificationFilter = .nearest
            layer.contentsGravity = .resize
            // 切り替えたときにふわっと切り替わる既定の動きを切る
            layer.actions = ["hidden": NSNull(), "bounds": NSNull(), "position": NSNull(), "contentsScale": NSNull()]
            layer.contents = frame
            layer.isHidden = i != 0
            imageLayer.addSublayer(layer)
            return layer
        }
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
                          fit: fit, position: position)
        // layout は左上が原点、CALayer は左下が原点の点（ポイント）で置く
        let screenHeight = Int((size.height * scale).rounded())
        imageLayer.frame = CGRect(x: CGFloat(rect.x) / scale,
                                  y: CGFloat(screenHeight - rect.y - rect.height) / scale,
                                  width: CGFloat(rect.width) / scale, height: CGFloat(rect.height) / scale)
        for layer in frameLayers {
            layer.contentsScale = scale
            layer.frame = imageLayer.bounds
        }
        contentView?.layer?.frame = CGRect(origin: .zero, size: size)
        contentView?.layer?.contentsScale = scale
    }

    /// 並べ方・位置・余白の色・速さだけを変える。フレームの layer は作り直さない（位置のつまみを動かしている間も軽いように）
    func update(fit: Fit, position: Double, background: CGColor, speed: Double) {
        self.fit = fit
        self.position = position
        self.speed = speed
        backgroundColor = NSColor(cgColor: background) ?? .black
        rootLayer.backgroundColor = background
        relayout()
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
        reasons.occluded = occluded || covered
        let run = isVisible && image.frames.count > 1 && !reasons.shouldPause
        if run, timer == nil {
            let t = DispatchSource.makeTimerSource(queue: .main)
            t.setEventHandler { [weak self] in self?.advance() }
            timer = t
            nextDeadline = .now()
            scheduleNext()
            t.resume()
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

        // まとめて起こしてもらえるよう、待ち時間の1割（最大 10ms）の遅れを許す。タイマーは作り直さず、次の時刻を入れ直す
        let leeway = Int(min(delay * 0.1, 0.01) * 1_000_000)
        timer?.schedule(deadline: nextDeadline, repeating: .never, leeway: .microseconds(leeway))
    }

    private func advance() {
        frameLayers[frameIndex].isHidden = true
        frameIndex = (frameIndex + 1) % image.frames.count
        frameLayers[frameIndex].isHidden = false
        scheduleNext()
    }
}
