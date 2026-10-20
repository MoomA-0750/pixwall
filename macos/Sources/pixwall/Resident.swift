import AppKit
import PixwallCore
import ServiceManagement

/// 常駐側（壁紙を動かすプロセス）と設定画面のプロセス・pixwallctl のやり取り。DistributedNotificationCenter で知らせる。
/// 設定画面は別のプロセスにして、閉じたら終わる（SwiftUI が使ったメモリは、窓を捨てても常駐側に残るため）
enum Resident {
    /// 設定画面のプロセスとして起動するときの引数
    static let settingsArgument = "--settings"
    /// 設定画面を起動するときに、今一時停止しているかを渡す引数
    static let pausedArgument = "--paused"

    static let configURL = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("pixwall/config.json")

    enum Message: String {
        /// 設定画面 → 常駐側: config.json を読み直して
        case reloadConfig
        /// 設定画面 → 常駐側: 表示を更新して
        case refresh
        /// 設定画面 → 常駐側: 一時停止する・やめる（userInfo の paused）
        case setPaused
        /// 設定画面 → 常駐側: 終わって
        case quit
        /// 常駐側 → 設定画面: 前に出て
        case showSettings
        /// pixwallctl → 常駐側: 動いているか（pong で返す）
        case ping
        /// 常駐側 → pixwallctl: 動いている（userInfo の paused）
        case pong

        var name: Notification.Name { .init("dev.mooma.pixwall.\(rawValue)") }
    }

    static func post(_ message: Message, userInfo: [String: Any]? = nil) {
        DistributedNotificationCenter.default().postNotificationName(
            message.name, object: nil, userInfo: userInfo, deliverImmediately: true)
    }

    static func observe(_ message: Message, _ handler: @escaping (Notification) -> Void) {
        DistributedNotificationCenter.default().addObserver(
            forName: message.name, object: nil, queue: .main, using: handler)
    }

    /// ログイン項目に入れる・外す。入れたあと、システム設定で本人が切ったときは requiresApproval になり、入れ直さない。
    /// 返り値は、入れたいのにシステム設定で切られているか
    @discardableResult
    static func updateLoginItem(_ launchAtLogin: Bool) -> Bool {
        let service = SMAppService.mainApp
        do {
            if launchAtLogin, service.status == .notRegistered || service.status == .notFound {
                try service.register()
            } else if !launchAtLogin, service.status == .enabled || service.status == .requiresApproval {
                try service.unregister()
            }
        } catch {
            NSLog("pixwall: ログイン項目を変えられない: \(error)")
        }
        return launchAtLogin && service.status == .requiresApproval
    }
}
