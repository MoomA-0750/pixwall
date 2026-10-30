import Foundation

/// 出す画像を何で切り替えるか。設定の JSON にもこの名前で書く
public enum SwitchBy: String, Codable, CaseIterable {
    /// 切り替えない（Config の image と並べ方を使う。既定）
    case none
    /// 決めた時刻で切り替える（Config.timeSlots）
    case time
    /// 端末のダークモード・ライトモードで切り替える（Config.lightSlot・darkSlot）
    case appearance
}

/// 1つの画像と、その並べ方・位置・大きさ。設定の JSON では Config の上の階層と同じ名前の項目に書く
public struct Slot: Codable, Equatable {
    /// 動かす画像のパス。選んでいなければ nil
    public var image: String?
    public var placement = Placement()

    public init(image: String? = nil, placement: Placement = Placement()) {
        self.image = image
        self.placement = placement
    }

    private enum CodingKeys: String, CodingKey {
        case image, fit, position, freeX, freeY, freeSize
    }

    /// 足りない項目・読めない項目は既定の値にし、範囲の外の値は範囲に収める
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let d = Placement()
        image = (try? c.decodeIfPresent(String.self, forKey: .image)) ?? nil
        func unit(_ key: CodingKeys, _ fallback: Double) -> Double {
            min(max((try? c.decodeIfPresent(Double.self, forKey: key)) ?? fallback, 0), 1)
        }
        let sizes = Placement.freeSizeRange
        placement = Placement(
            fit: (try? c.decodeIfPresent(Fit.self, forKey: .fit)) ?? d.fit,
            position: unit(.position, d.position),
            freeX: unit(.freeX, d.freeX),
            freeY: unit(.freeY, d.freeY),
            freeSize: min(max((try? c.decodeIfPresent(Double.self, forKey: .freeSize)) ?? d.freeSize,
                              sizes.lowerBound), sizes.upperBound))
    }

    public func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encodeIfPresent(image, forKey: .image)
        try c.encode(placement.fit, forKey: .fit)
        try c.encode(placement.position, forKey: .position)
        try c.encode(placement.freeX, forKey: .freeX)
        try c.encode(placement.freeY, forKey: .freeY)
        try c.encode(placement.freeSize, forKey: .freeSize)
    }
}

/// 決めた時刻から出す画像。JSON では Slot の項目に start（"HH:MM"）を足して書く
public struct TimeSlot: Codable, Equatable {
    /// 始まる時刻。0 時からの分（0〜1439）
    public var start: Int
    public var slot: Slot

    public init(start: Int, slot: Slot) {
        self.start = min(max(start, 0), minutesPerDay - 1)
        self.slot = slot
    }

    private enum CodingKeys: String, CodingKey {
        case start
    }

    /// 時刻が読めないものは 0:00 にする
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        start = ((try? c.decodeIfPresent(String.self, forKey: .start)) ?? nil).flatMap(minutes(clock:)) ?? 0
        slot = try Slot(from: decoder)
    }

    public func encode(to encoder: Encoder) throws {
        try slot.encode(to: encoder)
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(clock(minutes: start), forKey: .start)
    }
}

public let minutesPerDay = 24 * 60

/// "H:MM" か "HH:MM"（0:00〜23:59）を 0 時からの分にする。形が違えば nil
public func minutes(clock: String) -> Int? {
    let parts = clock.split(separator: ":", omittingEmptySubsequences: false)
    guard parts.count == 2, (1...2).contains(parts[0].count), parts[1].count == 2,
          parts.allSatisfy({ $0.allSatisfy(\.isASCII) && $0.allSatisfy(\.isNumber) }),
          let h = Int(parts[0]), let m = Int(parts[1]), h < 24, m < 60 else { return nil }
    return h * 60 + m
}

/// 0 時からの分を "HH:MM" にする
public func clock(minutes: Int) -> String {
    let m = ((minutes % minutesPerDay) + minutesPerDay) % minutesPerDay
    return String(format: "%02d:%02d", m / 60, m % 60)
}

extension Config {
    /// 切り替えないときに出す画像と並べ方（上の階層の image・fit・position・freeX・freeY・freeSize）
    public var mainSlot: Slot {
        get { Slot(image: image, placement: placement) }
        set {
            image = newValue.image
            placement = newValue.placement
        }
    }

