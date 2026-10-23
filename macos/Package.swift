// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "pixwall",
    // 設定画面の SettingsModel に @Observable（macOS 14 から）を使う
    platforms: [.macOS(.v14)],
    targets: [
        // OS に触らない計算（並べ方・待ち時間・止める条件）。単体テストの対象
        .target(name: "PixwallCore"),
        .executableTarget(name: "pixwall", dependencies: ["PixwallCore"]),
        .testTarget(name: "PixwallCoreTests", dependencies: ["PixwallCore"]),
    ]
)
