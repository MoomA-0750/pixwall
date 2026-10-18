#include "desktop.h"

namespace pixwall {

DesktopParent FindDesktopParent() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return {};

    // アイコンの後ろに壁紙を描く WorkerW を作らせる（Lively Wallpaper などと同じ）
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, nullptr);

    // 24H2 から: Progman の子に SHELLDLL_DefView（アイコン）と WorkerW（OS の壁紙）があり、
    // アイコンが手前。その間（アイコンのすぐ後ろ）に入れる
    HWND defView = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    HWND worker = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
    if (defView && worker) return {progman, defView, true};

    // 24H2 より前: SHELLDLL_DefView を子に持つ最上位の窓のすぐ後ろに、空の WorkerW ができる
    HWND found = nullptr;
    EnumWindows(
        [](HWND top, LPARAM lp) -> BOOL {
            if (!FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) return TRUE;
            *reinterpret_cast<HWND*>(lp) = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&found));
    if (found) return {found, HWND_TOP, false};
    return {};
}

}  // namespace pixwall
