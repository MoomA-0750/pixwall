import CoreGraphics
import PixwallCore
import XCTest

final class CoverageTests: XCTestCase {
    // MacBook Air M1 の既定。メニューバーと Dock を除いた部分
    let visible = CGRect(x: 0, y: 70, width: 1440, height: 800)

    func testMaximizedWindowCovers() {
        XCTAssertTrue(isCovered(visible, by: [visible]))
        XCTAssertTrue(isCovered(visible, by: [CGRect(x: -10, y: 0, width: 2000, height: 2000)]))
    }

    func testNoWindowsDoNotCover() {
        XCTAssertFalse(isCovered(visible, by: []))
        XCTAssertEqual(uncoveredFraction(of: visible, by: []), 1, accuracy: 1e-9)
    }

    func testHalfWindowDoesNotCover() {
        let left = CGRect(x: 0, y: 70, width: 720, height: 800)
        XCTAssertEqual(uncoveredFraction(of: visible, by: [left]), 0.5, accuracy: 1e-9)
        XCTAssertFalse(isCovered(visible, by: [left]))
    }

    func testTwoTiledWindowsWithGapsCover() {
        // 左右に並べて、ふちに 8pt のすき間
        let left = CGRect(x: 8, y: 78, width: 708, height: 784)
        let right = CGRect(x: 724, y: 78, width: 708, height: 784)
        XCTAssertTrue(isCovered(visible, by: [left, right]))
    }

    func testVisibleStripDoesNotCover() {
        // 右に 100pt 幅の壁紙が見えている
        XCTAssertFalse(isCovered(visible, by: [CGRect(x: 0, y: 70, width: 1340, height: 800)]))
    }

    func testOverlappingWindowsAreNotCountedTwice() {
        let a = CGRect(x: 0, y: 70, width: 1000, height: 800)
        let b = CGRect(x: 400, y: 70, width: 900, height: 800)
        XCTAssertEqual(uncoveredFraction(of: visible, by: [a, b]), 140.0 / 1440, accuracy: 1e-9)
        XCTAssertFalse(isCovered(visible, by: [a, b]))
    }

    func testWindowOnAnotherScreenDoesNotCover() {
        XCTAssertFalse(isCovered(visible, by: [CGRect(x: 1440, y: 0, width: 1920, height: 1080)]))
    }
}
