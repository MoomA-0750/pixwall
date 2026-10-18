// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "pixwall",
    platforms: [.macOS(.v13)],
    targets: [
        // OS に触らない計算（並べ方・待ち時間・止める条件）。単体テストの対象
        .target(name: "PixwallCore"),
        .executableTarget(name: "pixwall", dependencies: ["PixwallCore"]),
        .testTarget(name: "PixwallCoreTests", dependencies: ["PixwallCore"]),
    ]
)
