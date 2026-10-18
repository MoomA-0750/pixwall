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
        XCTAssertTrue(c.pauseOnLowPower)
        XCTAssertTrue(c.launchAtLogin)
    }

    func testRoundTrip() throws {
        var c = Config()
        c.image = "/Users/a/b c.gif"
        c.fit = .fill
        c.speed = 2
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
    }

    func testBrokenOrUnknownValuesFallBack() throws {
        let url = dir.appendingPathComponent("config.json")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        try #"{"fit": "tile?", "speed": 100, "image": "/x.gif"}"#.write(to: url, atomically: true, encoding: .utf8)
        let c = Config.load(from: url)
        XCTAssertEqual(c.fit, .integer)
        XCTAssertEqual(c.speed, 4)
        XCTAssertEqual(c.image, "/x.gif")
        // 前の版の設定には launchAtLogin が無い。既定のオンにする
        XCTAssertTrue(c.launchAtLogin)

        try "not json".write(to: url, atomically: true, encoding: .utf8)
        XCTAssertEqual(Config.load(from: url), Config())
    }
}
