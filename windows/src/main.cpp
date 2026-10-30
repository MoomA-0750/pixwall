// pixwall.exe（常駐側）。動かす画像は %APPDATA%\pixwall\config.json から読む。
// 引数で画像を渡すと（pixwall.exe <画像のパス>）、それを動かして設定にも書く。
// もう一度起動されたら、設定画面（settings\PixwallSettings.exe）を開いて自分は終わる。
// 設定画面・pixwallctl.exe とは、隠れた窓（pixwall-message）への WM_COPYDATA でやり取りする（request.h）。
// 終えるときは taskkill /im pixwall.exe（隠れた窓に WM_CLOSE が届いて片付けてから終わる）
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <wtsapi32.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "config_file.h"
#include "core.h"
#include "desktop.h"
#include "image_loader.h"
#include "request.h"
#include "still.h"
#include "wall_window.h"

using Microsoft::WRL::ComPtr;
using namespace pixwall;

namespace {

constexpr UINT_PTR kRebuildTimer = 1;
// 解像度などが変わってから、変化が落ち着くまで待つ時間
constexpr UINT kRebuildDelayMs = 200;
// 並べ方・位置・余白の色を変えてから、OS の壁紙を描き直すまで待つ時間。
// 位置のつまみを動かしている間は窓だけを動かし、PNG を描いて OS に入れる重い処理は止まってから1回だけ行う
constexpr UINT_PTR kWallpaperTimer = 2;
constexpr UINT kWallpaperDelayMs = 500;
// 時刻で切り替えるときに、次の時刻に1回だけ鳴らす
constexpr UINT_PTR kSwitchTimer = 3;
// アイコンの後ろに入れる窓が見つからないとき（Explorer が立ち上がる途中など）に探し直す間隔と回数
constexpr UINT kRetryDelayMs = 1000;
// 前面の窓の位置を見る間隔。最大化・元に戻すは出来事が来ないことがあるので（Win+↑ など）、これで気づく。
// 前面の窓1つを見るだけなので軽い。すべての窓を数え直すのは重いので（to-fu で 1 回 220 万サイクル、窓 443 個）、
// 前面の窓が替わった・動いたときと、出来事が来たときだけにする
constexpr DWORD kForegroundPollMs = 250;
// 出来事が来てから数え直すまで待つ時間。離した直後はまだ最大化し終えていないことがあり、
// 窓がいくつも続けて出たときなどもまとめて1回にする
constexpr DWORD kCoverageEventDelayMs = 50;
constexpr int kMaxRetries = 30;

// winnt.h の GUID_CONSOLE_DISPLAY_STATE と GUID_POWER_SAVING_STATUS と同じ値。
// SDK の定義を使うと INITGUID やリンクするライブラリに左右されるので、ここで持つ
constexpr GUID kDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
constexpr GUID kPowerSaving = {0xe00958c0, 0xc213, 0x4ace, {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};

// WM_COPYDATA は相手が返事を待つ送り方で届き、その間はほかのプロセスの COM（IDesktopWallpaper）を呼べない
// （RPC_E_CANTCALLOUT_ININPUTSYNCCALL）。OS の壁紙に触る頼みは、返事をしてから自分宛てに送り直して行う。
// wParam は Request
constexpr UINT kDeferredRequest = WM_APP + 1;
// 覆われているかを見るスレッドから、数え直した結果が届く。wParam が 0 以外なら、すべての画面が覆われている
constexpr UINT kCoverageChanged = WM_APP + 2;

struct App {
    ComPtr<IWICImagingFactory> wic;
    Gpu gpu;
    Config saved;   // config.json のとおりの設定
    Config config;  // 今出すもの（時刻・ダークモードで選んだ画像と並べ方）を image と並べ方に入れた設定。描くのはこちら
    LoadedImage image;
    Color cornerColor;  // 画像の1枚目の左上の1ドット（自動の余白の色）
    Color background;   // 今使っている余白の色
    std::vector<std::unique_ptr<WallWindow>> windows;
    OsWallpaper osWallpaper;
    HWND messageWindow = nullptr;
    UINT taskbarCreated = 0;
    int retries = 0;

    PauseReasons pause;
    // ほかの窓に覆われているかを見るスレッド。動かす必要があるときだけ動かす
    std::thread coverageThread;
    std::atomic<bool> coverageStop{false};
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

uint8_t Byte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f); }

// 自分で選んだ色があればそれ、無ければ画像の左上の1ドットの色
Color BackgroundFor(const Config& config, Color corner) {
    if (!config.customBackground || config.backgroundColor < 0) return corner;
    const int c = config.backgroundColor;
    return {((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, 1};
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

// ---- ほかの窓に覆われているか ----

// 覆う窓として数えるか。タスクバーや Alt+Tab に出るふつうのアプリの窓と、そのダイアログだけ数える。
// 見えていない・最小化した・ほかの仮想デスクトップの（cloaked）窓、クリックを素通しする重ね絵、
// ツールの窓（メニュー・ツールチップなど）、デスクトップそのもの（Progman・WorkerW）、
// どの画面にもほとんどかからない窓は数えない。
// 安い判定を先にする。DWM に聞く2つ（cloaked と見えている枠）は 1 回ごとに DWM とやり取りして重いので、最後に聞く
bool CoverRect(HWND hwnd, const std::vector<PixelRect>& screens, PixelRect& out) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TRANSPARENT) return false;
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return false;
    RECT r;
    if (!GetWindowRect(hwnd, &r)) return false;
    const PixelRect rough{r.left, r.top, r.right - r.left, r.bottom - r.top};
    // 画面の 1% 未満しかかからない窓は、覆うかどうかに効かない
    if (std::none_of(screens.begin(), screens.end(),
                     [&](const PixelRect& s) { return UncoveredFraction(s, {rough}) < 0.99; })) {
        return false;
    }
    wchar_t cls[16];
    if (GetClassNameW(hwnd, cls, 16) && (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0)) return false;
    if (ex & WS_EX_LAYERED) {
        BYTE alpha = 255;
        DWORD flags = 0;
        if (GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0) return false;
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    // 見えている枠（GetWindowRect は、見えない広げるためのふちを含む）
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(hwnd, &r);
    out = {r.left, r.top, r.right - r.left, r.bottom - r.top};
    return true;
}

// すべての画面が、ほかの窓で覆われているか。タスクバーのまわりは見ない（隠れていなくても止める）。
// 窓とタイマーは全画面で1つなので、どれか1つの画面でも見えていれば動かす
bool AllScreensCovered() {
    struct Search {
        std::vector<PixelRect> screens;
        std::vector<PixelRect> covers;
    } s;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(mon, &mi)) {
                const RECT& w = mi.rcWork;
                reinterpret_cast<Search*>(lp)->screens.push_back({w.left, w.top, w.right - w.left, w.bottom - w.top});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&s));
    if (s.screens.empty()) return false;
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(lp);
            PixelRect r;
            if (CoverRect(hwnd, s.screens, r)) s.covers.push_back(r);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&s));
    return std::all_of(s.screens.begin(), s.screens.end(), [&](const PixelRect& screen) { return IsCovered(screen, s.covers); });
}

