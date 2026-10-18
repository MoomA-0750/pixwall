import AppKit

/// 画面に出ている、ほかのふつうのアプリ（Dock に出るもの）の窓の位置。AppKit と同じ、主画面の左下が原点の座標で返す。
/// 位置と大きさだけなら、画面収録の許可なしで読める。
/// regularApps は Dock に出るアプリの pid。それ以外のアプリ（画面いっぱいの透明な重ね絵、Stage Manager の飾りなど）の窓は数えない
func otherWindowFrames(regularApps: Set<pid_t>) -> [CGRect] {
    let options: CGWindowListOption = [.optionOnScreenOnly, .excludeDesktopElements]
    guard let list = CGWindowListCopyWindowInfo(options, kCGNullWindowID) as? [[String: Any]] else { return [] }
    let me = ProcessInfo.processInfo.processIdentifier
    // CGWindow の座標は主画面の左上が原点で、下へ増える
    let primaryHeight = NSScreen.screens.first?.frame.height ?? 0
    return list.compactMap { info in
        guard (info[kCGWindowLayer as String] as? Int) == 0,
              let pid = info[kCGWindowOwnerPID as String] as? pid_t, pid != me, regularApps.contains(pid),
              (info[kCGWindowAlpha as String] as? Double ?? 1) > 0,
              let bounds = info[kCGWindowBounds as String],
              let r = CGRect(dictionaryRepresentation: bounds as! CFDictionary)
        else { return nil }
        return CGRect(x: r.minX, y: primaryHeight - r.maxY, width: r.width, height: r.height)
    }
}
