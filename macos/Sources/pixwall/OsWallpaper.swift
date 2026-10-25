import AppKit
import PixwallCore

/// OS の壁紙（NSWorkspace の desktopImage）に、動かしている画像の1枚目を入れる。
/// Mission Control や、壁紙からメニューバーの色を決める macOS で、見た目が pixwall とそろう。
/// 入れる前の壁紙は画面ごとに `desktop/original.json` に覚え、restore で戻す。
/// 写真・ダイナミック・空撮などの壁紙は NSWorkspace からは読めず（DefaultDesktop.heic と返る）、
/// ほかの操作スペースの壁紙も替えられないので、macOS の壁紙の設定ファイル（WallpaperAgent の Index.plist）も
/// 入れる前に控え、終えたときに書き戻して WallpaperAgent に読み直させる
final class OsWallpaper {
    private struct Original: Codable {
        var url: String
        var scaling: Int?
        var allowClipping: Bool?
        /// 余白の色（sRGB の r, g, b, a）
        var fillColor: [Double]?
    }

    private let dir = Resident.configURL.deletingLastPathComponent().appendingPathComponent("desktop")
    private var originalsURL: URL { dir.appendingPathComponent("original.json") }
    /// macOS の壁紙の設定ファイル。公開されていないので、無ければ（形が変われば）NSWorkspace だけで戻す
    private let storeURL = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("com.apple.wallpaper/Store/Index.plist")
    private var storeBackupURL: URL { dir.appendingPathComponent("wallpaper-store.plist") }
    /// 画面の UUID → 入れる前の壁紙
    private var originals: [String: Original]?
    /// 最後に入れたときの中身の印
    private var applied: String?

    /// 画面ごとに1枚目を描いて入れる。画像・並べ方・位置・余白の色・画面の大きさが前と同じで、自分の PNG が入ったままなら何もしない
    func apply(_ image: AnimatedImage, imagePath: String, placement: Placement, background: CGColor) {
        var originals = loadOriginals()
        let screens = NSScreen.screens.compactMap { screen in Self.uuid(of: screen).map { (screen, $0) } }
        guard !screens.isEmpty else { return }
        let mark = ([imagePath, "\(placement)", "\(background.components ?? [])"] + screens.map { screen, id in
            let size = StillImage.pixelSize(of: screen)
            return "\(id)=\(size.width)x\(size.height)"
        }).joined(separator: "|")
        let allOurs = screens.allSatisfy { screen, _ in isOurs(NSWorkspace.shared.desktopImageURL(for: screen)) }
        if mark == applied, allOurs { return }

        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        updateStoreBackup()

        // 入れる前の壁紙を覚える。自分の PNG が入っている画面は、前に覚えた値のまま（落ちて戻せなかったときなど）
        for (screen, id) in screens {
            guard let current = NSWorkspace.shared.desktopImageURL(for: screen), !isOurs(current) else { continue }
            let options = NSWorkspace.shared.desktopImageOptions(for: screen) ?? [:]
            originals[id] = Original(
                url: current.path,
                scaling: options[.imageScaling] as? Int,
                allowClipping: options[.allowClipping] as? Bool,
                fillColor: (options[.fillColor] as? NSColor)?.usingColorSpace(.sRGB).map {
                    [$0.redComponent, $0.greenComponent, $0.blueComponent, $0.alphaComponent].map(Double.init)
                })
        }
        saveOriginals(originals)

        // 同じ名前だと OS が前の絵を使い回すので、毎回名前を変える
        let stamp = Int(Date().timeIntervalSince1970 * 1000)
        var keep: Set<String> = []
        for (screen, id) in screens {
            let size = StillImage.pixelSize(of: screen)
            let url = dir.appendingPathComponent("\(id)-\(size.width)x\(size.height)-\(stamp).png")
            guard let still = StillImage.render(image, frame: 0, width: size.width, height: size.height,
                                                    placement: placement, background: background),
                  StillImage.writePNG(still, to: url) else { continue }
            // 画面の大きさちょうどに描いたので、OS には縦横をそのまま合わせてもらう（物理ピクセルで 1 対 1 になる）
            let options: [NSWorkspace.DesktopImageOptionKey: Any] = [
                .imageScaling: NSImageScaling.scaleAxesIndependently.rawValue,
                .allowClipping: false,
                .fillColor: NSColor(cgColor: background) ?? .black,
            ]
            keep.insert(url.lastPathComponent)
            do {
                try NSWorkspace.shared.setDesktopImageURL(url, for: screen, options: options)
            } catch {
                NSLog("pixwall: OS の壁紙を入れられない: \(error)")
            }
        }
        applied = mark
        // 前の PNG を消す。ほかの操作スペースがまだ使っているもの（設定ファイルが名前を挙げているもの）は残る
        removePNGs(except: keep)
    }

