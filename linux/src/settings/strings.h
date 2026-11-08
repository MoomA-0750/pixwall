// 設定画面の言葉。OS の言語の並び（LANGUAGE・LANG）のうち、日本語と英語の先にあるほう。どちらも無ければ英語。
// 言葉は Windows 版の Resources.resw にそろえ、Linux に合わない所（省電力・OS の壁紙）だけ変える。
// GTK 版（settings/gtk）と QML 版（settings/qt）で同じ表を使う。QML 版は名前（kStringNames）で引く
#pragma once

#include <glib.h>

#include <cstring>
#include <string>
#include <utility>

namespace pixwall {

// X(名前, 日本語, 英語)
#define PIXWALL_STRINGS(X) \
    X(WindowTitle, "pixwall の設定", "pixwall Settings") \
    X(RefreshLabel, "壁紙が画面に合っていないとき", "When the wallpaper doesn't fit the screen") \
    X(RefreshButton, "表示を更新", "Refresh Display") \
    X(NotRunning, "pixwall が動いていません", "pixwall isn't running") \
    X(Start, "開始", "Start") \
    X(StartFailedGnome, "拡張機能を読み込めませんでした。一度ログアウトしてから、もう一度ログインしてください", "Couldn't load the extension. Log out and log in again") \
    X(ImageMain, "画像", "Image") \
    X(ImageFrom, "画像（{0} から）", "Image (from {0})") \
    X(ImageLight, "画像（ライト）", "Image (Light)") \
    X(ImageDark, "画像（ダーク）", "Image (Dark)") \
    X(AllDisplays, "すべての画面", "All Displays") \
    X(Choose, "選ぶ…", "Choose…") \
    X(NoImage, "選んでいない", "None") \
    X(StillLabel, "静止画", "Still Image") \
    X(ExportButton, "静止画を書き出す…", "Export Still Image…") \
    X(FrameOf, "書き出す絵: {0} 枚のうち {1} 枚目", "Frame to export: {1} of {0}") \
    X(DisplayHeading, "表示", "Display") \
    X(Fit, "並べ方", "Scaling") \
    X(FitInteger, "整数倍で最大", "Largest Whole Multiple") \
    X(FitFit, "画面に合わせる", "Fit to Screen") \
    X(FitFill, "画面を埋める", "Fill Screen") \
    X(FitFree, "自由に置く", "Free Placement") \
    X(Position, "位置", "Position") \
    X(PositionHint, "0 で上・左、100 で下・右", "0 is top or left, 100 is bottom or right") \
    X(FreeSize, "大きさ", "Size") \
    X(FreeSizeHint, "100% で画面に合わせた大きさ", "100% is the size that fits the screen") \
    X(FreeX, "横の位置", "Horizontal Position") \
    X(FreeXHint, "0 で左の端、100 で右の端", "0 is the left edge, 100 is the right edge") \
    X(FreeY, "縦の位置", "Vertical Position") \
    X(FreeYHint, "0 で上の端、100 で下の端", "0 is the top edge, 100 is the bottom edge") \
    X(Background, "余白の色", "Margin Color") \
    X(BackgroundHint, "自動では画像の左上の色", "Automatic uses the image's top-left pixel") \
    X(BackgroundAuto, "自動", "Automatic") \
    X(BackgroundCustom, "自分で選ぶ", "Custom") \
    X(Speed, "速さ", "Speed") \
    X(SpeedTimes, "{0}倍", "{0}×") \
    X(PauseHeading, "止める", "Pause") \
    X(LowPower, "省電力モードのときは止める", "Pause in Power Saver") \
    X(Pause, "一時停止", "Paused") \
    X(StartupHeading, "起動", "Startup") \
    X(Login, "ログイン時に起動", "Open at Sign-in") \
    X(LoginHint, "uwsm で始める Hyprland（Omarchy など）はこれで始まります。使っていなければ、Hyprland の設定で pixwall を起動してください", "Works with Hyprland started by uwsm (such as Omarchy). Otherwise, start pixwall from your Hyprland config") \
    X(QuitLabel, "壁紙を止めて、元の壁紙に戻す", "Stop the wallpaper and go back to the desktop wallpaper") \
    X(QuitButton, "pixwall を終了", "Quit pixwall") \
    X(CouldNotRead, "読めませんでした: {0}", "Couldn't read: {0}") \
    X(Exported, "書き出しました: {0}", "Exported: {0}") \
    X(ExportFailed, "書き出せませんでした: {0}", "Couldn't export: {0}") \
    X(SwitchLabel, "画像の切り替え", "Switch Images") \
    X(SwitchHint, "時刻や外観ごとに、画像と並べ方を選べます", "Choose an image and scaling for each time or appearance") \
    X(SwitchNone, "切り替えない", "Don't Switch") \
    X(SwitchTime, "時刻で", "By Time") \
    X(SwitchAppearance, "ダーク・ライトで", "By Light or Dark") \
    X(AddTime, "時刻を足す", "Add Time") \
    X(EditAppearance, "編集する外観", "Appearance to Edit") \
    X(Light, "ライト", "Light") \
    X(Dark, "ダーク", "Dark") \
    X(Now, "今", "Now") \
    X(RemoveTime, "削除", "Remove") \
    X(StartTime, "始まる時刻", "Start time") \
    X(Images, "画像", "Images") \
    X(Cancel, "キャンセル", "Cancel") \
    X(SwitchBy, "切り替え方", "Switch By") \
    X(SwitchOmarchy, "Omarchy の壁紙で", "By Omarchy Wallpaper") \
    X(ImageOmarchy, "画像（Omarchy の壁紙から）", "Image (from the Omarchy Wallpaper)") \
    X(OmarchyHint, "壁紙のセレクターで選んだ静止画と同じフォルダーの pixwall/ に、同じ名前の GIF を置くと動きます: {0}", "Put a GIF with the same name in pixwall/ next to the wallpaper you choose in the selector: {0}") \
    X(OmarchyNoPair, "この壁紙と組になる GIF が無いので、Omarchy の静止画を出しています", "This wallpaper has no paired GIF, so Omarchy shows the still image") \
    X(Keep, "終了しても壁紙を残す", "Keep the Wallpaper After Quitting") \
    X(QuitKeeps, "壁紙を止めて、今の画像の1枚目を GNOME の壁紙に残す", "Stop the wallpaper and keep the first frame as the GNOME wallpaper")

enum class S {
#define PIXWALL_STRING_ENUM(name, ja, en) name,
    PIXWALL_STRINGS(PIXWALL_STRING_ENUM)
#undef PIXWALL_STRING_ENUM
    Count
};

inline constexpr const char* kStringNames[] = {
#define PIXWALL_STRING_NAME(name, ja, en) #name,
    PIXWALL_STRINGS(PIXWALL_STRING_NAME)
#undef PIXWALL_STRING_NAME
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
#define PIXWALL_STRING_TEXT(name, ja, en) {ja, en},
        PIXWALL_STRINGS(PIXWALL_STRING_TEXT)
#undef PIXWALL_STRING_TEXT
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
