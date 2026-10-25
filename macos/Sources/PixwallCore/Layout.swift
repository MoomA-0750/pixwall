/// 並べ方。設定の JSON にもこの名前で書く
public enum Fit: String, Codable, CaseIterable {
    /// 整数倍で最大（既定）。整数倍で収まらない大きな画像は fit と同じ
    case integer
    /// 画面に合わせる（はみ出さない）
    case fit
    /// 画面を埋める（はみ出す）
    case fill
    /// 自由に置く（大きさと横・縦の位置を自分で決める）
    case free
}

/// 並べ方と、それぞれの並べ方で使う位置・大きさ。設定の JSON では Config の同じ名前の項目に書く
public struct Placement: Equatable {
    public var fit: Fit = .integer
    /// 画面に合わせる・埋めるときの位置（0 で上・左、0.5 で中央、1 で下・右）
    public var position = 0.5
    /// 自由に置くときの横の位置（0 で画像の左の端を画面の左の端に、1 で右の端を右の端にそろえる）
    public var freeX = 0.5
    /// 自由に置くときの縦の位置（0 で上の端、1 で下の端をそろえる）
    public var freeY = 0.5
    /// 自由に置くときの大きさ。画面に合わせたときの大きさを 1 とする
    public var freeSize = 1.0

    /// 自由に置くときの大きさの範囲
    public static let freeSizeRange = 0.1...3.0

    public init(fit: Fit = .integer, position: Double = 0.5,
                freeX: Double = 0.5, freeY: Double = 0.5, freeSize: Double = 1) {
        self.fit = fit
        self.position = position
        self.freeX = freeX
        self.freeY = freeY
        self.freeSize = freeSize
    }
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
/// 画面の中央に置く。fill では画面より大きくなり、x・y が負になる。
/// fit と fill では、余白（fill でははみ出し）の中のどこに置くかを position で決める（0 で上・左、0.5 で中央、1 で下・右）。
/// fill の 0 は、画像の上・左の端を画面の端にそろえる（はみ出すのは下・右）
public func layout(imageWidth: Int, imageHeight: Int,
                   screenWidth: Int, screenHeight: Int,
                   fit: Fit, position: Double = 0.5) -> PixelRect {
    layout(imageWidth: imageWidth, imageHeight: imageHeight, screenWidth: screenWidth, screenHeight: screenHeight,
           placement: Placement(fit: fit, position: position))
}

/// free では、画面に合わせたときの大きさに freeSize を掛け、横は freeX、縦は freeY で置く（余白・はみ出しの中の割合。fill の position と同じ決まり）
public func layout(imageWidth: Int, imageHeight: Int,
                   screenWidth: Int, screenHeight: Int,
                   placement: Placement) -> PixelRect {
    let fit = placement.fit
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
    case .free:
        let size = min(max(placement.freeSize, Placement.freeSizeRange.lowerBound), Placement.freeSizeRange.upperBound)
        (width, height) = scaled(imageWidth, imageHeight, by: min(sx, sy) * size)
    }
    func unit(_ v: Double) -> Double { min(max(v, 0), 1) }
    let (px, py): (Double, Double)
    switch fit {
    case .integer: (px, py) = (0.5, 0.5)
    case .fit, .fill: (px, py) = (unit(placement.position), unit(placement.position))
    case .free: (px, py) = (unit(placement.freeX), unit(placement.freeY))
    }
    // 余白×位置を 0 の側へ切り捨てる（0.5 のとき、余白を半分にして切り捨てるのと同じ。はみ出す負の余白でも）
    func place(_ slack: Int, _ p: Double) -> Int { Int((Double(slack) * p).rounded(.towardZero)) }
    return PixelRect(x: place(screenWidth - width, px), y: place(screenHeight - height, py),
                     width: width, height: height)
}

private func scaled(_ w: Int, _ h: Int, by scale: Double) -> (Int, Int) {
    (max(1, Int((Double(w) * scale).rounded())), max(1, Int((Double(h) * scale).rounded())))
}
