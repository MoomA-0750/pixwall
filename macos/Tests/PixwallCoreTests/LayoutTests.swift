import PixwallCore
import XCTest

final class LayoutTests: XCTestCase {
    // MacBook Air M1 の既定（1440×900 の2倍）に 64×64
    func testIntegerPicksLargestWholeScale() {
        let r = layout(imageWidth: 64, imageHeight: 64, screenWidth: 2880, screenHeight: 1800, fit: .integer)
        XCTAssertEqual(r, PixelRect(x: 544, y: 4, width: 1792, height: 1792))
    }

    func testIntegerFallsBackToFitWhenImageIsLarger() {
        let r = layout(imageWidth: 4000, imageHeight: 2000, screenWidth: 2000, screenHeight: 2000, fit: .integer)
        XCTAssertEqual(r, PixelRect(x: 0, y: 500, width: 2000, height: 1000))
    }

    func testFitKeepsWholeImageOnScreen() {
        let r = layout(imageWidth: 320, imageHeight: 180, screenWidth: 2880, screenHeight: 1800, fit: .fit)
        XCTAssertEqual(r, PixelRect(x: 0, y: 90, width: 2880, height: 1620))
    }

    func testFillCoversScreen() {
        let r = layout(imageWidth: 320, imageHeight: 180, screenWidth: 2880, screenHeight: 1800, fit: .fill)
        XCTAssertEqual(r, PixelRect(x: -160, y: 0, width: 3200, height: 1800))
    }

    func testFitCanBeMovedInsideTheMargin() {
        let top = layout(imageWidth: 320, imageHeight: 180, screenWidth: 2880, screenHeight: 1800, fit: .fit, position: 0)
        XCTAssertEqual(top, PixelRect(x: 0, y: 0, width: 2880, height: 1620))
        let bottom = layout(imageWidth: 320, imageHeight: 180, screenWidth: 2880, screenHeight: 1800, fit: .fit, position: 1)
        XCTAssertEqual(bottom, PixelRect(x: 0, y: 180, width: 2880, height: 1620))
        let right = layout(imageWidth: 100, imageHeight: 200, screenWidth: 1000, screenHeight: 1000, fit: .fit, position: 1)
        XCTAssertEqual(right, PixelRect(x: 500, y: 0, width: 500, height: 1000))
        // ほかの並べ方では中央のまま
        let integer = layout(imageWidth: 64, imageHeight: 64, screenWidth: 2880, screenHeight: 1800, fit: .integer, position: 0)
        XCTAssertEqual(integer, PixelRect(x: 544, y: 4, width: 1792, height: 1792))
    }

    // 拡大率が変わってもドットの大きさは物理ピクセルで決まる
    func testSameDotSizeForSamePixels() {
        let a = layout(imageWidth: 64, imageHeight: 36, screenWidth: 1920, screenHeight: 1080, fit: .integer)
        XCTAssertEqual(a.width / 64, 30)
    }
}

final class TimingTests: XCTestCase {
    func testShortDelaysBecome100ms() {
        XCTAssertEqual(frameDelay(0), 0.1)
        XCTAssertEqual(frameDelay(0.01), 0.1)
        XCTAssertEqual(frameDelay(0.02), 0.02)
    }

    func testSpeedIsClamped() {
        XCTAssertEqual(frameDelay(0.1, speed: 2), 0.05, accuracy: 1e-9)
        XCTAssertEqual(frameDelay(0.1, speed: 100), 0.025, accuracy: 1e-9)
        XCTAssertEqual(frameDelay(0.1, speed: 0), 0.4, accuracy: 1e-9)
    }

    func testPauseReasons() {
        var p = PauseReasons()
        XCTAssertFalse(p.shouldPause)
        p.lowPower = true
        XCTAssertTrue(p.shouldPause)
        p.pauseOnLowPower = false
        XCTAssertFalse(p.shouldPause)
        p.occluded = true
        XCTAssertTrue(p.shouldPause)
        p.occluded = false
        p.userPaused = true
        XCTAssertTrue(p.shouldPause)
    }
}
