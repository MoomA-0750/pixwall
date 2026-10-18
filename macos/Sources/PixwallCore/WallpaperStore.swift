import Foundation

/// macOS の壁紙の設定ファイル（WallpaperAgent の Index.plist）の控えと戻し。
/// 項目は、操作スペースごと（"Spaces" の下）と、それ以外の上の段の項目（"SystemDefault"・"Displays" など）ごとに扱う。
/// marker（自分の PNG のフォルダの名前）を含む項目を「自分の PNG を指している」とみなす
public enum WallpaperStore {
    /// 控えを作り直す。自分の PNG を指していない項目は今の値を写し、指している項目は前に控えた値のまま。
    /// 今の設定ファイルがどこも自分の PNG を指していなければ、前の控えは捨てて今の値をそのまま控える
    public static func backup(current: [String: Any], previous: [String: Any]?, marker: Data) -> [String: Any] {
        var backup = mentions(current, marker) ? previous ?? [:] : [:]
        for (key, value) in current where key != "Spaces" && !mentions(value, marker) {
            backup[key] = value
        }
        var spaces = backup["Spaces"] as? [String: Any] ?? [:]
        for (id, value) in current["Spaces"] as? [String: Any] ?? [:] where !mentions(value, marker) {
            spaces[id] = value
        }
        backup["Spaces"] = spaces
        return backup
    }

    /// 戻した設定ファイル。自分の PNG を指している項目だけを控えの値にし、ほかはそのまま（その間に本人が替えた分）。
    /// 控えの無い項目は、中の壁紙（"Desktop" の下の "Content"）だけを本人の壁紙に差し替える。
    /// 項目は消さない（消すと WallpaperAgent が設定ファイルを全部決まりの壁紙に作り直す）。
    /// 戻す壁紙が見つからない・項目がそろわない・自分の PNG が残るときは nil（書かない）
    public static func restored(current: [String: Any], backup: [String: Any]?, marker: Data) -> [String: Any]? {
        guard mentions(current, marker) else { return nil }
        let backup = backup ?? [:]
        let savedSpaces = backup["Spaces"] as? [String: Any] ?? [:]
        // 本人の壁紙の中身。一番目の操作スペースの控えを先に使い、無ければ控えか今の設定ファイルの中から探す
        guard let content = desktopContent(in: savedSpaces[""] as Any, marker)
                ?? desktopContent(in: backup, marker) ?? desktopContent(in: current, marker) else { return nil }
        func restore(_ value: Any, saved: Any?) -> Any {
            guard mentions(value, marker) else { return value }
            if let saved, !mentions(saved, marker) { return saved }
            return replacing(value, with: content, marker)
        }
        var result = current
        for (key, value) in current where key != "Spaces" {
            result[key] = restore(value, saved: backup[key])
        }
        let currentSpaces = current["Spaces"] as? [String: Any] ?? [:]
        var spaces = currentSpaces
        for (id, value) in currentSpaces {
            spaces[id] = restore(value, saved: savedSpaces[id])
        }
        if current["Spaces"] != nil { result["Spaces"] = spaces }
        guard Set(result.keys) == Set(current.keys), Set(spaces.keys) == Set(currentSpaces.keys),
              !mentions(result, marker) else { return nil }
        return result
    }

    public static func mentions(_ value: Any, _ marker: Data) -> Bool {
        guard let data = try? PropertyListSerialization.data(fromPropertyList: value, format: .binary, options: 0)
        else { return false }
        return data.range(of: marker) != nil
    }

    /// 自分の PNG を指していない、デスクトップの壁紙の中身を探す。"Default" を先に、ほかは名前の順に見る
    private static func desktopContent(in value: Any, _ marker: Data) -> Any? {
        guard let dict = value as? [String: Any] else { return nil }
        if let desktop = dict["Desktop"] as? [String: Any], let content = desktop["Content"],
           !mentions(content, marker) {
            return content
        }
        let keys = dict.keys.sorted { ($0 == "Default" ? 0 : 1, $0) < ($1 == "Default" ? 0 : 1, $1) }
        for key in keys {
            if let found = desktopContent(in: dict[key] as Any, marker) { return found }
        }
        return nil
    }

    /// 自分の PNG を指している "Content" を、content に差し替える
    private static func replacing(_ value: Any, with content: Any, _ marker: Data) -> Any {
        guard var dict = value as? [String: Any] else { return value }
        for (key, child) in dict where mentions(child, marker) {
            dict[key] = key == "Content" ? content : replacing(child, with: content, marker)
        }
        return dict
    }
}
