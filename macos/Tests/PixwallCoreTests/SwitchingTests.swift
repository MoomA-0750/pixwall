import Foundation
import PixwallCore
import XCTest

final class SwitchingTests: XCTestCase {
    private func slot(_ image: String, _ fit: Fit = .integer) -> Slot {
        Slot(image: image, placement: Placement(fit: fit))
    }

    func testClock() {
        XCTAssertEqual(minutes(clock: "06:00"), 360)
        XCTAssertEqual(minutes(clock: "6:05"), 365)
        XCTAssertEqual(minutes(clock: "23:59"), 1439)
        XCTAssertNil(minutes(clock: "24:00"))
        XCTAssertNil(minutes(clock: "6:5"))
        XCTAssertNil(minutes(clock: "a:00"))
        XCTAssertNil(minutes(clock: "06:00:00"))
        XCTAssertEqual(clock(minutes: 365), "06:05")
        XCTAssertEqual(clock(minutes: 0), "00:00")
    }

    func testNoneUsesMain() {
        var c = Config()
        c.image = "/main.gif"
        c.timeSlots = [TimeSlot(start: 0, slot: slot("/t.gif"))]
        c.darkSlot = slot("/d.gif")
        XCTAssertEqual(c.activeSlot(minute: 600, dark: true).image, "/main.gif")
        XCTAssertNil(c.minutesToNextSwitch(after: 600))
    }

    func testTime() {
        var c = Config()
        c.image = "/main.gif"
        c.switchBy = .time
        // 始まる時刻の順に書かなくてよい
        c.timeSlots = [TimeSlot(start: 18 * 60, slot: slot("/night.gif", .fill)),
                       TimeSlot(start: 6 * 60, slot: slot("/day.gif"))]
        XCTAssertEqual(c.activeSlot(minute: 6 * 60, dark: false).image, "/day.gif")
        XCTAssertEqual(c.activeSlot(minute: 17 * 60 + 59, dark: false).image, "/day.gif")
        XCTAssertEqual(c.activeSlot(minute: 18 * 60, dark: false), slot("/night.gif", .fill))
        // 0 時から 6 時までは、前の日の 18 時から続く
        XCTAssertEqual(c.activeSlot(minute: 3 * 60, dark: false).image, "/night.gif")
        XCTAssertEqual(c.minutesToNextSwitch(after: 5 * 60), 60)
        XCTAssertEqual(c.minutesToNextSwitch(after: 6 * 60), 12 * 60)
        XCTAssertEqual(c.minutesToNextSwitch(after: 20 * 60), 10 * 60)
        // 出す画像の並べ方だけを使い、ほかの設定は残す
        let r = c.resolved(minute: 19 * 60, dark: false)
        XCTAssertEqual(r.image, "/night.gif")
        XCTAssertEqual(r.fit, .fill)
        XCTAssertEqual(r.timeSlots, c.timeSlots)
    }

    func testTimeWithoutSlotsUsesMain() {
        var c = Config()
        c.image = "/main.gif"
        c.switchBy = .time
        XCTAssertEqual(c.activeSlot(minute: 0, dark: false).image, "/main.gif")
        c.timeSlots = [TimeSlot(start: 600, slot: slot("/only.gif"))]
        XCTAssertEqual(c.activeSlot(minute: 0, dark: false).image, "/only.gif")
        // 時刻が1つだけなら、切り替えることは無い
        XCTAssertNil(c.minutesToNextSwitch(after: 0))
    }

    func testAppearance() {
        var c = Config()
        c.image = "/main.gif"
        c.switchBy = .appearance
        c.darkSlot = slot("/dark.gif", .free)
        XCTAssertEqual(c.activeSlot(minute: 0, dark: true), slot("/dark.gif", .free))
        XCTAssertEqual(c.activeSlot(minute: 0, dark: false).image, "/main.gif")
        c.lightSlot = slot("/light.gif")
        XCTAssertEqual(c.activeSlot(minute: 0, dark: false).image, "/light.gif")
        XCTAssertNil(c.minutesToNextSwitch(after: 0))
    }

