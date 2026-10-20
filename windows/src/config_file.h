// config.json のファイルとしての読み書き（常駐側と pixwallctl.exe で使う）
#pragma once

#include <windows.h>

#include <string>

#include "config.h"

namespace pixwall {

std::wstring Wide(const std::string& s);
std::string Utf8(const std::wstring& w);

// %APPDATA%\pixwall\config.json
std::wstring ConfigPath();
// 無い・壊れているときは既定の設定
Config LoadConfig(const std::wstring& path);
// 別のファイルに書いてから入れ替える。入れ替えられなければ false
bool SaveConfig(const std::wstring& path, const Config& config);

}  // namespace pixwall
