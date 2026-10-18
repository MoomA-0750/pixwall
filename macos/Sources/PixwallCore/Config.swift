import Foundation

/// 設定。`config.json` にこの形で書く。Windows 版とそろえる
public struct Config: Codable, Equatable {
    public var version = 1
    /// 動かす画像のパス。今はすべての画面に同じ画像を出す
    public var image: String?
    public var fit: Fit = .integer
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
        speed = min(max((try? c.decodeIfPresent(Double.self, forKey: .speed)) ?? d.speed, 0.25), 4)
        pauseOnLowPower = (try? c.decodeIfPresent(Bool.self, forKey: .pauseOnLowPower)) ?? d.pauseOnLowPower
        launchAtLogin = (try? c.decodeIfPresent(Bool.self, forKey: .launchAtLogin)) ?? d.launchAtLogin
    }

    /// 無い・壊れているときは既定の設定
    public static func load(from url: URL) -> Config {
        guard let data = try? Data(contentsOf: url),
              let config = try? JSONDecoder().decode(Config.self, from: data) else { return Config() }
        return config
    }

    public func save(to url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        try encoder.encode(self).write(to: url, options: .atomic)
    }
}