// 窓が出た・消えた・最小化した・前に来た・ドラッグで動かし終えた・仮想デスクトップを替えた（覆われているかを見るスレッドで呼ばれる）。
// 最初に来た時刻。0 なら来ていない
thread_local ULONGLONG windowChangedAt = 0;

void CALLBACK OnWindowEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    // 窓の中の部品・カーソル・キャレットは捨てる
    if (windowChangedAt || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    // 消えた窓は、もう親もスタイルも読めない
    if (event != EVENT_OBJECT_DESTROY) {
        if (GetAncestor(hwnd, GA_ROOT) != hwnd) return;
        // ツールチップやメニューなど、数えない窓が出た・消えただけなら数え直さない
        const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((ex & WS_EX_TRANSPARENT) || ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW))) return;
    }
    windowChangedAt = GetTickCount64();
}

// ほかの窓の出来事を受けて、覆われているかが変わったら主のスレッドに知らせる。
// 窓の位置の出来事（EVENT_OBJECT_LOCATIONCHANGE）は受けない。カーソルが動くたびにも来て（毎秒数百回）、
// 捨てるだけでも重い（to-fu でマウスを動かしている間 +3〜5 Mcyc/s）。最大化・元に戻すは、前面の窓の位置を
// 時々見て気づく
void WatchCoverage(HWND notify, std::atomic<bool>* stop) {
    const std::pair<DWORD, DWORD> ranges[] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
    };
    std::vector<HWINEVENTHOOK> hooks;
    for (const auto& [first, last] : ranges) {
        if (HWINEVENTHOOK h = SetWinEventHook(first, last, nullptr, OnWindowEvent, 0, 0,
                                              WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS)) {
            hooks.push_back(h);
        }
    }
    bool covered = AllScreensCovered();
    PostMessageW(notify, kCoverageChanged, covered, 0);
    ULONGLONG last = GetTickCount64();
    HWND foreground = GetForegroundWindow();
    RECT foregroundRect{};
    GetWindowRect(foreground, &foregroundRect);
    while (!stop->load()) {
        // 出来事が来たら少し待って数え直す。来なければ前面の窓を時々見る
        const ULONGLONG due = windowChangedAt ? windowChangedAt + kCoverageEventDelayMs : last + kForegroundPollMs;
        const ULONGLONG now = GetTickCount64();
        if (now < due) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(due - now), QS_ALLINPUT);
            // 出来事は、このスレッドのメッセージを取り出すときに OnWindowEvent に届く
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
            continue;
        }
        last = now;
        HWND fg = GetForegroundWindow();
        RECT rect{};
        GetWindowRect(fg, &rect);
        const bool moved = fg != foreground || !EqualRect(&rect, &foregroundRect);
        foreground = fg;
        foregroundRect = rect;
        if (!windowChangedAt && !moved) continue;
        windowChangedAt = 0;
        const bool next = AllScreensCovered();
        if (next != covered) {
            covered = next;
            PostMessageW(notify, kCoverageChanged, covered, 0);
        }
    }
    for (HWINEVENTHOOK h : hooks) UnhookWinEvent(h);
}

