import CoreGraphics
import Foundation
import ImageIO
import PixwallCore

/// 全フレームを元の大きさのまま読んだ画像。画面の窓どうしで共有する
final class AnimatedImage {
    let frames: [CGImage]
    /// フレームごとの、GIF などに書かれた待ち時間（秒）
    let delays: [Double]
    let width: Int
    let height: Int
    /// 余白の色の既定。1枚目の左上の1ドット
    let cornerColor: CGColor

    init?(url: URL) {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil) else { return nil }
        let count = CGImageSourceGetCount(source)
        var frames: [CGImage] = []
        var delays: [Double] = []
        // GIF の重ね方（disposal）は ImageIO が当てて、完成した絵を返す
        let options = [kCGImageSourceShouldCacheImmediately: true] as CFDictionary
        for i in 0..<count {
            guard let image = CGImageSourceCreateImageAtIndex(source, i, options) else { continue }
            frames.append(image)
            delays.append(Self.writtenDelay(source, at: i))
        }
        guard let first = frames.first else { return nil }
        self.frames = frames
        self.delays = delays
        width = first.width
        height = first.height
        cornerColor = Self.topLeftColor(of: first)
    }

    private static func writtenDelay(_ source: CGImageSource, at index: Int) -> Double {
        guard let props = CGImageSourceCopyPropertiesAtIndex(source, index, nil) as? [CFString: Any] else {
            return 0
        }
        let containers: [(CFString, CFString, CFString)] = [
            (kCGImagePropertyGIFDictionary, kCGImagePropertyGIFUnclampedDelayTime, kCGImagePropertyGIFDelayTime),
            (kCGImagePropertyPNGDictionary, kCGImagePropertyAPNGUnclampedDelayTime, kCGImagePropertyAPNGDelayTime),
            (kCGImagePropertyWebPDictionary, kCGImagePropertyWebPUnclampedDelayTime, kCGImagePropertyWebPDelayTime),
        ]
        for (dict, unclamped, clamped) in containers {
            guard let d = props[dict] as? [CFString: Any] else { continue }
            if let v = d[unclamped] as? Double, v > 0 { return v }
            if let v = d[clamped] as? Double { return v }
        }
        return 0
    }

    private static func topLeftColor(of image: CGImage) -> CGColor {
        var pixel: [UInt8] = [0, 0, 0, 0]
        let space = CGColorSpace(name: CGColorSpace.sRGB)!
        pixel.withUnsafeMutableBytes { buf in
            guard let ctx = CGContext(data: buf.baseAddress, width: 1, height: 1, bitsPerComponent: 8,
                                      bytesPerRow: 4, space: space,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
            ctx.interpolationQuality = .none
            // 左上の1ドットだけが 1×1 の中に来るようにずらして描く（CGContext は左下が原点）
            ctx.draw(image, in: CGRect(x: 0, y: -(image.height - 1), width: image.width, height: image.height))
        }
        let a = CGFloat(pixel[3]) / 255
        guard a > 0 else { return CGColor(srgbRed: 0, green: 0, blue: 0, alpha: 1) }
        // 背景は不透明にする。半透明のドットは色だけ使う
        return CGColor(srgbRed: CGFloat(pixel[0]) / 255 / a, green: CGFloat(pixel[1]) / 255 / a,
                       blue: CGFloat(pixel[2]) / 255 / a, alpha: 1)
    }
}
