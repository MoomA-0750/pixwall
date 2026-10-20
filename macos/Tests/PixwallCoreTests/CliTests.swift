import PixwallCore
import XCTest

final class CliTests: XCTestCase {
    func testParsesCommands() throws {
        XCTAssertEqual(try parseCli(["show"]), .show)
        XCTAssertEqual(try parseCli(["status"]), .status)
        XCTAssertEqual(try parseCli(["pause"]), .pause)
        XCTAssertEqual(try parseCli(["resume"]), .resume)
        XCTAssertEqual(try parseCli(["refresh"]), .refresh)
        XCTAssertEqual(try parseCli(["quit"]), .quit)
        XCTAssertEqual(try parseCli(["--help"]), .help)
        XCTAssertEqual(try parseCli(["get", "fit"]), .get(.fit))
        XCTAssertEqual(try parseCli(["set", "fit", "fill", "speed", "2"]), .set([(.fit, "fill"), (.speed, "2")]))
        XCTAssertEqual(try parseCli(["export", "a.png"]), .export(path: "a.png", frame: 0))
        XCTAssertEqual(try parseCli(["export", "--frame", "3", "a.png"]), .export(path: "a.png", frame: 2))
    }

    func testRejectsWrongArguments() {
        for args in [[], ["nope"], ["show", "x"], ["get"], ["get", "nope"], ["set"], ["set", "fit"],
                     ["set", "nope", "1"], ["export"], ["export", "a.png", "b.png"], ["export", "a.png", "--frame", "0"],
                     ["export", "a.png", "--frame"]] {
            XCTAssertThrowsError(try parseCli(args), "\(args)")
        }
    }

    func testGetsValues() {
        var c = Config()
        XCTAssertEqual(c.value(for: .image), "")
        XCTAssertEqual(c.value(for: .fit), "integer")
        XCTAssertEqual(c.value(for: .position), "0.5")
        XCTAssertEqual(c.value(for: .speed), "1")
        XCTAssertEqual(c.value(for: .background), "auto")
        XCTAssertEqual(c.value(for: .backgroundColor), "")
        XCTAssertEqual(c.value(for: .pauseOnLowPower), "true")
        c.image = "/a/b.gif"
        XCTAssertEqual(c.value(for: .image), "/a/b.gif")
    }

    func testSetsValues() throws {
        var c = Config()
        try c.set(.fit, to: "fill")
        try c.set(.position, to: "0.25")
        try c.set(.speed, to: "4")
        try c.set(.background, to: "custom")
        try c.set(.backgroundColor, to: "#1e2a3b")
        try c.set(.pauseOnLowPower, to: "false")
        try c.set(.launchAtLogin, to: "false")
        try c.set(.image, to: "/a/b.gif")
        XCTAssertEqual(c.fit, .fill)
        XCTAssertEqual(c.position, 0.25)
        XCTAssertEqual(c.speed, 4)
        XCTAssertEqual(c.customBackground, RGB(r: 0x1E, g: 0x2A, b: 0x3B))
        XCTAssertEqual(c.backgroundColor, "#1E2A3B")
        XCTAssertFalse(c.pauseOnLowPower)
        XCTAssertFalse(c.launchAtLogin)
        XCTAssertEqual(c.image, "/a/b.gif")
    }

    // 範囲の外・形の違う値は受けず、設定は変わらない
    func testRejectsWrongValues() {
        var c = Config()
        let cases: [(ConfigKey, String)] = [
            (.fit, "stretch"), (.position, "1.5"), (.position, "-0.1"), (.position, "nan"), (.speed, "0.1"),
            (.speed, "5"), (.background, "red"), (.backgroundColor, "#12345"), (.backgroundColor, "123456"),
            (.pauseOnLowPower, "yes"), (.image, ""),
        ]
        for (key, value) in cases {
            XCTAssertThrowsError(try c.set(key, to: value), "\(key) \(value)")
        }
        XCTAssertEqual(c, Config())
    }

    // show は config.json と同じ形
    func testJsonIsWhatSaveWrites() throws {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".json")
        defer { try? FileManager.default.removeItem(at: url) }
        var c = Config()
        c.image = "/a/b c.gif"
        try c.save(to: url)
        XCTAssertEqual(try Data(contentsOf: url), try c.json())
    }
}