// 見るスレッドを止める。待っているところを起こしてから、終わるのを待つ
void StopCoverageWatch() {
    app.coverageStop = true;
    PostThreadMessageW(GetThreadId(app.coverageThread.native_handle()), WM_NULL, 0, 0);
    app.coverageThread.join();
}

// 動かす必要があるときだけ、ほかの窓を見る（ほかの理由で止めている間は見ない）
void UpdateCoverageWatch() {
    PauseReasons others = app.pause;
    others.occluded = false;
    const bool need = !app.windows.empty() && app.image.frames.size() > 1 && !others.ShouldPause();
    if (need && !app.coverageThread.joinable()) {
        app.coverageStop = false;
        app.coverageThread = std::thread(WatchCoverage, app.messageWindow, &app.coverageStop);
    } else if (!need && app.coverageThread.joinable()) {
        StopCoverageWatch();
        app.pause.occluded = false;
    }
}

void UpdateRunning() {
    UpdateCoverageWatch();
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

// OS の壁紙にも1枚目を入れる（タスク ビューや Explorer の作り直しの間、壁紙が見えたときも同じ絵になる）
void ApplyOsWallpaper() {
    KillTimer(app.messageWindow, kWallpaperTimer);
    app.osWallpaper.Apply(app.wic.Get(), app.image, Wide(app.config.image), app.config.placement(),
                          Byte(app.background.r), Byte(app.background.g), Byte(app.background.b));
}

// 画面を数え直して、画面ごとに窓を作り直す。フレームは読み直さない
void Rebuild() {
    KillTimer(app.messageWindow, kWallpaperTimer);
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
        auto w = std::make_unique<WallWindow>(app.gpu, app.image, rc, desktop, app.background,
                                              app.config.placement());
        if (w->ok()) app.windows.push_back(std::move(w));
    }
    UpdateRunning();
    ApplyOsWallpaper();
    // 起動と作り直しで触ったメモリ（ほとんどはドライバーの初期化）を手放す。
    // 動いている間に触るのは 1MB ほどで、to-fu ではプライベート ワーキング セットが 11.6MB から 1MB 未満になった
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void ScheduleRebuild(HWND hwnd) {
    app.retries = 0;
    SetTimer(hwnd, kRebuildTimer, kRebuildDelayMs, nullptr);
}

void ApplyConfig(const Config& next);
void Reselect();

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
                               app.config.placement(), Byte(app.background.r), Byte(app.background.g),
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
        case kAutoColor:
            if (app.image.frames.empty()) return 0;
            return 0x1000000 | (Byte(app.cornerColor.r) << 16) | (Byte(app.cornerColor.g) << 8) | Byte(app.cornerColor.b);
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
            // ダーク・ライトの切り替え
            if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) Reselect();
            return 0;
        case WM_TIMECHANGE:
            Reselect();
            return 0;
        case WM_TIMER:
            if (wp == kSwitchTimer) Reselect();
            if (wp == kWallpaperTimer) ApplyOsWallpaper();
            if (wp == kRebuildTimer) {
                KillTimer(hwnd, kRebuildTimer);
                Rebuild();
            }
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_POWERSETTINGCHANGE) OnPowerSetting(reinterpret_cast<POWERBROADCAST_SETTING*>(lp));
            // スリープの間はタイマーが進まないことがあるので、戻ったら選び直す
            if (wp == PBT_APMRESUMEAUTOMATIC) Reselect();
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
        case kCoverageChanged:
            // 見るのをやめた後に届いた古い結果は捨てる
            if (app.coverageThread.joinable()) {
                app.pause.occluded = wp != 0;
                UpdateRunning();
            }
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
            // 見るスレッドを止めてから終わる（動いたままの std::thread を壊すと落ちる）
            if (app.coverageThread.joinable()) StopCoverageWatch();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
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

