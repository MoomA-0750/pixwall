import CoreGraphics

/// target のうち、rects のどれにも覆われていない部分の面積の割合（0〜1）。
/// 長方形から長方形を引いて、残りを足す
public func uncoveredFraction(of target: CGRect, by rects: [CGRect]) -> Double {
    let total = target.width * target.height
    guard total > 0 else { return 0 }
    var rest = [target]
    for cover in rects where !cover.isEmpty {
        rest = rest.flatMap { subtract(cover, from: $0) }
        if rest.isEmpty { return 0 }
    }
    let left = rest.reduce(0) { $0 + $1.width * $1.height }
    return Double(left / total)
}

/// 窓に覆われていて止めてよいか。
/// 並べた窓の間のすき間（Mac の窓のタイル表示のふち）くらいなら覆われたことにする
public func isCovered(_ target: CGRect, by rects: [CGRect], tolerance: Double = 0.05) -> Bool {
    uncoveredFraction(of: target, by: rects) <= tolerance
}

/// rect から cover を引いた残りを、重ならない長方形（最大4つ）で返す
private func subtract(_ cover: CGRect, from rect: CGRect) -> [CGRect] {
    let hit = rect.intersection(cover)
    guard !hit.isNull, !hit.isEmpty else { return [rect] }
    var out: [CGRect] = []
    // 上下の帯は幅いっぱい、左右の帯は重なった高さだけ
    if hit.minY > rect.minY {
        out.append(CGRect(x: rect.minX, y: rect.minY, width: rect.width, height: hit.minY - rect.minY))
    }
    if hit.maxY < rect.maxY {
        out.append(CGRect(x: rect.minX, y: hit.maxY, width: rect.width, height: rect.maxY - hit.maxY))
    }
    if hit.minX > rect.minX {
        out.append(CGRect(x: rect.minX, y: hit.minY, width: hit.minX - rect.minX, height: hit.height))
    }
    if hit.maxX < rect.maxX {
        out.append(CGRect(x: hit.maxX, y: hit.minY, width: rect.maxX - hit.maxX, height: hit.height))
    }
    return out
}
