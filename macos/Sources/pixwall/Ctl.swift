import AppKit
import PixwallCore

/// `pixwallctl`（pixwall.app/Contents/MacOS/pixwallctl。pixwall へのシンボリックリンク）。
/// config.json を読み書きし、常駐側には設定画面と同じ知らせで頼む。終わりの値は、できた 0、できなかった 1、引数の誤り 2
enum Ctl {
    static let name = "pixwallctl"

    static func run(_ args: [String]) -> Int32 {
        let command: CliCommand
        do {
            command = try parseCli(args)
        } catch let e as CliError {
            fail("\(e.message)\n\n\(cliUsage)")
            return 2
        } catch {
            return 2
        }
        let config = Config.load(from: Resident.configURL)
        switch command {
        case .help:
            print(cliUsage)
        case .show:
            guard let data = try? config.json() else { return 1 }
            print(String(decoding: data, as: UTF8.self))
        case let .get(key):
            print(config.value(for: key))
        case let .set(pairs):
            return set(pairs, on: config)
        case .status:
            switch ping() {
            case nil: print("stopped")
            case true?: print("paused")
            case false?: print("running")
            }
        case .pause, .resume, .refresh, .quit:
            guard ping() != nil else {
                fail("pixwall is not running")
                return 1
            }
            switch command {
            case .pause: Resident.post(.setPaused, userInfo: ["paused": true])
            case .resume: Resident.post(.setPaused, userInfo: ["paused": false])
            case .refresh: Resident.post(.refresh)
            default: Resident.post(.quit)
            }
        case let .export(path, frame):
            return export(config, to: path, frame: frame)
        }
        return 0
    }

    /// 全部当ててから1回だけ書く。どれかがだめなら何も書かない
    private static func set(_ pairs: [(ConfigKey, String)], on config: Config) -> Int32 {
        var next = config
        do {
            for (key, value) in pairs {
                try next.set(key, to: key == .image ? absolute(value) : value)
            }
        } catch let e as CliError {
            fail(e.message)
            return 2
        } catch {
            return 2
        }
        // 読めない画像は設定に書かない（設定画面と同じ）
        if next.image != config.image, let image = next.image, AnimatedImage(url: URL(fileURLWithPath: image)) == nil {
            fail("cannot read the image: \(image)")
            return 1
        }
        do {
            try next.save(to: Resident.configURL)
        } catch {
            fail("cannot write \(Resident.configURL.path): \(error.localizedDescription)")
            return 1
        }
        Resident.post(.reloadConfig)
        if ping() == nil { fail("pixwall is not running; it uses the settings when it starts") }
        return 0
    }

    private static func export(_ config: Config, to path: String, frame: Int) -> Int32 {
        guard let imagePath = config.image else {
            fail("no image is chosen")
            return 1
        }
        guard let image = AnimatedImage(url: URL(fileURLWithPath: imagePath)) else {
            fail("cannot read the image: \(imagePath)")
            return 1
        }
        guard frame < image.frames.count else {
            fail("the image has \(image.frames.count) frames")
            return 2
        }
        let url = URL(fileURLWithPath: absolute(path))
        guard StillImage.export(image, imagePath: imagePath, frame: frame, placement: config.placement,
                                background: config.fillColor(for: image), to: url) else {
            fail("cannot write \(url.path)")
            return 1
        }
        return 0
    }

    /// 常駐側が動いていれば、一時停止しているかを返す。0.5 秒待って返事が無ければ nil
    private static func ping() -> Bool? {
        var paused: Bool?
        let observer = DistributedNotificationCenter.default().addObserver(
            forName: Resident.Message.pong.name, object: nil, queue: nil) { n in
            paused = (n.userInfo?["paused"] as? Bool) ?? false
        }
        defer { DistributedNotificationCenter.default().removeObserver(observer) }
        Resident.post(.ping)
        let deadline = Date(timeIntervalSinceNow: 0.5)
        while paused == nil, Date() < deadline {
            RunLoop.current.run(mode: .default, before: min(deadline, Date(timeIntervalSinceNow: 0.05)))
        }
        return paused
    }

    private static func absolute(_ path: String) -> String {
        let expanded = (path as NSString).expandingTildeInPath
        return URL(fileURLWithPath: expanded, relativeTo: URL(fileURLWithPath: FileManager.default.currentDirectoryPath))
            .standardizedFileURL.path
    }

    private static func fail(_ message: String) {
        FileHandle.standardError.write(Data("\(name): \(message)\n".utf8))
    }
}