    func testSwitchToSeedsFromMain() {
        var c = Config()
        c.image = "/main.gif"
        c.fit = .fill
        c.switchTo(.time)
        XCTAssertEqual(c.timeSlots.map(\.start), [360, 1080])
        XCTAssertEqual(c.timeSlots.map(\.slot), [c.mainSlot, c.mainSlot])
        c.timeSlots[0].slot.image = "/day.gif"
        c.switchTo(.none)
        c.switchTo(.time)
        // 一度決めた時刻は消さない
        XCTAssertEqual(c.timeSlots[0].slot.image, "/day.gif")
        c.switchTo(.appearance)
        XCTAssertEqual(c.lightSlot, c.mainSlot)
        XCTAssertEqual(c.darkSlot, c.mainSlot)
        XCTAssertEqual(c.allImages, ["/main.gif", "/day.gif"])
    }

    func testJson() throws {
        var c = Config()
        c.image = "/main.gif"
        c.switchBy = .time
        c.timeSlots = [TimeSlot(start: 6 * 60 + 30, slot: Slot(image: "/day.gif", placement: Placement(fit: .free, freeSize: 2)))]
        c.darkSlot = slot("/dark.gif")
        let data = try c.json()
        let text = String(decoding: data, as: UTF8.self)
        XCTAssertTrue(text.contains(#""switchBy" : "time""#), text)
        XCTAssertTrue(text.contains(#""start" : "06:30""#), text)
        XCTAssertFalse(text.contains("lightSlot"), text)
        XCTAssertEqual(try JSONDecoder().decode(Config.self, from: data), c)
    }

    func testBrokenSlotsAreSkippedOrClamped() throws {
        let json = #"""
        {"switchBy": "time", "timeSlots": [
          {"start": "7:00", "image": "/a.gif", "fit": "fill", "freeSize": 9, "position": -1},
          "broken",
          {"start": "25:00", "image": "/b.gif"}
        ], "darkSlot": {"fit": "nope"}, "lightSlot": 3}
        """#
        let c = try JSONDecoder().decode(Config.self, from: Data(json.utf8))
        XCTAssertEqual(c.timeSlots.count, 2)
        XCTAssertEqual(c.timeSlots[0].start, 420)
        XCTAssertEqual(c.timeSlots[0].slot.placement.freeSize, 3)
        XCTAssertEqual(c.timeSlots[0].slot.placement.position, 0)
        XCTAssertEqual(c.timeSlots[1].start, 0)
        XCTAssertEqual(c.darkSlot, Slot())
        XCTAssertNil(c.lightSlot)
        // 知らない切り替え方は、切り替えない
        let d = try JSONDecoder().decode(Config.self, from: Data(#"{"switchBy": "weather"}"#.utf8))
        XCTAssertEqual(d.switchBy, .none)
    }

    func testSlotRef() {
        var c = Config()
        c.image = "/main.gif"
        c.timeSlots = [TimeSlot(start: 0, slot: slot("/t.gif"))]
        XCTAssertEqual(c.slot(.time(0)).image, "/t.gif")
        XCTAssertEqual(c.slot(.time(5)).image, "/main.gif")
        XCTAssertEqual(c.slot(.dark).image, "/main.gif")
        c.setSlot(.dark, slot("/d.gif"))
        c.setSlot(.time(5), slot("/x.gif"))
        c.setSlot(.time(0), slot("/t2.gif"))
        XCTAssertEqual(c.darkSlot?.image, "/d.gif")
        XCTAssertEqual(c.timeSlots.map(\.slot.image), ["/t2.gif"])
        XCTAssertEqual(c.image, "/main.gif")
    }

    func testCli() throws {
        var c = Config()
        c.image = "/main.gif"
        try c.set(.switchBy, to: "appearance")
        XCTAssertEqual(c.value(for: .switchBy), "appearance")
        XCTAssertEqual(c.darkSlot?.image, "/main.gif")
        XCTAssertThrowsError(try c.set(.switchBy, to: "weather"))
    }
}
