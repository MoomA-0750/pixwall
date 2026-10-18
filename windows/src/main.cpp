// pixwall.exe（常駐側）。動かす画像は %APPDATA%\pixwall\config.json から読む。
// 引数で画像を渡すと（pixwall.exe <画像のパス>）、それを動かして設定にも書く
// 終えるときは taskkill /im pixwall.exe（隠れた窓に WM_CLOSE が届いて片付けてから終わる）
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <wtsapi32.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"
#include "core.h"
#include "desktop.h"
#include "image_loader.h"
#include "wall_window.h"

using Microsoft::WRL::ComPtr;
using namespace pixwall;

namespace {

constexpr UINT_PTR kRebuildTimer = 1;
// 解像度などが変わってから、変化が落ち着くまで待つ時間
constexpr UINT kRebuildDelayMs = 200;
// アイコンの後ろに入れる窓が見つからないとき（Explorer が立ち上がる途中など）に探し直す間隔と回数
constexpr UINT kRetryDelayMs = 1000;
constexpr int kMaxRetries = 30;

// winnt.h の GUID_CONSOLE_DISPLAY_STATE と GUID_POWER_SAVING_STATUS と同じ値。
// SDK の定義を使うと INITGUID やリンクするライブラリに左右されるので、ここで持つ
constexpr GUID kDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
constexpr GUID kPowerSaving = {0xe00958c0, 0xc213, 0x4ace, {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};

struct App {
    ComPtr<IWICImagingFactory> wic;
    Gpu gpu;
    Config config;
    LoadedImage image;
    Color background;
    std::vector<std::unique_ptr<WallWindow>> windows;
    HWND messageWindow = nullptr;
    UINT taskbarCreated = 0;
    int retries = 0;

    PauseReasons pause;
    size_t frame = 0;
    HANDLE timer = nullptr;
    bool armed = false;
    long long nextDue = 0;  // 100ns 単位の UTC
};

App app;

long long Now() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// 余白の色の既定。1枚目の左上の1ドット（premultiplied の BGRA を戻す）
Color TopLeftColor(const LoadedImage& image) {
    const uint8_t* p = image.frames[0].data();
    const float a = p[3] / 255.0f;
    if (a <= 0) return {0, 0, 0, 1};
    return {p[2] / 255.0f / a, p[1] / 255.0f / a, p[0] / 255.0f / a, 1};
}

void Rebuild();

void RenderAll() {
    for (auto& w : app.windows) {
        if (!w->Render(app.frame)) {
            // GPU が入れ替わった・ドライバーが更新されたなど。GPU の道具から作り直す
            app.gpu.Reset();
            Rebuild();
            return;
        }
    }
}

void ArmNext() {
    const long long delay = static_cast<long long>(FrameDelay(app.image.delays[app.frame], app.config.speed) * 10'000'000);
    const long long now = Now();
    app.nextDue += delay;
    // 遅れすぎたら（止めていた・眠っていた）今から数え直す
    if (app.nextDue < now) app.nextDue = now + delay;
    LARGE_INTEGER due;
    due.QuadPart = -(app.nextDue - now);  // 負は今からの相対
    SetWaitableTimer(app.timer, &due, 0, nullptr, nullptr, FALSE);
    app.armed = true;
}

void UpdateRunning() {
    const bool run = !app.windows.empty() && app.image.frames.size() > 1 && !app.pause.ShouldPause();
    if (run && !app.armed) {
        app.nextDue = Now();
        ArmNext();
    } else if (!run && app.armed) {
        CancelWaitableTimer(app.timer);
        app.armed = false;
    }
}

void Advance() {
    app.armed = false;
    app.frame = (app.frame + 1) % app.image.frames.size();
    RenderAll();
    ArmNext();
}

// 画面を数え直して、画面ごとに窓を作り直す。フレームは読み直さない
void Rebuild() {
    app.windows.clear();
    if (!app.gpu.dcomp && !app.gpu.Create()) {
        OutputDebugStringW(L"pixwall: GPU の道具を作れない\n");
        UpdateRunning();
        return;
    }
    const DesktopParent desktop = FindDesktopParent();
    if (!desktop.parent) {
        // Explorer が作り直された直後は、アイコンの窓も WorkerW もまだ無いことがある。少し待って探し直す
        if (app.retries < kMaxRetries) {
            ++app.retries;
            SetTimer(app.messageWindow, kRebuildTimer, kRetryDelayMs, nullptr);
        } else {
            OutputDebugStringW(L"pixwall: アイコンの後ろに入れる窓が見つからない\n");
        }
        UpdateRunning();
        return;
    }
    app.retries = 0;
    std::vector<RECT> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(mon, &mi)) reinterpret_cast<std::vector<RECT>*>(lp)->push_back(mi.rcMonitor);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    for (const RECT& rc : monitors) {
        auto w = std::make_unique<WallWindow>(app.gpu, app.image, rc, desktop, app.background, app.config.fit);
        if (w->ok()) app.windows.push_back(std::move(w));
    }
    UpdateRunning();
    // 起動と作り直しで触ったメモリ（ほとんどはドライバーの初期化）を手放す。
    // 動いている間に触るのは 1MB ほどで、to-fu ではプライベート ワーキング セットが 11.6MB から 1MB 未満になった
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void ScheduleRebuild(HWND hwnd) {
    app.retries = 0;
    SetTimer(hwnd, kRebuildTimer, kRebuildDelayMs, nullptr);
}

void OnPowerSetting(const POWERBROADCAST_SETTING* s) {
    if (s->DataLength < sizeof(DWORD)) return;
    const DWORD value = *reinterpret_cast<const DWORD*>(s->Data);
    if (s->PowerSetting == kDisplayState) {
        app.pause.displayOff = value == 0;  // 0: 消えた 1: ついた 2: 暗くなった
    } else if (s->PowerSetting == kPowerSaving) {
        app.pause.energySaver = value != 0;
    }
    UpdateRunning();
}

LRESULT CALLBACK MessageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == app.taskbarCreated && msg != 0) {
        // Explorer が作り直された。WorkerW も新しくなるので付け直す
        ScheduleRebuild(hwnd);
        return 0;
    }
    switch (msg) {
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            ScheduleRebuild(hwnd);
            return 0;
        case WM_SETTINGCHANGE:
            if (wp == SPI_SETWORKAREA) ScheduleRebuild(hwnd);
            return 0;
        case WM_TIMER:
            if (wp == kRebuildTimer) {
                KillTimer(hwnd, kRebuildTimer);
                Rebuild();
            }
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_POWERSETTINGCHANGE) OnPowerSetting(reinterpret_cast<POWERBROADCAST_SETTING*>(lp));
            return TRUE;
        case WM_WTSSESSION_CHANGE:
            switch (wp) {
                case WTS_SESSION_LOCK:
                case WTS_CONSOLE_DISCONNECT:
                    app.pause.locked = true;
                    break;
                case WTS_SESSION_UNLOCK:
                case WTS_CONSOLE_CONNECT:
                    app.pause.locked = false;
                    break;
            }
            UpdateRunning();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), static_cast<int>(w.size()));
    return w;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    std::string s(WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr),
                  '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), static_cast<int>(s.size()),
                        nullptr, nullptr);
    return s;
}

