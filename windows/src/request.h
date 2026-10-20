#pragma once

#include <windows.h>

namespace pixwall {

// 設定画面・pixwallctl.exe から常駐側の隠れた窓（pixwall-message）へ WM_COPYDATA で届く頼みごと
// （COPYDATASTRUCT の dwData）。設定画面の Resident.cs と値をそろえる
enum Request : ULONG_PTR {
    kReloadConfig = 1,  // config.json を読み直して当てる
    kRefresh = 2,       // 画面を数え直して作り直す（「表示を更新」）
    kSetPaused = 3,     // lpData の 1 バイト目が 0 以外なら一時停止
    kQuit = 4,          // 終わる
    kIsPaused = 5,      // 一時停止しているか。している 2、していない 1 を返す
    kExportStill = 6,   // 静止画を書き出す。lpData は UTF-16 の「フレームの番号\n保存先」。できたら 1
    kRenderPreview = 7, // プレビューを描く。lpData は UTF-16 の「フレームの番号\n長い辺の上限（画素）\n保存先」。上限に収まる整数倍で描く。できたら 1
    kFrameCount = 8,    // 動かしている画像のフレームの数を返す（無ければ 0）
    kAutoColor = 9,     // 自動の余白の色（画像の左上の1ドット）を 0x1RRGGBB で返す（画像が無ければ 0）
};

}  // namespace pixwall
