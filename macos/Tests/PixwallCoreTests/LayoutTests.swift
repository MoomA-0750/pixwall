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

    // 画面を埋めるときは、はみ出す向きに動く。0 で左・上の端、1 で右・下の端がそろう
    func testFillCanBeMovedInsideTheOverflow() {
        XCTAssertEqual(layout(imageWidth: 320, imageHeight: 180, screenWidth: 2880, screenHeight: 1800, fit: .fill, position: 0),
                       PixelRect(x: 0, y: 0, width: 3200, height: 1800))
        XCTAssertEqual(layout(imageWidth: 320, imageHeight: 180, screenWidth: 2880, screenHeight: 1800, fit: .fill, position: 1),
                       PixelRect(x: -320, y: 0, width: 3200, height: 1800))
        XCTAssertEqual(layout(imageWidth: 100, imageHeight: 200, screenWidth: 1000, screenHeight: 1000, fit: .fill, position: 0.25),
                       PixelRect(x: 0, y: -250, width: 1000, height: 2000))
        // 中央は前と同じ（はみ出しが奇数でも）
        XCTAssertEqual(layout(imageWidth: 3, imageHeight: 1, screenWidth: 4, screenHeight: 3, fit: .fill),
                       PixelRect(x: -2, y: 0, width: 9, height: 3))
    }

    // 自由に置く: 画面に合わせた大きさ×freeSize。横・縦は別々に、余白（はみ出し）の中の割合で置く
    func testFreeSizeAndPosition() {
        func free(_ x: Double, _ y: Double, _ size: Double) -> PixelRect {
            layout(imageWidth: 320, imageHeight: 180, screenWidth: 2880, screenHeight: 1800,
                   placement: Placement(fit: .free, freeX: x, freeY: y, freeSize: size))
        }
        // 1 は画面に合わせたときと同じ大きさ（位置は横・縦とも中央）
        XCTAssertEqual(free(0.5, 0.5, 1), PixelRect(x: 0, y: 90, width: 2880, height: 1620))
        // 半分の大きさで、左下の隅
        XCTAssertEqual(free(0, 1, 0.5), PixelRect(x: 0, y: 990, width: 1440, height: 810))
        // 右上
        XCTAssertEqual(free(1, 0, 0.5), PixelRect(x: 1440, y: 0, width: 1440, height: 810))
        // 画面より大きくすると、はみ出す向きに動く（0 で左・上の端がそろう）
        XCTAssertEqual(free(0, 0, 2), PixelRect(x: 0, y: 0, width: 5760, height: 3240))
        XCTAssertEqual(free(1, 1, 2), PixelRect(x: -2880, y: -1440, width: 5760, height: 3240))
        // 範囲の外は端の値にする
        XCTAssertEqual(free(-1, 2, 100), free(0, 1, 3))
        XCTAssertEqual(free(0.5, 0.5, 0), free(0.5, 0.5, 0.1))
        // position はほかの並べ方の値で、自由に置くときは使わない
        XCTAssertEqual(layout(imageWidth: 320, imageHeight: 180, screenWidth: 2880, screenHeight: 1800,
                              placement: Placement(fit: .free, position: 0, freeSize: 0.5)),
                       PixelRect(x: 720, y: 495, width: 1440, height: 810))
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
