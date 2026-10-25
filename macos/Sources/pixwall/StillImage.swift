import AppKit
import ImageIO
import PixwallCore
import UniformTypeIdentifiers

/// 静止画。OS の壁紙（常駐側）と「静止画を書き出す」（設定画面）で、壁紙の窓と同じ並べ方の絵を PNG に描く
enum StillImage {
    /// frame 枚目を width × height（物理ピクセル）の絵に並べて描く。余白は background の色、ドットはぼかさない
    static func render(_ image: AnimatedImage, frame: Int, width: Int, height: Int,
                       placement: Placement, background: CGColor) -> CGImage? {
        guard width > 0, height > 0, let space = CGColorSpace(name: CGColorSpace.sRGB),
              let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: space, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue) else { return nil }
        ctx.setFillColor(background)
        ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
        ctx.interpolationQuality = .none
        let rect = layout(imageWidth: image.width, imageHeight: image.height,
                          screenWidth: width, screenHeight: height, placement: placement)
        // layout は左上が原点、CGContext は左下が原点
        ctx.draw(image.frames[min(max(frame, 0), image.frames.count - 1)],
                 in: CGRect(x: rect.x, y: height - rect.y - rect.height, width: rect.width, height: rect.height))
        return ctx.makeImage()
    }

    /// 書きかけで壊れないよう、別のファイルに書いてから入れ替える
    @discardableResult
    static func writePNG(_ image: CGImage, to url: URL) -> Bool {
        let temp = url.deletingLastPathComponent().appendingPathComponent(".\(url.lastPathComponent).tmp")
        guard let dest = CGImageDestinationCreateWithURL(temp as CFURL, UTType.png.identifier as CFString, 1, nil)
        else { return false }
        CGImageDestinationAddImage(dest, image, nil)
        guard CGImageDestinationFinalize(dest) else {
            try? FileManager.default.removeItem(at: temp)
            return false
        }
        do {
            if FileManager.default.fileExists(atPath: url.path) {
                _ = try FileManager.default.replaceItemAt(url, withItemAt: temp)
            } else {
                try FileManager.default.moveItem(at: temp, to: url)
            }
            return true
        } catch {
            try? FileManager.default.removeItem(at: temp)
            return false
        }
    }

    /// 画面の物理ピクセルの大きさ
    static func pixelSize(of screen: NSScreen) -> (width: Int, height: Int) {
        let scale = screen.backingScaleFactor
        return (Int((screen.frame.width * scale).rounded()), Int((screen.frame.height * scale).rounded()))
    }

    /// 「静止画を書き出す」。frame 枚目を、画面ごとにその画面の大きさで描く。主の画面は url に、
    /// ほかの画面は同じフォルダに <画像の名前>-<幅>x<高さ>.png（あれば -2, -3…）で書く
    static func export(_ image: AnimatedImage, imagePath: String, frame: Int, placement: Placement,
                       background: CGColor, to url: URL) -> Bool {
        let screens = NSScreen.screens
        guard !screens.isEmpty else { return false }
        let stem = URL(fileURLWithPath: imagePath).deletingPathExtension().lastPathComponent
        let dir = url.deletingLastPathComponent()
        var ok = true
        for (i, screen) in screens.enumerated() {
            let size = pixelSize(of: screen)
            var out = url
            if i > 0 {
                let base = "\(stem)-\(size.width)x\(size.height)"
                out = dir.appendingPathComponent("\(base).png")
                var n = 2
                while FileManager.default.fileExists(atPath: out.path) {
                    out = dir.appendingPathComponent("\(base)-\(n).png")
                    n += 1
                }
            }
            guard let still = render(image, frame: frame, width: size.width, height: size.height,
                                     placement: placement, background: background) else {
                ok = false
                continue
            }
            ok = writePNG(still, to: out) && ok
        }
        return ok
    }
}