// %APPDATA%\pixwall\config.json
std::wstring ConfigPath() {
    PWSTR dir = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &dir))) return {};
    std::wstring path = std::wstring(dir) + L"\\pixwall\\config.json";
    CoTaskMemFree(dir);
    return path;
}

Config LoadConfig(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return ParseConfig(text.str());
}

void SaveConfig(const std::wstring& path, const Config& config) {
    CreateDirectoryW(path.substr(0, path.find_last_of(L'\\')).c_str(), nullptr);
    // 書きかけで落ちても壊れないよう、別のファイルに書いてから入れ替える
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << FormatConfig(config);
        if (!out) return;
    }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// ログインしたときに始まるよう、HKCU の Run に今の exe を入れる。設定で切っていたら消す
void UpdateLaunchAtLogin(bool on) {
    const wchar_t key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (!on) {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, key, L"pixwall");
        return;
    }
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
    const std::wstring command = L"\"" + std::wstring(exe) + L"\"";
    RegSetKeyValueW(HKEY_CURRENT_USER, key, L"pixwall", REG_SZ, command.c_str(),
                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
}

// OS の通知を受けるための、見えない最上位の窓。WM_DISPLAYCHANGE などは最上位の窓にしか来ない
HWND CreateMessageWindow(HINSTANCE inst) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MessageProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"pixwall-message";
    RegisterClassExW(&wc);
    // 表示しないので、タスクバーにも Alt+Tab にも出ない
    return CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"pixwall", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                           inst, nullptr);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 2回目の起動。設定画面ができたらそれを起こす。今は何もせず終わる
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\pixwall");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring configPath = ConfigPath();
    app.config = LoadConfig(configPath);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2) {
        wchar_t full[MAX_PATH];
        if (GetFullPathNameW(argv[1], MAX_PATH, full, nullptr)) {
            app.config.image = Utf8(full);
            SaveConfig(configPath, app.config);
        }
    }
    LocalFree(argv);
    if (app.config.image.empty()) {
        // 設定画面ができるまでは、画像を選ぶ場所が無い
        MessageBoxW(nullptr,
                    L"動かす画像がまだ選ばれていません。\n一度だけ「pixwall.exe <画像のパス>」で起動すると、次からはその画像で始まります。",
                    L"pixwall", MB_OK);
        return 2;
    }
    const std::wstring path = Wide(app.config.image);

    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&app.wic));
    if (!app.wic || !LoadImageFile(app.wic.Get(), path, app.image)) {
        MessageBoxW(nullptr, (L"画像を読めない: " + path).c_str(), L"pixwall", MB_OK);
        return 1;
    }
    app.background = TopLeftColor(app.image);
    app.pause.pauseOnEnergySaver = app.config.pauseOnLowPower;
    UpdateLaunchAtLogin(app.config.launchAtLogin);

    app.messageWindow = CreateMessageWindow(inst);
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    RegisterPowerSettingNotification(app.messageWindow, &kDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    RegisterPowerSettingNotification(app.messageWindow, &kPowerSaving, DEVICE_NOTIFY_WINDOW_HANDLE);
    WTSRegisterSessionNotification(app.messageWindow, NOTIFY_FOR_THIS_SESSION);

    app.timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!app.timer) app.timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    Rebuild();

    // 次のフレームまでは、メッセージとタイマーのどちらかが来るまで眠る
    for (;;) {
        const DWORD count = app.armed ? 1 : 0;
        const DWORD r = MsgWaitForMultipleObjectsEx(count, count ? &app.timer : nullptr, INFINITE, QS_ALLINPUT,
                                                    MWMO_INPUTAVAILABLE);
        if (count && r == WAIT_OBJECT_0) {
            Advance();
            continue;
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
done:
    // 子窓を消すと、Explorer が OS の壁紙を描き直す
    app.windows.clear();
    WTSUnRegisterSessionNotification(app.messageWindow);
    CloseHandle(app.timer);
    app.image = {};
    app.gpu.Reset();
    app.wic.Reset();
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
