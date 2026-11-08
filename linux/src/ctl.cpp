// pixwallctl（コマンドラインの道具）。config.json を読み書きし、常駐側にはセッションの D-Bus で頼む（bus.h）。
// Mac・Windows 版と同じ決まり。終わりの値は、できた 0、できなかった 1、引数の誤り 2
#include <cstdint>
#include <cstdio>
#include <glib/gstdio.h>
#include <cstdlib>
#include <string>
#include <vector>

#include "bus.h"
#include "cli.h"
#include "config.h"
#include "config_file.h"
#include "gnome_wallpaper.h"
#include "omarchy.h"
#include "image_loader.h"
#include "still.h"

using namespace pixwall;

namespace {

void Out(const std::string& s) {
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
}

void Fail(const std::string& s) {
    const std::string line = "pixwallctl: " + s + "\n";
    std::fwrite(line.data(), 1, line.size(), stderr);
}

bool Call(const char* method, GVariant* args = nullptr) {
    GVariant* reply = CallResident(method, args);
    if (!reply) return false;
    g_variant_unref(reply);
    return true;
}

// 全部当ててから1回だけ書く。どれかがだめなら何も書かない
int Set(const CliCommand& command, const std::string& configPath, const Config& config) {
    Config next = config;
    std::string error;
    for (const auto& [key, value] : command.pairs) {
        const std::string v = key == "image" ? AbsolutePath(value) : value;
        if (!SetConfigValue(next, key, v, error)) {
            Fail(error);
            return 2;
        }
    }
    // 読めない画像は設定に書かない（設定画面と同じ）
    LoadedImage image;
    if (next.image != config.image && !next.image.empty() && !LoadImageFile(next.image, image)) {
        Fail("cannot read the image: " + next.image);
        return 1;
    }
    if (!SaveConfig(configPath, next)) {
        Fail("cannot write " + configPath);
        return 1;
    }
    if (!Call("Reload")) Fail("pixwall is not running; it uses the settings when it starts");
    return 0;
}

// 今、壁紙に出しているもの（時刻・ダークモードで選んだもの）を書き出す
int Export(const CliCommand& command, const Config& saved) {
    const Config config = ResolveNow(saved, LocalMinute(), SystemIsDark());
    if (config.image.empty()) {
        Fail("no image is chosen");
        return 1;
    }
    LoadedImage image;
    if (!LoadImageFile(config.image, image)) {
        Fail("cannot read the image: " + config.image);
        return 1;
    }
    if (static_cast<size_t>(command.frame) >= image.frames.size()) {
        Fail("the image has " + std::to_string(image.frames.size()) + " frames");
        return 2;
    }
    const int rgb = config.customBackground && config.backgroundColor >= 0 ? config.backgroundColor
                                                                            : TopLeftColor(image);
    const std::vector<ScreenSize> sizes = MonitorSizes();
    if (sizes.empty()) {
        Fail("cannot find the screens (is a desktop session running?)");
        return 1;
    }
    const std::string path = AbsolutePath(command.path);
    if (!ExportStill(image, config.image, command.frame, config.placement(), rgb, sizes, path)) {
        Fail("cannot write " + path);
        return 1;
    }
    return 0;
}

int Run(const std::vector<std::string>& args) {
    CliCommand command;
    std::string error;
    if (!ParseCli(args, command, error)) {
        Fail(error + "\n\n" + kCliUsage);
        return 2;
    }
    const std::string configPath = ConfigPath();
    const Config config = LoadConfig(configPath);
    switch (command.action) {
        case CliAction::Help:
            Out(kCliUsage);
            return 0;
        case CliAction::Show:
            Out(FormatConfig(config));
            return 0;
        case CliAction::Get:
            Out(ConfigValue(config, command.pairs[0].first));
            return 0;
        case CliAction::Set:
            return Set(command, configPath, config);
        case CliAction::Status: {
            GVariant* reply = CallResident("IsPaused");
            gboolean paused = FALSE;
            if (reply) {
                g_variant_get(reply, "(b)", &paused);
                g_variant_unref(reply);
            }
            Out(!reply ? "stopped" : paused ? "paused" : "running");
            return 0;
        }
        case CliAction::Export:
            return Export(command, config);
        default:
            break;
    }
    bool ok = false;
    switch (command.action) {
        case CliAction::Pause:
            ok = Call("SetPaused", g_variant_new("(b)", TRUE));
            break;
        case CliAction::Resume:
            ok = Call("SetPaused", g_variant_new("(b)", FALSE));
            break;
        case CliAction::Refresh:
            ok = Call("Refresh");
            break;
        default:
            ok = Call("Quit");
            break;
    }
    if (!ok) {
        Fail("pixwall is not running");
        return 1;
    }
    return 0;
}

}  // namespace

