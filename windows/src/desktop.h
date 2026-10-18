#pragma once

#include <windows.h>

namespace pixwall {

// 壁紙の窓をどこに子として入れるか
struct DesktopParent {
    HWND parent = nullptr;
    // この窓のすぐ後ろに入れる。HWND_TOP なら親の中の一番手前
    HWND insertAfter = HWND_TOP;
    // Windows 11 24H2 からの並び（WorkerW とアイコンが Progman の子）か
    bool progmanChildren = false;
};

// Progman に 0x052C を送って WorkerW を作らせ、アイコンより後ろに入れられる親を探す。
// 見つからなければ parent が nullptr
DesktopParent FindDesktopParent();

}  // namespace pixwall
