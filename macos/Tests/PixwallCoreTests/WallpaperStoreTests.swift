import Foundation
import PixwallCore
import XCTest

final class WallpaperStoreTests: XCTestCase {
    let marker = Data("/pixwall/desktop/".utf8)
    let photo: [String: Any] = ["Provider": "com.apple.wallpaper.extension.photos", "Configuration": Data([1, 2])]
    let ours: [String: Any] = [
        "Provider": "com.apple.wallpaper.choice.image",
        "Configuration": Data("file:///Users/a/Library/Application%20Support/pixwall/desktop/x.png".utf8),
    ]
    let idle: [String: Any] = ["Provider": "com.apple.wallpaper.extension.photos"]

    /// 操作スペースひとつ分（実物と同じ形: Default と、画面ごとの Displays）
    func space(_ content: [String: Any]) -> [String: Any] {
        let entry: [String: Any] = ["Desktop": ["Content": content], "Idle": ["Content": idle], "Type": "individual"]
        return ["Default": entry, "Displays": ["SCREEN": entry]]
    }

    func store(spaces: [String: [String: Any]], system: [String: Any], displays: [String: Any]) -> [String: Any] {
        [
            "AllSpacesAndDisplays": ["Idle": ["Content": idle], "Type": "idle"],
            "SystemDefault": ["Desktop": ["Content": system], "Idle": ["Content": idle], "Type": "individual"],
            "Displays": ["SCREEN": ["Desktop": ["Content": displays], "Type": "individual"]],
            "Spaces": spaces.mapValues { space($0) },
        ]
    }

    func plistEqual(_ a: Any?, _ b: Any?) -> Bool {
        guard let a, let b else { return a == nil && b == nil }
        return (a as AnyObject).isEqual(b)
    }

    func testBackupKeepsSpacesThatAreStillTheUsers() {
        // 起動して一番目の操作スペースにだけ入れた。ほかの操作スペースは写真のまま
        let first = store(spaces: ["": photo, "B": photo], system: photo, displays: photo)
        let backup1 = WallpaperStore.backup(current: first, previous: nil, marker: marker)
        // 一番目に入れたあと、二番目へ移ったとき
        let running = store(spaces: ["": ours, "B": photo], system: ours, displays: ours)
        let backup2 = WallpaperStore.backup(current: running, previous: backup1, marker: marker)
        XCTAssertFalse(WallpaperStore.mentions(backup2, marker))
        XCTAssertTrue(plistEqual(backup2["SystemDefault"], first["SystemDefault"]))
        XCTAssertTrue(plistEqual((backup2["Spaces"] as? [String: Any])?[""], space(photo)))
    }

    func testRestorePutsBackEverySpace() throws {
        let original = store(spaces: ["": photo, "B": photo], system: photo, displays: photo)
        let backup = WallpaperStore.backup(current: original, previous: nil, marker: marker)
        let running = store(spaces: ["": ours, "B": ours], system: ours, displays: ours)
        let result = try XCTUnwrap(WallpaperStore.restored(current: running, backup: backup, marker: marker))
        XCTAssertTrue(plistEqual(result, original))
    }

    /// 2026-10-18 に起きたこと: 控えに無い上の段の項目（SystemDefault・Displays）を消して書き戻したら、
    /// WallpaperAgent が設定ファイルを全部決まりの壁紙に作り直した。項目は消さず、中の壁紙だけ差し替える
    func testRestoreKeepsEntriesThatHaveNoBackup() throws {
        // 控えには操作スペースの分しかない
        let backup: [String: Any] = ["Spaces": ["": space(photo)]]
        // C は動かしている間にできた操作スペース（今の操作スペースの壁紙が写される）
        let running = store(spaces: ["": ours, "C": ours], system: ours, displays: ours)
        let result = try XCTUnwrap(WallpaperStore.restored(current: running, backup: backup, marker: marker))
        XCTAssertEqual(Set(result.keys), Set(running.keys))
        XCTAssertEqual(Set((result["Spaces"] as? [String: Any] ?? [:]).keys), ["", "C"])
        XCTAssertFalse(WallpaperStore.mentions(result, marker))
        XCTAssertTrue(plistEqual((result["Spaces"] as? [String: Any])?["C"], space(photo)))
        let system = try XCTUnwrap(result["SystemDefault"] as? [String: Any])
        XCTAssertTrue(plistEqual((system["Desktop"] as? [String: Any])?["Content"], photo))
        XCTAssertTrue(plistEqual(system["Idle"], ["Content": idle]))
    }

    func testRestoreLeavesWhatTheUserChanged() throws {
        let original = store(spaces: ["": photo, "B": photo], system: photo, displays: photo)
        let backup = WallpaperStore.backup(current: original, previous: nil, marker: marker)
        let other: [String: Any] = ["Provider": "com.apple.wallpaper.choice.image", "Configuration": Data([9])]
        // 動かしている間に、本人が二番目の操作スペースの壁紙を替えた
        let running = store(spaces: ["": ours, "B": other], system: ours, displays: ours)
        let result = try XCTUnwrap(WallpaperStore.restored(current: running, backup: backup, marker: marker))
        XCTAssertTrue(plistEqual((result["Spaces"] as? [String: Any])?["B"], space(other)))
        XCTAssertTrue(plistEqual((result["Spaces"] as? [String: Any])?[""], space(photo)))
    }

    func testRestoreGivesUpWithoutTheUsersWallpaper() {
        // 本人の壁紙がどこにも無い（控えが無く、全部自分の PNG）ときは書かない
        let running = store(spaces: ["": ours], system: ours, displays: ours)
        XCTAssertNil(WallpaperStore.restored(current: running, backup: nil, marker: marker))
        // 自分の PNG をどこも指していなければ、戻すものは無い
        let clean = store(spaces: ["": photo], system: photo, displays: photo)
        XCTAssertNil(WallpaperStore.restored(current: clean, backup: nil, marker: marker))
    }
}
