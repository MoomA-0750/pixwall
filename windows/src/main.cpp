// pixwall.exe（常駐側）。動かす画像は %APPDATA%\pixwall\config.json から読む。
// 引数で画像を渡すと（pixwall.exe <画像のパス>）、それを動かして設定にも書く。
// もう一度起動されたら、設定画面（settings\PixwallSettings.exe）を開いて自分は終わる。
// 設定画面とは、隠れた窓（pixwall-message）への WM_COPYDATA でやり取りする。
// 終えるときは taskkill /im pixwall.exe（隠れた窓に WM_CLOSE が届いて片付けてから終わる）
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <wtsapi32.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"
#include "core.h"
#include "desktop.h"
#include "image_loader.h"
#include "still.h"
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

// 設定画面から WM_COPYDATA で届く頼みごと（COPYDATASTRUCT の dwData）。設定画面の Resident.cs と値をそろえる
enum Request : ULONG_PTR {
    kReloadConfig = 1,  // config.json を読み直して当てる
    kRefresh = 2,       // 画面を数え直して作り直す（「表示を更新」）
    kSetPaused = 3,     // lpData の 1 バイト目が 0 以外なら一時停止
    kQuit = 4,          // 終わる
    kIsPaused = 5,      // 一時停止しているか。している 2、していない 1 を返す
    kExportStill = 6,   // 静止画を書き出す。lpData は UTF-16 の「フレームの番号\n保存先」。できたら 1
    kRenderPreview = 7, // プレビューを描く。lpData は UTF-16 の「フレームの番号\n長い辺の上限（画素）\n保存先」。上限に収まる整数倍で描く。できたら 1
    kFrameCount = 8,    // 動かしている画像のフレームの数を返す（無ければ 0）
};

// WM_COPYDATA は相手が返事を待つ送り方で届き、その間はほかのプロセスの COM（IDesktopWallpaper）を呼べない
// （RPC_E_CANTCALLOUT_ININPUTSYNCCALL）。OS の壁紙に触る頼みは、返事をしてから自分宛てに送り直して行う。
// wParam は Request
constexpr UINT kDeferredRequest = WM_APP + 1;

struct App {
    ComPtr<IWICImagingFactory> wic;
    Gpu gpu;
    Config config;
    LoadedImage image;
    Color background;
    std::vector<std::unique_ptr<WallWindow>> windows;
    OsWallpaper osWallpaper;
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

std::wstring Wide(const std::string& s);

uint8_t Byte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f); }

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
    if (app.image.frames.empty()) {
        // 画像をまだ選んでいない・読めない。OS の壁紙のまま（入れ替えていたら戻す）
        app.osWallpaper.Restore();
        UpdateRunning();
        return;
    }
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
        auto w = std::make_unique<WallWindow>(app.gpu, app.image, rc, desktop, app.background, app.config.fit,
                                              app.config.position);
        if (w->ok()) app.windows.push_back(std::move(w));
    }
    UpdateRunning();
    // OS の壁紙にも1枚目を入れる（タスク ビューや Explorer の作り直しの間、壁紙が見えたときも同じ絵になる）
    app.osWallpaper.Apply(app.wic.Get(), app.image, Wide(app.config.image), app.config.fit, app.config.position,
                          Byte(app.background.r), Byte(app.background.g), Byte(app.background.b));
    // 起動と作り直しで触ったメモリ（ほとんどはドライバーの初期化）を手放す。
    // 動いている間に触るのは 1MB ほどで、to-fu ではプライベート ワーキング セットが 11.6MB から 1MB 未満になった
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void ScheduleRebuild(HWND hwnd) {
    app.retries = 0;
    SetTimer(hwnd, kRebuildTimer, kRebuildDelayMs, nullptr);
}

std::wstring ConfigPath();
Config LoadConfig(const std::wstring& path);
void ApplyConfig(const Config& next);

// WM_COPYDATA の UTF-16 の中身を、改行で分ける
std::vector<std::wstring> Lines(const COPYDATASTRUCT* data) {
    std::wstring text(static_cast<const wchar_t*>(data->lpData), data->cbData / sizeof(wchar_t));
    std::vector<std::wstring> lines;
    size_t start = 0;
    for (size_t nl; (nl = text.find(L'\n', start)) != std::wstring::npos; start = nl + 1) {
        lines.push_back(text.substr(start, nl - start));
    }
    lines.push_back(text.substr(start));
    return lines;
}

