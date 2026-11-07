// config.json のファイルとしての読み書きと、時刻・外観（常駐側・pixwallctl・設定画面で使う）
#pragma once

#include <string>

#include "config.h"

namespace pixwall {

// $XDG_CONFIG_HOME/pixwall/config.json（無ければ ~/.config/pixwall/config.json）
std::string ConfigPath();
// 無い・壊れているときは既定の設定
Config LoadConfig(const std::string& path);
// 別のファイルに書いてから入れ替える。書けなければ false
bool SaveConfig(const std::string& path, const Config& config);

// 今の時刻（この PC の時間帯）を 0 時からの分にする
int LocalMinute();
// デスクトップの外観がダークか。xdg-desktop-portal の org.freedesktop.appearance の color-scheme
// （GNOME・Hyprland とも、「ダークを好む」が 1）。ポータルが無ければ GNOME の color-scheme を見る
bool SystemIsDark();

// 今いる場所からのパスを絶対パスにする
std::string AbsolutePath(const std::string& path);

}  // namespace pixwall
