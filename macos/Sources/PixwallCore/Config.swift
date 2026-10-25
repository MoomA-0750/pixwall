import Foundation

/// 余白の色の決め方
public enum BackgroundMode: String, Codable {
    /// 画像の1枚目の左上の1ドットの色（既定）
    case auto
    /// 自分で選んだ色（Config.backgroundColor）
    case custom
}

/// 設定。`config.json` にこの形で書く。Windows 版とそろえる
public struct Config: Codable, Equatable {
    public var version = 1
    /// 動かす画像のパス。今はすべての画面に同じ画像を出す
    public var image: String?
    public var fit: Fit = .integer
    /// 画面に合わせる・埋めるときの位置（0 で上・左、0.5 で中央、1 で下・右）
    public var position: Double = 0.5
    /// 自由に置くときの横・縦の位置（0〜1）と大きさ（画面に合わせたときを 1 とする。0.1〜3）。Placement を見る
    public var freeX: Double = 0.5
    public var freeY: Double = 0.5
    public var freeSize: Double = 1
    public var background: BackgroundMode = .auto
    /// 自分で選んだ余白の色（"#RRGGBB"）。まだ選んでいなければ nil。auto にしても覚えておく
    public var backgroundColor: String?
    /// 速さの倍率（0.25〜4）
    public var speed: Double = 1
    /// 低電力モードのときに止める
    public var pauseOnLowPower = true
    /// ログインしたときに自動で始める
    public var launchAtLogin = true

    public init() {}

    /// 足りない項目・読めない項目は既定の値にする。古い・新しい版の設定でも起動できるように
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let d = Config()
        version = (try? c.decodeIfPresent(Int.self, forKey: .version)) ?? d.version
        image = (try? c.decodeIfPresent(String.self, forKey: .image)) ?? d.image
        fit = (try? c.decodeIfPresent(Fit.self, forKey: .fit)) ?? d.fit
        func unit(_ key: CodingKeys, _ fallback: Double) -> Double {
            min(max((try? c.decodeIfPresent(Double.self, forKey: key)) ?? fallback, 0), 1)
        }
        position = unit(.position, d.position)
        freeX = unit(.freeX, d.freeX)
        freeY = unit(.freeY, d.freeY)
        let sizes = Placement.freeSizeRange
        freeSize = min(max((try? c.decodeIfPresent(Double.self, forKey: .freeSize)) ?? d.freeSize, sizes.lowerBound), sizes.upperBound)
        background = (try? c.decodeIfPresent(BackgroundMode.self, forKey: .background)) ?? d.background
        backgroundColor = ((try? c.decodeIfPresent(String.self, forKey: .backgroundColor)) ?? nil)
            .flatMap(rgb(hex:)).map(hex(rgb:))
        speed = min(max((try? c.decodeIfPresent(Double.self, forKey: .speed)) ?? d.speed, 0.25), 4)
        pauseOnLowPower = (try? c.decodeIfPresent(Bool.self, forKey: .pauseOnLowPower)) ?? d.pauseOnLowPower
        launchAtLogin = (try? c.decodeIfPresent(Bool.self, forKey: .launchAtLogin)) ?? d.launchAtLogin
    }

    /// 並べ方と位置・大きさ
    public var placement: Placement {
        get { Placement(fit: fit, position: position, freeX: freeX, freeY: freeY, freeSize: freeSize) }
        set {
            fit = newValue.fit
            position = newValue.position
            freeX = newValue.freeX
            freeY = newValue.freeY
            freeSize = newValue.freeSize
        }
    }

    /// 余白に使う、自分で選んだ色（0〜255）。auto のとき・色をまだ選んでいないときは nil（画像の左上の色を使う）
    public var customBackground: RGB? {
        background == .custom ? backgroundColor.flatMap(rgb(hex:)) : nil
    }

    /// 無い・壊れているときは既定の設定
    public static func load(from url: URL) -> Config {
        guard let data = try? Data(contentsOf: url),
              let config = try? JSONDecoder().decode(Config.self, from: data) else { return Config() }
        return config
    }

    public func save(to url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try json().write(to: url, options: .atomic)
    }
}

/// 0〜255 の色
public struct RGB: Equatable {
    public var r: Int
    public var g: Int
    public var b: Int

    public init(r: Int, g: Int, b: Int) {
        self.r = r
        self.g = g
        self.b = b
    }
}

/// "#RRGGBB"（大文字・小文字どちらでも）を読む。形が違えば nil
public func rgb(hex: String) -> RGB? {
    guard hex.count == 7, hex.hasPrefix("#"), let v = Int(hex.dropFirst(), radix: 16),
          hex.dropFirst().allSatisfy(\.isHexDigit) else { return nil }
    return RGB(r: v >> 16 & 0xFF, g: v >> 8 & 0xFF, b: v & 0xFF)
}

/// "#RRGGBB"（大文字）にする
public func hex(rgb: RGB) -> String {
    String(format: "#%02X%02X%02X", min(max(rgb.r, 0), 255), min(max(rgb.g, 0), 255), min(max(rgb.b, 0), 255))
}