// 次に切り替わる時刻（の 0 秒）に1回だけ鳴らす。切り替えないなら止める
void ScheduleSwitch() {
    KillTimer(app.messageWindow, kSwitchTimer);
    const int wait = app.saved.MinutesToNextSwitch(LocalMinute());
    if (wait < 0) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    const UINT ms = static_cast<UINT>(wait) * 60'000 - (t.wSecond * 1000 + t.wMilliseconds);
    SetTimer(app.messageWindow, kSwitchTimer, std::max<UINT>(ms, USER_TIMER_MINIMUM), nullptr);
}

// 時刻・ダークモードが変わったら、出すものを選び直す（同じものなら何も変わらない）
void Reselect() { ApplyConfig(app.saved); }

// 設定を当てる。今の時刻・ダークモードで出すものを選び、次に時刻で切り替えるときのタイマーを掛け直す。
// 画像が変わったとき（と、まだ読めていないとき）だけ読み直す。
// 画像が同じなら窓は作り直さず、並べ方・位置・余白の色だけを変え、OS の壁紙は少し待ってから描き直す
void ApplyConfig(const Config& saved) {
    app.saved = saved;
    ScheduleSwitch();
    const Config next = saved.Resolved(LocalMinute(), SystemIsDark());
    const Config old = app.config;
    const bool reload = next.image != old.image || app.image.frames.empty();
    const Color oldBackground = app.background;
    app.config = next;
    app.background = BackgroundFor(next, app.cornerColor);
    const bool relayout = next.placement() != old.placement() || app.background.r != oldBackground.r ||
                          app.background.g != oldBackground.g || app.background.b != oldBackground.b;
    app.pause.pauseOnEnergySaver = next.pauseOnLowPower;
    UpdateLaunchAtLogin(next.launchAtLogin);
    UpdateRunning();
    if (!reload && !app.windows.empty()) {
        bool ok = true;
        for (auto& w : app.windows) ok = ok && w->SetLayout(app.background, app.config.placement());
        if (ok) {
            if (relayout) SetTimer(app.messageWindow, kWallpaperTimer, kWallpaperDelayMs, nullptr);
            return;
        }
        // GPU が失われた。GPU の道具から作り直す
        app.gpu.Reset();
    }
    // 窓は今の画像を参照しているので、画像を替える前に消す
    app.windows.clear();
    if (reload) {
        app.image = {};
        app.frame = 0;
        if (!next.image.empty() && app.wic && !LoadImageFile(app.wic.Get(), Wide(next.image), app.image)) {
            OutputDebugStringW(L"pixwall: 画像を読めない\n");
            app.image = {};
        }
        if (!app.image.frames.empty()) app.cornerColor = TopLeftColor(app.image);
        app.background = BackgroundFor(next, app.cornerColor);
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
        // 設定画面と同じく、Windows の表示言語が日本語なら日本語、ほかは英語
        const bool japanese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE;
        MessageBoxW(nullptr, ((japanese ? L"設定画面が見つからない: " : L"Couldn't find the settings app: ") + path).c_str(),
                    L"pixwall", MB_OK);
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
