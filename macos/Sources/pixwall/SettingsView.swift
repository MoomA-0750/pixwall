import AppKit
import PixwallCore
import ServiceManagement
import SwiftUI
import UniformTypeIdentifiers

/// 設定画面の中身と、変えたときにすること
final class SettingsModel: ObservableObject {
    @Published var config: Config {
        didSet { if config != oldValue { onChange(config) } }
    }
    @Published var paused = false {
        didSet { if paused != oldValue { onPause(paused) } }
    }
    /// 読めなかった画像のパス。画面に出す
    @Published var loadError: String?
    /// ログイン項目に入れたが、システム設定で切られている
    @Published var loginItemNeedsApproval = false
    /// 動かしている画像（プレビューと書き出しに使う）。読めないときは nil
    @Published var image: AnimatedImage? {
        didSet { frame = 0 }
    }
    /// 書き出す絵（何枚目か、0 から）
    @Published var frame = 0
    /// 書き出した結果。画面に出す
    @Published var exportResult: String?

    var onChange: (Config) -> Void = { _ in }
    var onPause: (Bool) -> Void = { _ in }
    var onRefresh: () -> Void = {}
    var onQuit: () -> Void = {}

    init(config: Config) {
        self.config = config
    }

    func chooseImage() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.gif, .png, .webP]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if let image = config.image {
            panel.directoryURL = URL(fileURLWithPath: image).deletingLastPathComponent()
        }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        config.image = url.path
    }

    /// 画面ごとに、その画面の大きさで書き出す。主の画面のファイル名と場所を、保存の画面で選ぶ
    func exportStill() {
        guard let image, let path = config.image, let screen = NSScreen.screens.first else { return }
        let size = StillImage.pixelSize(of: screen)
        let stem = URL(fileURLWithPath: path).deletingPathExtension().lastPathComponent
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.png]
        panel.nameFieldStringValue = "\(stem)-\(size.width)x\(size.height).png"
        panel.directoryURL = FileManager.default.urls(for: .picturesDirectory, in: .userDomainMask).first
        guard panel.runModal() == .OK, let url = panel.url else { return }
        exportResult = StillImage.export(image, imagePath: path, frame: frame, fit: config.fit,
                                         position: config.position, to: url)
            ? "書き出しました: \(url.path)"
            : "書き出せませんでした: \(url.path)"
    }
}

struct SettingsView: View {
    @ObservedObject var model: SettingsModel

    var body: some View {
        Form {
            Section {
                // 自動で合わなかったときのために、一番上に置く
                Button("表示を更新") { model.onRefresh() }
            }
            Section("画像") {
                LabeledContent("すべての画面") {
                    HStack {
                        Text(imageName)
                            .foregroundStyle(model.config.image == nil ? .secondary : .primary)
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
                                    .frame(width: 80, alignment: .trailing)
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
                Picker("並べ方", selection: $model.config.fit) {
                    Text("整数倍で最大").tag(Fit.integer)
                    Text("画面に合わせる").tag(Fit.fit)
                    Text("画面を埋める").tag(Fit.fill)
                }
                // 画面に合わせるときだけ。余白が上下にできる画面では上下、左右にできる画面では左右に動く
                if model.config.fit == .fit {
                    LabeledContent {
                        HStack {
                            Slider(value: $model.config.position, in: 0...1, step: 0.05)
                                .frame(width: 160)
                            Text(positionLabel)
                                .monospacedDigit()
                                .frame(width: 48, alignment: .trailing)
                        }
                    } label: {
                        Text("位置")
                        Text("0 で上・左、100 で下・右")
                    }
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
                Button("pixwall を終了", role: .destructive) { model.onQuit() }
            }
        }
        .formStyle(.grouped)
        .frame(width: 440)
        .fixedSize(horizontal: false, vertical: true)
    }

    private var imageName: String {
        model.config.image.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "選んでいない"
    }

    private var frameValue: Binding<Double> {
        Binding(get: { Double(model.frame) }, set: { model.frame = Int($0.rounded()) })
    }

    /// 速さは 0.25・0.5・1・2・4 倍の5段。つまみは 2 の何乗かで動かす
    private var speedExponent: Binding<Double> {
        Binding(
            get: { log2(model.config.speed).rounded() },
            set: { model.config.speed = pow(2, $0) }
        )
    }

    private var positionLabel: String {
        let p = model.config.position
        return abs(p - 0.5) < 0.001 ? "中央" : String(format: "%.0f", p * 100)
    }

    private var speedLabel: String {
        let s = model.config.speed
        return s < 1 ? String(format: "%.2g倍", s) : String(format: "%.0f倍", s)
    }
}
