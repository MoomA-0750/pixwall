// pixwall.exe（常駐側）。試作の間は、動かす画像を引数で1つ渡す: pixwall.exe <画像のパス>
// 終えるときは taskkill /im pixwall.exe（隠れた窓に WM_CLOSE が届いて片付けてから終わる）
#include <windows.h>
#include <d2d1.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <wtsapi32.h>

#include <memory>
#include <string>
#include <vector>

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

// winnt.h の GUID_CONSOLE_DISPLAY_STATE と GUID_POWER_SAVING_STATUS と同じ値。
// SDK の定義を使うと INITGUID やリンクするライブラリに左右されるので、ここで持つ
constexpr GUID kDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
constexpr GUID kPowerSaving = {0xe00958c0, 0xc213, 0x4ace, {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};

struct App {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Factory> d2d;
    LoadedImage image;
    D2D1_COLOR_F background{0, 0, 0, 1};
    std::vector<std::unique_ptr<WallWindow>> windows;
    HWND messageWindow = nullptr;
    UINT taskbarCreated = 0;

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
D2D1_COLOR_F TopLeftColor(const LoadedImage& image) {
    const uint8_t* p = image.frames[0].data();
    const float a = p[3] / 255.0f;
    if (a <= 0) return {0, 0, 0, 1};
    return {p[2] / 255.0f / a, p[1] / 255.0f / a, p[0] / 255.0f / a, 1};
}

void RenderAll() {
    for (auto& w : app.windows) w->Render(app.frame);
}

void ArmNext() {
    const long long delay = static_cast<long long>(FrameDelay(app.image.delays[app.frame]) * 10'000'000);
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
    const DesktopParent desktop = FindDesktopParent();
    if (!desktop.parent) {
        OutputDebugStringW(L"pixwall: アイコンの後ろに入れる窓が見つからない\n");
        UpdateRunning();
        return;
    }
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
        auto w = std::make_unique<WallWindow>(app.d2d.Get(), app.image, rc, desktop, app.background, Fit::Integer);
        if (w->ok()) app.windows.push_back(std::move(w));
    }
    RenderAll();
    UpdateRunning();
}

void ScheduleRebuild(HWND hwnd) { SetTimer(hwnd, kRebuildTimer, kRebuildDelayMs, nullptr); }

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

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 2) {
        MessageBoxW(nullptr, L"使い方: pixwall.exe <画像のパス>", L"pixwall", MB_OK);
        return 2;
    }
    const std::wstring path = argv[1];
    LocalFree(argv);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&app.wic));
    if (!app.wic || !LoadImageFile(app.wic.Get(), path, app.image)) {
        MessageBoxW(nullptr, (L"画像を読めない: " + path).c_str(), L"pixwall", MB_OK);
        return 1;
    }
    app.background = TopLeftColor(app.image);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, app.d2d.GetAddressOf());

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
    app.d2d.Reset();
    app.wic.Reset();
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
