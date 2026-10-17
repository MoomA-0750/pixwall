/// 並べ方。設定の JSON にもこの名前で書く
public enum Fit: String, Codable, CaseIterable {
    /// 整数倍で最大（既定）。整数倍で収まらない大きな画像は fit と同じ
    case integer
    /// 画面に合わせる（はみ出さない）
    case fit
    /// 画面を埋める（はみ出す）
    case fill
}

/// 物理ピクセルでの大きさと位置
public struct PixelRect: Equatable {
    public var x: Int
    public var y: Int
    public var width: Int
    public var height: Int

    public init(x: Int, y: Int, width: Int, height: Int) {
        self.x = x
        self.y = y
        self.width = width
        self.height = height
    }
}

/// 画像を画面のどこにどの大きさで置くかを、物理ピクセルで決める。
/// 画面の中央に置く。fill では画面より大きくなり、x・y が負になる
public func layout(imageWidth: Int, imageHeight: Int,
                   screenWidth: Int, screenHeight: Int,
                   fit: Fit) -> PixelRect {
    guard imageWidth > 0, imageHeight > 0, screenWidth > 0, screenHeight > 0 else {
        return PixelRect(x: 0, y: 0, width: 0, height: 0)
    }
    let sx = Double(screenWidth) / Double(imageWidth)
    let sy = Double(screenHeight) / Double(imageHeight)

    let width: Int
    let height: Int
    switch fit {
    case .integer where min(sx, sy) >= 1:
        let scale = min(screenWidth / imageWidth, screenHeight / imageHeight)
        width = imageWidth * scale
        height = imageHeight * scale
    case .integer, .fit:
        (width, height) = scaled(imageWidth, imageHeight, by: min(sx, sy))
    case .fill:
        (width, height) = scaled(imageWidth, imageHeight, by: max(sx, sy))
    }
    return PixelRect(x: (screenWidth - width) / 2, y: (screenHeight - height) / 2,
                     width: width, height: height)
}

private func scaled(_ w: Int, _ h: Int, by scale: Double) -> (Int, Int) {
    (max(1, Int((Double(w) * scale).rounded())), max(1, Int((Double(h) * scale).rounded())))
}