LRESULT OnRequest(const COPYDATASTRUCT* data) {
    switch (data->dwData) {
        case kReloadConfig:
        case kRefresh:
        case kQuit:
            PostMessageW(app.messageWindow, kDeferredRequest, data->dwData, 0);
            return 1;
        case kSetPaused:
            app.pause.userPaused = data->cbData >= 1 && static_cast<const uint8_t*>(data->lpData)[0] != 0;
            UpdateRunning();
            return 1;
        case kIsPaused:
            return app.pause.userPaused ? 2 : 1;
        case kExportStill: {
            const auto lines = Lines(data);
            if (lines.size() < 2) return 0;
            return ExportStill(app.wic.Get(), app.image, Wide(app.config.image), _wtoi(lines[0].c_str()),
                               app.config.fit, app.config.position, Byte(app.background.r), Byte(app.background.g),
                               Byte(app.background.b), lines[1])
                       ? 1
                       : 0;
        }
        case kRenderPreview: {
            const auto lines = Lines(data);
            if (lines.size() < 3 || app.image.frames.empty()) return 0;
            const size_t frame = std::min<size_t>(_wtoi(lines[0].c_str()), app.image.frames.size() - 1);
            const int scale = std::max(1, _wtoi(lines[1].c_str()) / std::max(app.image.width, app.image.height));
            const int w = app.image.width * scale;
            const int h = app.image.height * scale;
            const auto still = RenderStill(app.image.frames[frame].data(), app.image.width, app.image.height, w, h,
                                           Fit::Integer, Byte(app.background.r), Byte(app.background.g),
                                           Byte(app.background.b));
            return WritePng(app.wic.Get(), lines[2], still, w, h) ? 1 : 0;
        }
        case kFrameCount:
            return static_cast<LRESULT>(app.image.frames.size());
        default:
            return 0;
    }
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
        case WM_COPYDATA:
            return OnRequest(reinterpret_cast<const COPYDATASTRUCT*>(lp));
        case kDeferredRequest:
            switch (wp) {
                case kReloadConfig:
                    ApplyConfig(LoadConfig(ConfigPath()));
                    break;
                case kRefresh:
                    app.retries = 0;
                    Rebuild();
                    break;
                case kQuit:
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            // OS の壁紙を戻す。ログオフ・シャットダウンで終わるときは戻さない（次に始まったときも同じ絵を入れるので）
            app.osWallpaper.Restore();
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

// 設定を当てる。画像が変わったとき（と、まだ読めていないとき）だけ読み直す
void ApplyConfig(const Config& next) {
    const bool reload = next.image != app.config.image || app.image.frames.empty();
    app.config = next;
    app.pause.pauseOnEnergySaver = next.pauseOnLowPower;
    UpdateLaunchAtLogin(next.launchAtLogin);
    // 窓は今の画像を参照しているので、画像を替える前に消す
    app.windows.clear();
    if (reload) {
        app.image = {};
        app.frame = 0;
        if (!next.image.empty() && app.wic && !LoadImageFile(app.wic.Get(), Wide(next.image), app.image)) {
            OutputDebugStringW(L"pixwall: 画像を読めない\n");
            app.image = {};
        }
        if (!app.image.frames.empty()) app.background = TopLeftColor(app.image);
    }
    app.retries = 0;
    Rebuild();
}

// 設定画面を開く。pixwall.exe の隣の settings フォルダにある
void OpenSettings() {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return;
    std::wstring path = exe;
    path = path.substr(0, path.find_last_of(L'\\')) + L"\\settings\\PixwallSettings.exe";
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
        MessageBoxW(nullptr, (L"設定画面が見つからない: " + path).c_str(), L"pixwall", MB_OK);
    }
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

    // 2回目の起動（スタートメニューから開き直したなど）。設定画面を開いて終わる
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\pixwall");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        OpenSettings();
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring configPath = ConfigPath();
    Config config = LoadConfig(configPath);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2) {
        wchar_t full[MAX_PATH];
        if (GetFullPathNameW(argv[1], MAX_PATH, full, nullptr)) {
            config.image = Utf8(full);
            SaveConfig(configPath, config);
        }
    }
    LocalFree(argv);

    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&app.wic));
    app.messageWindow = CreateMessageWindow(inst);
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    RegisterPowerSettingNotification(app.messageWindow, &kDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    RegisterPowerSettingNotification(app.messageWindow, &kPowerSaving, DEVICE_NOTIFY_WINDOW_HANDLE);
    WTSRegisterSessionNotification(app.messageWindow, NOTIFY_FOR_THIS_SESSION);

    app.timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!app.timer) app.timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    ApplyConfig(config);
    // 画像をまだ選んでいない・読めないときは、何も見えないので設定画面を出す（Mac と同じ）
    if (app.image.frames.empty()) OpenSettings();

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
