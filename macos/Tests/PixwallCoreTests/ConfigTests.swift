import Foundation
import PixwallCore
import XCTest

final class ConfigTests: XCTestCase {
    private var dir: URL!

    override func setUpWithError() throws {
        dir = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: dir)
    }

    func testMissingFileGivesDefaults() {
        let c = Config.load(from: dir.appendingPathComponent("config.json"))
        XCTAssertEqual(c, Config())
        XCTAssertNil(c.image)
        XCTAssertEqual(c.fit, .integer)
        XCTAssertEqual(c.speed, 1)
        XCTAssertEqual(c.position, 0.5)
        XCTAssertEqual(c.placement, Placement())
        XCTAssertEqual(c.background, .auto)
        XCTAssertNil(c.backgroundColor)
        XCTAssertNil(c.customBackground)
        XCTAssertTrue(c.pauseOnLowPower)
        XCTAssertTrue(c.launchAtLogin)
    }

    func testRoundTrip() throws {
        var c = Config()
        c.image = "/Users/a/b c.gif"
        c.fit = .fill
        c.speed = 2
        c.position = 0.25
        c.placement.freeX = 0.1
        c.placement.freeY = 0.9
        c.placement.freeSize = 2.5
        c.background = .custom
        c.backgroundColor = "#1E2A3B"
        c.pauseOnLowPower = false
        c.launchAtLogin = false
        let url = dir.appendingPathComponent("pixwall/config.json")
        try c.save(to: url)
        XCTAssertEqual(Config.load(from: url), c)
    }

    func testKeysAreReadable() throws {
        var c = Config()
        c.image = "/a.gif"
        let url = dir.appendingPathComponent("config.json")
        try c.save(to: url)
        let text = try String(contentsOf: url, encoding: .utf8)
        XCTAssertTrue(text.contains(#""image" : "/a.gif""#), text)
        XCTAssertTrue(text.contains(#""fit" : "integer""#), text)

        c.fit = .free
        try c.save(to: url)
        XCTAssertTrue(try String(contentsOf: url, encoding: .utf8).contains(#""fit" : "free""#))
        XCTAssertEqual(Config.load(from: url).fit, .free)
    }

    func testBrokenOrUnknownValuesFallBack() throws {
        let url = dir.appendingPathComponent("config.json")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        try #"{"fit": "tile?", "speed": 100, "position": 3, "image": "/x.gif", "freeX": -1, "freeY": "a", "freeSize": 9}"#
            .write(to: url, atomically: true, encoding: .utf8)
        let c = Config.load(from: url)
        XCTAssertEqual(c.freeX, 0)
        XCTAssertEqual(c.freeY, 0.5)
        XCTAssertEqual(c.freeSize, 3)
        XCTAssertEqual(c.fit, .integer)
        XCTAssertEqual(c.speed, 4)
        XCTAssertEqual(c.position, 1)
        XCTAssertEqual(c.image, "/x.gif")
        // 前の版の設定には launchAtLogin が無い。既定のオンにする
        XCTAssertTrue(c.launchAtLogin)

        // 余白の色: 読めない色は選んでいないのと同じ。小文字は大文字にそろえる
        try ##"{"background": "custom", "backgroundColor": "#12345"}"##.write(to: url, atomically: true, encoding: .utf8)
        XCTAssertEqual(Config.load(from: url).background, .custom)
        XCTAssertNil(Config.load(from: url).backgroundColor)
        XCTAssertNil(Config.load(from: url).customBackground)
        try ##"{"background": "rainbow", "backgroundColor": "#a0b0c0"}"##.write(to: url, atomically: true, encoding: .utf8)
        XCTAssertEqual(Config.load(from: url).background, .auto)
        XCTAssertEqual(Config.load(from: url).backgroundColor, "#A0B0C0")
        XCTAssertNil(Config.load(from: url).customBackground)

        try "not json".write(to: url, atomically: true, encoding: .utf8)
        XCTAssertEqual(Config.load(from: url), Config())
    }

    func testCustomBackground() {
        var c = Config()
        c.backgroundColor = "#FF8000"
        // 色を覚えていても、auto なら使わない
        XCTAssertNil(c.customBackground)
        c.background = .custom
        XCTAssertEqual(c.customBackground, RGB(r: 255, g: 128, b: 0))
        XCTAssertEqual(hex(rgb: RGB(r: 1, g: 2, b: 300)), "#0102FF")
        XCTAssertNil(rgb(hex: "FF8000"))
        XCTAssertNil(rgb(hex: "#GG8000"))
        XCTAssertNil(rgb(hex: "#-FFFFF"))
    }
}
