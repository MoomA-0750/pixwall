// 設定画面の言葉。OS の言語の並び（LANGUAGE・LANG）のうち、日本語と英語の先にあるほう。どちらも無ければ英語。
// 言葉は Windows 版の Resources.resw にそろえ、Linux に合わない所（省電力・OS の壁紙）だけ変える
#pragma once

#include <glib.h>

#include <cstring>
#include <string>

namespace pixwall {

enum class S {
    WindowTitle, RefreshLabel, RefreshButton, NotRunning, Start, StartFailedGnome, ImageMain, ImageFrom, ImageLight,
    ImageDark, AllDisplays, Choose, NoImage, StillLabel, ExportButton, FrameOf, DisplayHeading, Fit, FitInteger,
    FitFit, FitFill, FitFree, Position, PositionHint, FreeSize, FreeSizeHint, FreeX, FreeXHint, FreeY, FreeYHint,
    Background, BackgroundHint, BackgroundAuto, BackgroundCustom, Speed, SpeedTimes, PauseHeading, LowPower, Pause,
    StartupHeading, Login, LoginHint, QuitLabel, QuitButton, CouldNotRead, Exported, ExportFailed, SwitchLabel,
    SwitchHint, SwitchNone, SwitchTime, SwitchAppearance, AddTime, EditAppearance, Light, Dark, Now, RemoveTime,
    StartTime, Images, Cancel, SwitchBy, SwitchOmarchy, ImageOmarchy, OmarchyHint, OmarchyNoPair, Count
};

inline bool Japanese() {
    static const bool ja = [] {
        for (const gchar* const* name = g_get_language_names(); *name; ++name) {
            if (std::strncmp(*name, "ja", 2) == 0) return true;
            if (std::strncmp(*name, "en", 2) == 0) return false;
        }
        return false;
    }();
    return ja;
}

inline const char* T(S key) {
    static const char* const kText[][2] = {
        {"pixwall の設定", "pixwall Settings"},
        {"壁紙が画面に合っていないとき", "When the wallpaper doesn't fit the screen"},
        {"表示を更新", "Refresh Display"},
        {"pixwall が動いていません", "pixwall isn't running"},
        {"開始", "Start"},
        {"拡張機能を読み込めませんでした。一度ログアウトしてから、もう一度ログインしてください",
         "Couldn't load the extension. Log out and log in again"},
        {"画像", "Image"},
        {"画像（{0} から）", "Image (from {0})"},
        {"画像（ライト）", "Image (Light)"},
        {"画像（ダーク）", "Image (Dark)"},
        {"すべての画面", "All Displays"},
        {"選ぶ…", "Choose…"},
        {"選んでいない", "None"},
        {"静止画", "Still Image"},
        {"静止画を書き出す…", "Export Still Image…"},
        {"書き出す絵: {0} 枚のうち {1} 枚目", "Frame to export: {1} of {0}"},
        {"表示", "Display"},
        {"並べ方", "Scaling"},
        {"整数倍で最大", "Largest Whole Multiple"},
        {"画面に合わせる", "Fit to Screen"},
        {"画面を埋める", "Fill Screen"},
        {"自由に置く", "Free Placement"},
        {"位置", "Position"},
        {"0 で上・左、100 で下・右", "0 is top or left, 100 is bottom or right"},
        {"大きさ", "Size"},
        {"100% で画面に合わせた大きさ", "100% is the size that fits the screen"},
        {"横の位置", "Horizontal Position"},
        {"0 で左の端、100 で右の端", "0 is the left edge, 100 is the right edge"},
        {"縦の位置", "Vertical Position"},
        {"0 で上の端、100 で下の端", "0 is the top edge, 100 is the bottom edge"},
        {"余白の色", "Margin Color"},
        {"自動では画像の左上の色", "Automatic uses the image's top-left pixel"},
        {"自動", "Automatic"},
        {"自分で選ぶ", "Custom"},
        {"速さ", "Speed"},
        {"{0}倍", "{0}×"},
        {"止める", "Pause"},
        {"省電力モードのときは止める", "Pause in Power Saver"},
        {"一時停止", "Paused"},
        {"起動", "Startup"},
        {"ログイン時に起動", "Open at Sign-in"},
        {"uwsm で始める Hyprland（Omarchy など）はこれで始まります。使っていなければ、Hyprland の設定で pixwall を起動してください",
         "Works with Hyprland started by uwsm (such as Omarchy). Otherwise, start pixwall from your Hyprland config"},
        {"壁紙を止めて、元の壁紙に戻す", "Stop the wallpaper and go back to the desktop wallpaper"},
        {"pixwall を終了", "Quit pixwall"},
        {"読めませんでした: {0}", "Couldn't read: {0}"},
        {"書き出しました: {0}", "Exported: {0}"},
        {"書き出せませんでした: {0}", "Couldn't export: {0}"},
        {"画像の切り替え", "Switch Images"},
        {"時刻や外観ごとに、画像と並べ方を選べます", "Choose an image and scaling for each time or appearance"},
        {"切り替えない", "Don't Switch"},
        {"時刻で", "By Time"},
        {"ダーク・ライトで", "By Light or Dark"},
        {"時刻を足す", "Add Time"},
        {"編集する外観", "Appearance to Edit"},
        {"ライト", "Light"},
        {"ダーク", "Dark"},
        {"今", "Now"},
        {"削除", "Remove"},
        {"始まる時刻", "Start time"},
        {"画像", "Images"},
        {"キャンセル", "Cancel"},
        {"切り替え方", "Switch By"},
        {"Omarchy の壁紙で", "By Omarchy Wallpaper"},
        {"画像（Omarchy の壁紙から）", "Image (from the Omarchy Wallpaper)"},
        {"壁紙のセレクターで選んだ静止画と同じフォルダーの pixwall/ に、同じ名前の GIF を置くと動きます: {0}",
         "Put a GIF with the same name in pixwall/ next to the wallpaper you choose in the selector: {0}"},
        {"この壁紙と組になる GIF が無いので、Omarchy の静止画を出しています", "This wallpaper has no paired GIF, so Omarchy shows the still image"},
    };
    static_assert(sizeof(kText) / sizeof(kText[0]) == static_cast<size_t>(S::Count));
    return kText[static_cast<int>(key)][Japanese() ? 0 : 1];
}

// {0}・{1} を入れる
inline std::string Format(S key, const std::string& a, const std::string& b = {}) {
    std::string s = T(key);
    for (const auto& [mark, value] : {std::pair<std::string, std::string>{"{0}", a}, {"{1}", b}}) {
        const size_t at = s.find(mark);
        if (at != std::string::npos) s.replace(at, mark.size(), value);
    }
    return s;
}

}  // namespace pixwall
