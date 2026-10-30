// pixwallctl.exe（コマンドラインの道具）。config.json を読み書きし、常駐側には設定画面と同じく
// 隠れた窓（pixwall-message）への WM_COPYDATA で頼む（request.h）。Mac 版の Ctl.swift と同じ決まり。
// 終わりの値は、できた 0、できなかった 1、引数の誤り 2
#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "cli.h"
#include "config.h"
#include "config_file.h"
#include "image_loader.h"
#include "request.h"
#include "still.h"

using Microsoft::WRL::ComPtr;
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

HWND Resident() { return FindWindowW(L"pixwall-message", nullptr); }

LRESULT Send(HWND hwnd, Request request, const void* data = nullptr, DWORD size = 0) {
    COPYDATASTRUCT cds{request, size, const_cast<void*>(data)};
    return SendMessageW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
}

std::wstring FullPath(const std::string& path) {
    const std::wstring w = Wide(path);
    wchar_t full[MAX_PATH];
    return GetFullPathNameW(w.c_str(), MAX_PATH, full, nullptr) ? full : w;
}

// 読めるか確かめる・書き出すために、画像をこのプロセスで読む
bool Load(const std::string& path, LoadedImage& image) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        return false;
    }
    return LoadImageFile(wic.Get(), Wide(path), image) && !image.frames.empty();
}

// 全部当ててから1回だけ書く。どれかがだめなら何も書かない
int Set(const CliCommand& command, const std::wstring& configPath, const Config& config) {
    Config next = config;
    std::string error;
    for (const auto& [key, value] : command.pairs) {
        const std::string v = key == "image" ? Utf8(FullPath(value)) : value;
        if (!SetConfigValue(next, key, v, error)) {
            Fail(error);
            return 2;
        }
    }
    // 読めない画像は設定に書かない（設定画面と同じ）
    LoadedImage image;
    if (next.image != config.image && !Load(next.image, image)) {
        Fail("cannot read the image: " + next.image);
        return 1;
    }
    if (!SaveConfig(configPath, next)) {
        Fail("cannot write " + Utf8(configPath));
        return 1;
    }
    if (HWND hwnd = Resident()) {
        Send(hwnd, kReloadConfig);
    } else {
        Fail("pixwall is not running; it uses the settings when it starts");
    }
    return 0;
}

uint8_t Byte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f); }

// 今、壁紙に出しているもの（時刻・ダークモードで選んだもの）を書き出す
int Export(const CliCommand& command, const Config& saved) {
    const Config config = saved.Resolved(LocalMinute(), SystemIsDark());
    if (config.image.empty()) {
        Fail("no image is chosen");
        return 1;
    }
    ComPtr<IWICImagingFactory> wic;
    LoadedImage image;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        !LoadImageFile(wic.Get(), Wide(config.image), image) || image.frames.empty()) {
        Fail("cannot read the image: " + config.image);
        return 1;
    }
    if (static_cast<size_t>(command.frame) >= image.frames.size()) {
        Fail("the image has " + std::to_string(image.frames.size()) + " frames");
        return 2;
    }
    // 余白の色。自分で選んだ色か、1枚目の左上の1ドット（常駐側の TopLeftColor と同じく premultiplied を戻す）
    uint8_t r = 0, g = 0, b = 0;
    if (config.customBackground && config.backgroundColor >= 0) {
        r = (config.backgroundColor >> 16) & 0xFF;
        g = (config.backgroundColor >> 8) & 0xFF;
        b = config.backgroundColor & 0xFF;
    } else if (const uint8_t* p = image.frames[0].data(); p[3] > 0) {
        const float a = p[3] / 255.0f;
        r = Byte(p[2] / 255.0f / a);
        g = Byte(p[1] / 255.0f / a);
        b = Byte(p[0] / 255.0f / a);
    }
    const std::wstring path = FullPath(command.path);
    if (!ExportStill(wic.Get(), image, Wide(config.image), command.frame, config.placement(), r, g, b, path)) {
        Fail("cannot write " + Utf8(path));
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
    const std::wstring configPath = ConfigPath();
    const Config config = LoadConfig(configPath);
    HWND hwnd = Resident();
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
        case CliAction::Status:
            Out(!hwnd ? "stopped" : Send(hwnd, kIsPaused) == 2 ? "paused" : "running");
            return 0;
        case CliAction::Export:
            return Export(command, config);
        default:
            break;
    }
    if (!hwnd) {
        Fail("pixwall is not running");
        return 1;
    }
    const uint8_t paused = command.action == CliAction::Pause ? 1 : 0;
    switch (command.action) {
        case CliAction::Pause:
        case CliAction::Resume:
            Send(hwnd, kSetPaused, &paused, 1);
            break;
        case CliAction::Refresh:
            Send(hwnd, kRefresh);
            break;
        default:
            Send(hwnd, kQuit);
            break;
    }
    return 0;
}

}  // namespace

int wmain() {
    // 画面の倍率に左右されない、物理ピクセルの画面の大きさで書き出す
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // パスなどの UTF-8 を、コンソールにそのまま出す
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(Utf8(argv[i]));
    LocalFree(argv);
    const int code = Run(args);
    CoUninitialize();
    return code;
}
