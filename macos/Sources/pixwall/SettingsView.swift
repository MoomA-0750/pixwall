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

    var onChange: (Config) -> Void = { _ in }
    var onPause: (Bool) -> Void = { _ in }
    var onRefresh: () -> Void = {}

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
                if let preview {
                    Image(nsImage: preview)
                        .interpolation(.none)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(maxWidth: .infinity, maxHeight: 160)
                }
            }
            Section("表示") {
                Picker("並べ方", selection: $model.config.fit) {
                    Text("整数倍で最大").tag(Fit.integer)
                    Text("画面に合わせる").tag(Fit.fit)
                    Text("画面を埋める").tag(Fit.fill)
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
                Button("pixwall を終了", role: .destructive) { NSApp.terminate(nil) }
            }
        }
        .formStyle(.grouped)
        .frame(width: 440)
        .fixedSize(horizontal: false, vertical: true)
    }

    private var imageName: String {
        model.config.image.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "選んでいない"
    }

    private var preview: NSImage? {
        model.config.image.flatMap { NSImage(contentsOfFile: $0) }
    }

    /// 速さは 0.25・0.5・1・2・4 倍の5段。つまみは 2 の何乗かで動かす
    private var speedExponent: Binding<Double> {
        Binding(
            get: { log2(model.config.speed).rounded() },
            set: { model.config.speed = pow(2, $0) }
        )
    }

    private var speedLabel: String {
        let s = model.config.speed
        return s < 1 ? String(format: "%.2g倍", s) : String(format: "%.0f倍", s)
    }
}
