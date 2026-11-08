import Foundation

/// コマンドライン（`pixwallctl`）で読み書きできる設定の項目。名前は config.json のキーと同じ
public enum ConfigKey: String, CaseIterable {
    case image
    case fit
    case position
    case freeX
    case freeY
    case freeSize
    case switchBy
    case background
    case backgroundColor
    case speed
    case pauseOnLowPower
    case launchAtLogin
    case keepWallpaperOnQuit
}

/// `pixwallctl` で頼めること。Windows 版の cli.h とそろえる
public enum CliCommand: Equatable {
    case show
    case get(ConfigKey)
    /// 書いた順に当てる
    case set([(ConfigKey, String)])
    case status
    case pause
    case resume
    case refresh
    case quit
    /// frame は何枚目か（0 から）
    case export(path: String, frame: Int)
    case help

    public static func == (a: CliCommand, b: CliCommand) -> Bool {
        switch (a, b) {
        case let (.set(x), .set(y)): return x.map(\.0) == y.map(\.0) && x.map(\.1) == y.map(\.1)
        case (.show, .show), (.status, .status), (.pause, .pause), (.resume, .resume), (.refresh, .refresh),
             (.quit, .quit), (.help, .help): return true
        case let (.get(x), .get(y)): return x == y
        case let (.export(p, f), .export(q, g)): return p == q && f == g
        default: return false
        }
    }
}

public struct CliError: Error, Equatable {
    public var message: String

    public init(_ message: String) {
        self.message = message
    }
}

public let cliUsage = """
    usage: pixwallctl <command>

      show                         print the settings (config.json)
      get <key>                    print one setting
      set <key> <value> [...]      change settings; the wallpaper follows at once
      status                       print running, paused or stopped
      pause | resume               pause or resume the animation
      refresh                      count the screens again and redraw
      quit                         quit pixwall (the OS wallpaper is restored)
      export <file.png> [--frame N]
                                   write a still image for each screen (frame N, from 1)

    keys and values:
      image            path to a GIF, PNG or WebP
      fit              integer | fit | fill | free
      position         0 to 1 (0 top/left, 0.5 center, 1 bottom/right; fit and fill)
      freeX, freeY     0 to 1 (0 left/top edge, 1 right/bottom edge; free)
      freeSize         0.1 to 3 (1 is the size that fits the screen; free)
      switchBy         none | time | appearance (the times and images are set in the settings)
      background       auto | custom
      backgroundColor  #RRGGBB (used when background is custom)
      speed            0.25 to 4
      pauseOnLowPower  true | false
      launchAtLogin    true | false
      keepWallpaperOnQuit  true | false (keep the first frame as the OS wallpaper after quitting)
    """

/// 引数（コマンドの名前より後）を読む。形が違えば CliError
public func parseCli(_ args: [String]) throws -> CliCommand {
    guard let name = args.first else { throw CliError("no command") }
    let rest = Array(args.dropFirst())
    func noMore(_ command: CliCommand) throws -> CliCommand {
        guard rest.isEmpty else { throw CliError("\(name) takes no arguments") }
        return command
    }
    func key(_ s: String) throws -> ConfigKey {
        guard let k = ConfigKey(rawValue: s) else { throw CliError("unknown key: \(s)") }
        return k
    }
    switch name {
    case "show": return try noMore(.show)
    case "status": return try noMore(.status)
    case "pause": return try noMore(.pause)
    case "resume": return try noMore(.resume)
    case "refresh": return try noMore(.refresh)
    case "quit": return try noMore(.quit)
    case "help", "-h", "--help": return .help
    case "get":
        guard rest.count == 1 else { throw CliError("get takes one key") }
        return .get(try key(rest[0]))
    case "set":
        guard !rest.isEmpty, rest.count % 2 == 0 else { throw CliError("set takes pairs of key and value") }
        return .set(try stride(from: 0, to: rest.count, by: 2).map { (try key(rest[$0]), rest[$0 + 1]) })
    case "export":
        var path: String?
        var frame = 0
        var i = 0
        while i < rest.count {
            if rest[i] == "--frame" {
                guard i + 1 < rest.count, let n = Int(rest[i + 1]), n >= 1 else {
                    throw CliError("--frame takes a number from 1")
                }
                frame = n - 1
                i += 2
            } else if path == nil {
                path = rest[i]
                i += 1
            } else {
                throw CliError("export takes one file")
            }
        }
        guard let path else { throw CliError("export takes a file") }
        return .export(path: path, frame: frame)
    default:
        throw CliError("unknown command: \(name)")
    }
}

extension Config {
    /// `pixwallctl get` が出す値。選んでいない画像・色は空
    public func value(for key: ConfigKey) -> String {
        switch key {
        case .image: return image ?? ""
        case .fit: return fit.rawValue
        case .position: return number(position)
        case .freeX: return number(freeX)
        case .freeY: return number(freeY)
        case .freeSize: return number(freeSize)
        case .switchBy: return switchBy.rawValue
        case .background: return background.rawValue
        case .backgroundColor: return backgroundColor ?? ""
        case .speed: return number(speed)
        case .pauseOnLowPower: return String(pauseOnLowPower)
        case .launchAtLogin: return String(launchAtLogin)
        case .keepWallpaperOnQuit: return String(keepWallpaperOnQuit)
        }
    }

    /// `pixwallctl set` の値を当てる。範囲の外・形の違う値は CliError で、設定は変えない。
    /// 画像のパスはそのまま入れる（絶対パスにする・読めるか確かめるのは呼ぶ側）
    public mutating func set(_ key: ConfigKey, to text: String) throws {
        func double(_ range: ClosedRange<Double>) throws -> Double {
            guard let v = Double(text), v.isFinite, range.contains(v) else {
                throw CliError("\(key.rawValue) must be from \(number(range.lowerBound)) to \(number(range.upperBound))")
            }
            return v
        }
        func bool() throws -> Bool {
            guard let v = Bool(text) else { throw CliError("\(key.rawValue) must be true or false") }
            return v
        }
        switch key {
        case .image:
            guard !text.isEmpty else { throw CliError("image must be a path") }
            image = text
        case .fit:
            guard let v = Fit(rawValue: text) else { throw CliError("fit must be integer, fit, fill or free") }
            fit = v
        case .position: position = try double(0...1)
        case .freeX: freeX = try double(0...1)
        case .freeY: freeY = try double(0...1)
        case .freeSize: freeSize = try double(Placement.freeSizeRange)
        case .switchBy:
            guard let v = SwitchBy(rawValue: text) else { throw CliError("switchBy must be none, time or appearance") }
            switchTo(v)
        case .background:
            guard let v = BackgroundMode(rawValue: text) else { throw CliError("background must be auto or custom") }
            background = v
        case .backgroundColor:
            guard let v = rgb(hex: text) else { throw CliError("backgroundColor must be #RRGGBB") }
            backgroundColor = hex(rgb: v)
        case .speed: speed = try double(0.25...4)
        case .pauseOnLowPower: pauseOnLowPower = try bool()
        case .launchAtLogin: launchAtLogin = try bool()
        case .keepWallpaperOnQuit: keepWallpaperOnQuit = try bool()
        }
    }

    /// config.json に書く形（`pixwallctl show` もこれを出す）
    public func json() throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        return try encoder.encode(self)
    }
}

/// 0.5 は "0.5"、1 は "1"
private func number(_ v: Double) -> String {
    v == v.rounded() && abs(v) < 1e15 ? String(Int(v)) : String(v)
}