    /// 入れる前の壁紙に戻す。本人がその間に壁紙を替えていた画面はそのままにする
    func restore() {
        applied = nil
        let originals = loadOriginals()
        if restoreStore() {
            saveOriginals([:])
            removePNGs(except: [])
            return
        }
        // 設定ファイルで戻せないとき（控えが無い・どの操作スペースも自分の PNG でない）は、画面ごとに NSWorkspace で戻す
        try? FileManager.default.removeItem(at: storeBackupURL)
        for screen in NSScreen.screens {
            guard let id = Self.uuid(of: screen), let original = originals[id],
                  isOurs(NSWorkspace.shared.desktopImageURL(for: screen)) else { continue }
            var options: [NSWorkspace.DesktopImageOptionKey: Any] = [:]
            if let s = original.scaling { options[.imageScaling] = s }
            if let c = original.allowClipping { options[.allowClipping] = c }
            if let f = original.fillColor, f.count == 4 {
                options[.fillColor] = NSColor(srgbRed: f[0], green: f[1], blue: f[2], alpha: f[3]) // apple-ui-allow: 本人の元の壁紙の色を戻す
            }
            do {
                try NSWorkspace.shared.setDesktopImageURL(URL(fileURLWithPath: original.url), for: screen,
                                                          options: options)
            } catch {
                NSLog("pixwall: OS の壁紙を戻せない: \(error)")
            }
        }
        saveOriginals([:])
        removePNGs(except: [])
    }

    /// macOS の壁紙の設定ファイルを控える。自分の PNG を指していない項目（操作スペースごと・上の段の項目ごと）だけを
    /// 控えに写す（起動したときはほかの操作スペースが本人の壁紙のままで、移ったときに初めてそこへ入れるので）
    private func updateStoreBackup() {
        guard let current = Self.loadStore(storeURL) else { return }
        Self.saveStore(WallpaperStore.backup(current: current, previous: Self.loadStore(storeBackupURL),
                                             marker: Self.marker), to: storeBackupURL)
    }

    /// 設定ファイルの、自分の PNG を指している項目を控えの値に戻し、WallpaperAgent を起こし直して読ませる。
    /// 全部の操作スペースと、写真などの壁紙まで元どおりになる。戻し方は WallpaperStore.restored
    private func restoreStore() -> Bool {
        guard let current = Self.loadStore(storeURL), WallpaperStore.mentions(current, Self.marker) else { return false }
        guard let result = WallpaperStore.restored(current: current, backup: Self.loadStore(storeBackupURL),
                                                   marker: Self.marker) else {
            NSLog("pixwall: 壁紙の設定ファイルを戻せる形にできない")
            return false
        }
        guard Self.saveStore(result, to: storeURL) else {
            NSLog("pixwall: 壁紙の設定ファイルを戻せない")
            return false
        }
        try? FileManager.default.removeItem(at: storeBackupURL)
        // WallpaperAgent は終わると launchd がすぐ起こし直し、そのときに設定ファイルを読む
        let kill = Process()
        kill.executableURL = URL(fileURLWithPath: "/usr/bin/killall")
        kill.arguments = ["WallpaperAgent"]
        try? kill.run()
        kill.waitUntilExit()
        return kill.terminationStatus == 0
    }

    /// 設定ファイルの中では、壁紙の場所は file URL の文字のまま入っている
    private static let marker = Data("/pixwall/desktop/".utf8)

    private static func loadStore(_ url: URL) -> [String: Any]? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        return (try? PropertyListSerialization.propertyList(from: data, format: nil)) as? [String: Any]
    }

    @discardableResult
    private static func saveStore(_ store: [String: Any], to url: URL) -> Bool {
        guard let data = try? PropertyListSerialization.data(fromPropertyList: store, format: .binary, options: 0)
        else { return false }
        return (try? data.write(to: url, options: .atomic)) != nil
    }

    private func isOurs(_ url: URL?) -> Bool {
        guard let url else { return false }
        return url.standardizedFileURL.path.hasPrefix(dir.standardizedFileURL.path + "/")
    }

    private func loadOriginals() -> [String: Original] {
        if let originals { return originals }
        let loaded = (try? Data(contentsOf: originalsURL))
            .flatMap { try? JSONDecoder().decode([String: Original].self, from: $0) } ?? [:]
        originals = loaded
        return loaded
    }

    private func saveOriginals(_ value: [String: Original]) {
        originals = value
        if value.isEmpty {
            try? FileManager.default.removeItem(at: originalsURL)
            return
        }
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        try? encoder.encode(value).write(to: originalsURL, options: .atomic)
    }

    /// 作った PNG を消す。macOS の壁紙の設定ファイル（とその控え）がまだ名前を挙げているものは残す
    /// （ほかの操作スペースの壁紙になったままのことがあるので）
    private func removePNGs(except keep: Set<String>) {
        let files = (try? FileManager.default.contentsOfDirectory(atPath: dir.path)) ?? []
        let stores = [storeURL, storeBackupURL].compactMap { try? Data(contentsOf: $0) }
        for name in files where name.hasSuffix(".png") && !keep.contains(name) {
            let bytes = Data(name.utf8)
            if stores.contains(where: { $0.range(of: bytes) != nil }) { continue }
            try? FileManager.default.removeItem(at: dir.appendingPathComponent(name))
        }
    }

    /// 画面の UUID。画面をつなぎ直しても変わらない
    private static func uuid(of screen: NSScreen) -> String? {
        guard let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber,
              let uuid = CGDisplayCreateUUIDFromDisplayID(number.uint32Value)?.takeRetainedValue() else { return nil }
        return CFUUIDCreateString(nil, uuid) as String
    }
}
