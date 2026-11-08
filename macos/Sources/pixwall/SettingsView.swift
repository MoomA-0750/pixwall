import AppKit
import PixwallCore
import ServiceManagement
import SwiftUI
import UniformTypeIdentifiers

/// 設定画面の中身と、変えたときにすること
@MainActor @Observable
final class SettingsModel {
    var config: Config {
        didSet { if config != oldValue { onChange(config) } }
    }
    var paused = false {
        didSet { if paused != oldValue { onPause(paused) } }
    }
    /// どの画像と並べ方を編集しているか（切り替えないときは main）
    var editing: SlotRef = .main {
        didSet { loadPreview() }
    }
    /// 読めなかった画像のパス。画面に出す
    var loadError: String?
    /// ログイン項目に入れたが、システム設定で切られている
    var loginItemNeedsApproval = false
    /// 編集している画像（プレビューと書き出しに使う）。読めないときは nil
    var image: AnimatedImage? {
        didSet { frame = 0 }
    }
    /// image を読んだパス
    @ObservationIgnored private var previewPath: String?
    /// 書き出す絵（何枚目か、0 から）
    var frame = 0
    /// 書き出した結果。画面に出す
    var exportResult: String?

    @ObservationIgnored var onChange: (Config) -> Void = { _ in }
    @ObservationIgnored var onPause: (Bool) -> Void = { _ in }
    @ObservationIgnored var onRefresh: () -> Void = {}
    @ObservationIgnored var onQuit: () -> Void = {}

    init(config: Config) {
        self.config = config
        editing = Self.initialEditing(config)
        loadPreview()
    }

    /// 開いたときは、今出しているものを編集する
    private static func initialEditing(_ config: Config) -> SlotRef {
        switch config.switchBy {
        case .none: return .main
        case .time: return config.activeTimeIndex(minute: minuteOfDay()).map(SlotRef.time) ?? .main
        case .appearance:
            return NSApp.effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? .dark : .light
        }
    }

    /// 編集している画像と並べ方
    var slot: Slot {
        get { config.slot(editing) }
        set { config.setSlot(editing, newValue) }
    }

    /// 切り替え方を変え、今出しているものの編集に移る
    func switchTo(_ mode: SwitchBy) {
        var c = config
        c.switchTo(mode)
        config = c
        editing = Self.initialEditing(config)
    }

    /// 時刻を1つ足す。今編集しているものを写し、一番遅い時刻の1時間後から始める
    func addTime() {
        let start = ((config.timeSlots.map(\.start).max() ?? 0) + 60) % minutesPerDay
        config.timeSlots.append(TimeSlot(start: start, slot: slot))
        editing = .time(config.timeSlots.count - 1)
    }

    func removeTime(_ index: Int) {
        guard config.timeSlots.indices.contains(index), config.timeSlots.count > 1 else { return }
        config.timeSlots.remove(at: index)
        editing = .time(min(index, config.timeSlots.count - 1))
    }

    /// 編集している画像をプレビューに読む（同じパスなら読み直さない）
    func loadPreview() {
        let path = slot.image
        if path == previewPath { return }
        previewPath = path
        image = path.flatMap { AnimatedImage(url: URL(fileURLWithPath: $0)) }
        loadError = image == nil ? path : nil
        exportResult = nil
    }

    /// 読めない画像は設定に書かない。壁紙はそのまま
    func chooseImage() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.gif, .png, .webP]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if let image = slot.image ?? config.allImages.first {
            panel.directoryURL = URL(fileURLWithPath: image).deletingLastPathComponent()
        }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        guard let loaded = AnimatedImage(url: url) else {
            loadError = url.path
            return
        }
        previewPath = url.path
        image = loaded
        loadError = nil
        exportResult = nil
        slot.image = url.path
    }

    /// 画面ごとに、その画面の大きさで書き出す。主の画面のファイル名と場所を、保存の画面で選ぶ
    func exportStill() {
        guard let image, let path = slot.image, let screen = NSScreen.screens.first else { return }
        let size = StillImage.pixelSize(of: screen)
        let stem = URL(fileURLWithPath: path).deletingPathExtension().lastPathComponent
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.png]
        panel.nameFieldStringValue = "\(stem)-\(size.width)x\(size.height).png"
        panel.directoryURL = FileManager.default.urls(for: .picturesDirectory, in: .userDomainMask).first
        guard panel.runModal() == .OK, let url = panel.url else { return }
        exportResult = StillImage.export(image, imagePath: path, frame: frame, placement: slot.placement,
                                         background: config.fillColor(for: image), to: url)
            ? String(localized: "書き出しました: \(url.path)")
            : String(localized: "書き出せませんでした: \(url.path)")
    }
}