    /// 時刻で切り替えるときに、今（0 時からの分）出す timeSlots の番号。
    /// 始まる時刻が今以前のうち一番遅いもの。無ければ前の日から続く、一番遅く始まるもの。同じ時刻なら後に書いたもの
    public func activeTimeIndex(minute: Int) -> Int? {
        guard !timeSlots.isEmpty else { return nil }
        let order = timeSlots.indices.sorted { (timeSlots[$0].start, $0) < (timeSlots[$1].start, $1) }
        return order.last { timeSlots[$0].start <= minute } ?? order.last
    }

    /// 今（0 時からの分・ダークモードか）出す画像と並べ方。切り替える先が無ければ mainSlot
    public func activeSlot(minute: Int, dark: Bool) -> Slot {
        switch switchBy {
        case .none: return mainSlot
        case .time: return activeTimeIndex(minute: minute).map { timeSlots[$0].slot } ?? mainSlot
        case .appearance: return (dark ? darkSlot : lightSlot) ?? mainSlot
        }
    }

    /// 今出すものを mainSlot に入れた設定。常駐側はこれで描く
    public func resolved(minute: Int, dark: Bool) -> Config {
        var c = self
        c.mainSlot = activeSlot(minute: minute, dark: dark)
        return c
    }

    /// 時刻で切り替えるとき、今（0 時からの分）から次に切り替わる時刻までの分（1〜1440）。切り替えないなら nil
    public func minutesToNextSwitch(after minute: Int) -> Int? {
        guard switchBy == .time, Set(timeSlots.map(\.start)).count > 1 else { return nil }
        return timeSlots.map { (($0.start - minute - 1 + minutesPerDay) % minutesPerDay) + 1 }.min()
    }

    /// 切り替え方を変える。初めて切り替えるときは、今の画像と並べ方から始める
    /// （時刻は 6:00 と 18:00 の2つ、ダーク・ライトは両方）
    public mutating func switchTo(_ mode: SwitchBy) {
        switchBy = mode
        switch mode {
        case .none: break
        case .time:
            if timeSlots.isEmpty {
                timeSlots = [TimeSlot(start: 6 * 60, slot: mainSlot), TimeSlot(start: 18 * 60, slot: mainSlot)]
            }
        case .appearance:
            if lightSlot == nil { lightSlot = mainSlot }
            if darkSlot == nil { darkSlot = mainSlot }
        }
    }

    /// 設定に出てくる画像のパス（重なりなし、出てくる順）
    public var allImages: [String] {
        var seen = Set<String>()
        return ([image] + timeSlots.map(\.slot.image) + [lightSlot?.image, darkSlot?.image])
            .compactMap { $0 }.filter { seen.insert($0).inserted }
    }
}

/// 端末の今の時刻を 0 時からの分にする
public func minuteOfDay(_ date: Date = Date(), calendar: Calendar = .current) -> Int {
    let c = calendar.dateComponents([.hour, .minute], from: date)
    return (c.hour ?? 0) * 60 + (c.minute ?? 0)
}

/// 設定の中のどの画像と並べ方か（設定画面で編集するもの）
public enum SlotRef: Hashable {
    case main
    /// timeSlots の番号
    case time(Int)
    case light
    case dark
}

extension Config {
    /// 無いもの（番号が範囲の外・まだ選んでいないダーク・ライト）は mainSlot
    public func slot(_ ref: SlotRef) -> Slot {
        switch ref {
        case .main: return mainSlot
        case .time(let i): return timeSlots.indices.contains(i) ? timeSlots[i].slot : mainSlot
        case .light: return lightSlot ?? mainSlot
        case .dark: return darkSlot ?? mainSlot
        }
    }

    /// 番号が範囲の外なら何もしない
    public mutating func setSlot(_ ref: SlotRef, _ slot: Slot) {
        switch ref {
        case .main: mainSlot = slot
        case .time(let i): if timeSlots.indices.contains(i) { timeSlots[i].slot = slot }
        case .light: lightSlot = slot
        case .dark: darkSlot = slot
        }
    }
}