// GNOME の拡張機能のための、使い方に出さない頼みごと。シェルの中で重い計算をしないよう、こちらで読む
//   --resolve <分> <0|1>: 1行目に次に切り替わるまでの分（-1 は切り替えない）、続けて今出す設定の JSON
//   --frames <パス>: 画像を読み、"PXWF"、幅・高さ・フレームの数・自動の余白の色（uint32）、待ち時間（double 秒 × 数）、
//                    フレーム（premultiplied の BGRA）をそのまま書く。読めなければ何も書かず 1
//   --gnome-wallpaper <分> <0|1> <幅> <高さ>: 今出す画像の1枚目を、主の画面の物理ピクセルの大きさで描いて GNOME の
//                    壁紙に入れる（gnome_wallpaper.h）。画像が無い・読めなければ元の壁紙に戻す
//   --gnome-wallpaper-restore: 元の壁紙に戻す
int GnomeWallpaper(int minute, bool dark, int width, int height) {
    const Config config = ResolveNow(LoadConfig(ConfigPath()), minute, dark);
    LoadedImage image;
    GStatBuf st;
    if (config.image.empty() || width <= 0 || height <= 0 || g_stat(config.image.c_str(), &st) != 0 ||
        !LoadImageFile(config.image, image) || image.frames.empty()) {
        RestoreGnomeWallpaper(GnomeWallpaperDir());
        return 1;
    }
    const int rgb = config.customBackground && config.backgroundColor >= 0 ? config.backgroundColor
                                                                            : TopLeftColor(image);
    const Placement p = config.placement();
    char key[512];
    std::snprintf(key, sizeof(key), "%lld:%lld:%d:%g:%g:%g:%g:%06X:%dx%d:", static_cast<long long>(st.st_size),
                  static_cast<long long>(st.st_mtime), static_cast<int>(p.fit), p.position, p.freeX, p.freeY,
                  p.freeSize, rgb & 0xFFFFFF, width, height);
    const bool ok = ApplyGnomeWallpaper(GnomeWallpaperDir(), key + config.image, [&](const std::string& png) {
        const auto still = RenderStill(image.frames[0].data(), image.width, image.height, width, height, p,
                                       (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        return WritePng(png, still, width, height);
    });
    return ok ? 0 : 1;
}

int Internal(const std::vector<std::string>& args) {
    if (args[0] == "--gnome-wallpaper" && args.size() == 5) {
        return GnomeWallpaper(std::atoi(args[1].c_str()), args[2] == "1", std::atoi(args[3].c_str()),
                              std::atoi(args[4].c_str()));
    }
    if (args[0] == "--gnome-wallpaper-restore" && args.size() == 1) {
        return RestoreGnomeWallpaper(GnomeWallpaperDir()) ? 0 : 1;
    }
    if (args[0] == "--resolve" && args.size() == 3) {
        const Config saved = LoadConfig(ConfigPath());
        const int minute = std::atoi(args[1].c_str());
        Out(std::to_string(saved.MinutesToNextSwitch(minute)));
        Out(FormatConfig(ResolveNow(saved, minute, args[2] == "1")));
        return 0;
    }
    if (args[0] == "--frames" && args.size() == 2) {
        LoadedImage image;
        if (!LoadImageFile(args[1], image)) return 1;
        const uint32_t head[] = {static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height),
                                 static_cast<uint32_t>(image.frames.size()),
                                 static_cast<uint32_t>(TopLeftColor(image))};
        std::fwrite("PXWF", 1, 4, stdout);
        std::fwrite(head, sizeof(head), 1, stdout);
        std::fwrite(image.delays.data(), sizeof(double), image.delays.size(), stdout);
        for (const auto& f : image.frames) std::fwrite(f.data(), 1, f.size(), stdout);
        return std::fflush(stdout) == 0 ? 0 : 1;
    }
    Fail("unknown internal command");
    return 2;
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0].rfind("--", 0) == 0 && args[0] != "--help") return Internal(args);
    return Run(args);
}