struct SettingsView: View {
    @Bindable var model: SettingsModel

    var body: some View {
        Form {
            Section {
                // 自動で合わなかったときのために、一番上に置く
                Button("表示を更新") { model.onRefresh() }
            }
            switchSection
            Section(imageHeader) {
                LabeledContent("すべての画面") {
                    HStack {
                        Text(imageName)
                            .foregroundStyle(model.slot.image == nil ? .secondary : .primary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                        Button("選ぶ…") { model.chooseImage() }
                    }
                }
                if let error = model.loadError {
                    Text("読めませんでした: \(error)")
                        .foregroundStyle(.red)
                        .font(.callout)
                }
                if let image = model.image {
                    Image(decorative: image.frames[min(model.frame, image.frames.count - 1)], scale: 1)
                        .interpolation(.none)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        // 透明なところには、壁紙と同じく余白の色が見える
                        .background(Color(cgColor: model.config.fillColor(for: image)))
                        .frame(maxWidth: .infinity, maxHeight: 160)
                    // pixwall が動いていないときにも、同じ見た目を静止画で壁紙にできるように
                    LabeledContent("静止画") {
                        Button("静止画を書き出す…") { model.exportStill() }
                    }
                    if image.frames.count > 1 {
                        LabeledContent("書き出す絵") {
                            HStack {
                                Slider(value: frameValue, in: 0...Double(image.frames.count - 1), step: 1)
                                    .frame(width: 160)
                                Text("\(model.frame + 1) / \(image.frames.count) 枚目")
                                    .monospacedDigit()
                                    .frame(width: 100, alignment: .trailing)
                            }
                        }
                    }
                    if let result = model.exportResult {
                        Text(result)
                            .foregroundStyle(.secondary)
                            .font(.callout)
                            .textSelection(.enabled)
                    }
                }
            }
            Section("表示") {
                Picker("並べ方", selection: $model.slot.placement.fit) {
                    Text("整数倍で最大").tag(Fit.integer)
                    Text("画面に合わせる").tag(Fit.fit)
                    Text("画面を埋める").tag(Fit.fill)
                    Text("自由に置く").tag(Fit.free)
                }
                // 画面に合わせる・埋めるときだけ。余白（埋めるときははみ出し）が上下にできる画面では上下、左右にできる画面では左右に動く
                if model.slot.placement.fit == .fit || model.slot.placement.fit == .fill {
                    LabeledContent {
                        percentSlider(value: percent(\.position), in: 0...100, label: percentLabel(model.slot.placement.position))
                    } label: {
                        Text("位置")
                        Text("0 で上・左、100 で下・右")
                    }
                }
                // 自由に置くときは、大きさと横・縦の位置を別々に決める
                if model.slot.placement.fit == .free {
                    LabeledContent {
                        percentSlider(value: freeSizePercent, in: 10...300, label: String(format: "%.0f%%", freeSizePercent.wrappedValue))
                    } label: {
                        Text("大きさ")
                        Text("100% で画面に合わせた大きさ")
                    }
                    LabeledContent {
                        percentSlider(value: percent(\.freeX), in: 0...100, label: percentLabel(model.slot.placement.freeX))
                    } label: {
                        Text("横の位置")
                        Text("0 で左の端、100 で右の端")
                    }
                    LabeledContent {
                        percentSlider(value: percent(\.freeY), in: 0...100, label: percentLabel(model.slot.placement.freeY))
                    } label: {
                        Text("縦の位置")
                        Text("0 で上の端、100 で下の端")
                    }
                }
                // 余白と、画像の透明なところの色
                Picker(selection: backgroundMode) {
                    Text("自動").tag(BackgroundMode.auto)
                    Text("自分で選ぶ").tag(BackgroundMode.custom)
                } label: {
                    Text("余白の色")
                    Text("自動では画像の左上の色")
                }
                if model.config.background == .custom {
                    ColorPicker("色", selection: customColor, supportsOpacity: false)
                }
                LabeledContent("速さ") {
                    HStack {
                        Slider(value: speedExponent, in: -2...2, step: 1)
                            .frame(width: 160)
                        Text(speedLabel)
                            .monospacedDigit()
                            .frame(width: 48, alignment: .trailing)
                    }
                }
            }
            Section("止める") {
                Toggle("低電力モードのときは止める", isOn: $model.config.pauseOnLowPower)
                Toggle("一時停止", isOn: $model.paused)
            }
            Section {
                Toggle("ログイン時に起動", isOn: $model.config.launchAtLogin)
                if model.loginItemNeedsApproval {
                    HStack {
                        Text("システム設定の「ログイン項目」でオフになっています")
                            .foregroundStyle(.secondary)
                            .font(.callout)
                        Button("開く…") { SMAppService.openSystemSettingsLoginItems() }
                    }
                }
            }
            Section {
                Toggle("終了しても壁紙を残す", isOn: $model.config.keepWallpaperOnQuit)
                Button("pixwall を終了", role: .destructive) { model.onQuit() }
            } footer: {
                // 三項演算子では String になり訳されないので、LocalizedStringKey にする
                Text(model.config.keepWallpaperOnQuit ? LocalizedStringKey("終了すると、今の画像の1枚目を macOS の壁紙に残します")
                                                      : LocalizedStringKey("終了すると、元の macOS の壁紙に戻します"))
            }
        }
        .formStyle(.grouped)
        .frame(width: 440)
        .fixedSize(horizontal: false, vertical: true)
    }

    /// 出す画像を何で切り替えるか。時刻のときは時刻の一覧、ダーク・ライトのときはどちらを編集するかを出す
    @ViewBuilder private var switchSection: some View {
        Section {
            Picker(selection: Binding(get: { model.config.switchBy }, set: { model.switchTo($0) })) {
                Text("切り替えない").tag(SwitchBy.none)
                Text("時刻で").tag(SwitchBy.time)
                Text("ダーク・ライトで").tag(SwitchBy.appearance)
            } label: {
                Text("画像の切り替え")
                Text("時刻や外観ごとに、画像と並べ方を選べます")
            }
            switch model.config.switchBy {
            case .none:
                EmptyView()
            case .time:
                let active = model.config.activeTimeIndex(minute: minuteOfDay())
                ForEach(model.config.timeSlots.indices, id: \.self) { i in
                    timeRow(i, active: i == active)
                }
                Button("時刻を足す") { model.addTime() }
            case .appearance:
                Picker("編集する外観", selection: $model.editing) {
                    Text("ライト").tag(SlotRef.light)
                    Text("ダーク").tag(SlotRef.dark)
                }
                .pickerStyle(.segmented)
            }
        }
    }

    /// 時刻の一覧の1行。選ぶと、下の「画像」「表示」でその時刻の画像と並べ方を編集する
    private func timeRow(_ i: Int, active: Bool) -> some View {
        let t = model.config.timeSlots[i]
        let selected = model.editing == .time(i)
        return HStack {
            DatePicker("から", selection: startDate(i), displayedComponents: .hourAndMinute)
                .labelsHidden()
            Button {
                model.editing = .time(i)
            } label: {
                HStack {
                    Image(systemName: selected ? "checkmark.circle.fill" : "circle")
                        .foregroundStyle(selected ? Color.accentColor : .secondary)
                    Text(t.slot.image.map { URL(fileURLWithPath: $0).lastPathComponent } ?? String(localized: "選んでいない"))
                        .foregroundStyle(t.slot.image == nil ? .secondary : .primary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityAddTraits(selected ? .isSelected : [])
            if active {
                Text("今")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            if model.config.timeSlots.count > 1 {
                Button("削除", systemImage: "minus.circle") { model.removeTime(i) }
                    .labelStyle(.iconOnly)
                    .buttonStyle(.borderless)
            }
        }
    }

    /// 始まる時刻を DatePicker で編集する（日付は使わない）
    private func startDate(_ i: Int) -> Binding<Date> {
        Binding(
            get: {
                let m = model.config.timeSlots.indices.contains(i) ? model.config.timeSlots[i].start : 0
                return Calendar.current.date(bySettingHour: m / 60, minute: m % 60, second: 0, of: Date()) ?? Date()
            },
            set: {
                guard model.config.timeSlots.indices.contains(i) else { return }
                model.config.timeSlots[i].start = minuteOfDay($0)
                model.editing = .time(i)
            }
        )
    }

    private var imageHeader: String {
        switch model.editing {
        case .main: return String(localized: "画像")
        case .time(let i):
            let start = model.config.timeSlots.indices.contains(i) ? model.config.timeSlots[i].start : 0
            return String(localized: "画像（\(clock(minutes: start)) から）")
        case .light: return String(localized: "画像（ライト）")
        case .dark: return String(localized: "画像（ダーク）")
        }
    }

    private var imageName: String {
        model.slot.image.map { URL(fileURLWithPath: $0).lastPathComponent } ?? String(localized: "選んでいない")
    }

    private var frameValue: Binding<Double> {
        Binding(get: { Double(model.frame) }, set: { model.frame = Int($0.rounded()) })
    }

    /// 初めて自分で選ぶときは、今の自動の色（画像の左上の色）から始める
    private var backgroundMode: Binding<BackgroundMode> {
        Binding(
            get: { model.config.background },
            set: { mode in
                var c = model.config
                c.background = mode
                if mode == .custom, c.backgroundColor == nil {
                    c.backgroundColor = model.image.flatMap { hex(cgColor: $0.cornerColor) } ?? "#000000"
                }
                model.config = c
            }
        )
    }

    private var customColor: Binding<CGColor> {
        Binding(
            get: {
                let c = model.config.customBackground ?? RGB(r: 0, g: 0, b: 0)
                return CGColor(srgbRed: CGFloat(c.r) / 255, green: CGFloat(c.g) / 255, blue: CGFloat(c.b) / 255,
                               alpha: 1)
            },
            set: { if let h = hex(cgColor: $0) { model.config.backgroundColor = h } }
        )
    }

    /// sRGB の "#RRGGBB" にする
    private func hex(cgColor: CGColor) -> String? {
        guard let c = NSColor(cgColor: cgColor)?.usingColorSpace(.sRGB) else { return nil }
        return PixwallCore.hex(rgb: RGB(r: Int((c.redComponent * 255).rounded()), g: Int((c.greenComponent * 255).rounded()),
                                        b: Int((c.blueComponent * 255).rounded())))
    }

    /// 速さは 0.25・0.5・1・2・4 倍の5段。つまみは 2 の何乗かで動かす
    private var speedExponent: Binding<Double> {
        Binding(
            get: { log2(model.config.speed).rounded() },
            set: { model.config.speed = pow(2, $0) }
        )
    }

    /// 位置は 1 刻み（Slider の step にすると目盛りが 101 本並ぶので、値のほうを丸める）。
    /// つかんでいる間も壁紙がついてくる（常駐側は窓を置き直すだけで、OS の壁紙は止まってから描く）。
    /// 設定には 0〜1 で、端数の出ないよう 100 分の 1 に丸めて書く。大きさ（0.1〜3）も同じく % で動かす
    private func percent(_ key: WritableKeyPath<Placement, Double>) -> Binding<Double> {
        Binding(get: { (model.slot.placement[keyPath: key] * 100).rounded() },
                set: { model.slot.placement[keyPath: key] = $0.rounded() / 100 })
    }

    private func percentLabel(_ p: Double) -> String {
        abs(p - 0.5) < 0.001 ? String(localized: "中央") : String(format: "%.0f", p * 100)
    }

    private var freeSizePercent: Binding<Double> { percent(\.freeSize) }

    private func percentSlider(value: Binding<Double>, in range: ClosedRange<Double>, label: String) -> some View {
        HStack {
            Slider(value: value, in: range)
                .frame(width: 160)
            Text(label)
                .monospacedDigit()
                .frame(width: 48, alignment: .trailing)
        }
    }

    private var speedLabel: String {
        let s = model.config.speed
        let number = s < 1 ? String(format: "%.2g", s) : String(format: "%.0f", s)
        return String(localized: "\(number)倍")
    }
}
