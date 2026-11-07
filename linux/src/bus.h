// 常駐側（Hyprland などは pixwall、GNOME はシェルの拡張機能）への頼みごと。セッションの D-Bus で届ける。
// 名前と形は GNOME の拡張機能（gnome-extension/extension.js）とそろえる
#pragma once

#include <gio/gio.h>

#include <string>

namespace pixwall {

constexpr const char* kBusName = "xyz.mooma0750.pixwall";
constexpr const char* kBusPath = "/xyz/mooma0750/pixwall";
constexpr const char* kBusInterface = "xyz.mooma0750.pixwall";

// Reload: config.json を読み直して当てる
// Refresh: 画面を数え直して作り直す（「表示を更新」）
// SetPaused(b): 一時停止する・やめる
// IsPaused() -> b
// Quit: 終わる（OS の壁紙を戻す）
extern const char* const kBusXml;

// 常駐側が動いているか（名前を持っているプロセスがいるか）
bool ResidentRunning();
// 引数は無いか、(b) の1つ。返りは g_variant_unref する。動いていなければ nullptr
GVariant* CallResident(const char* method, GVariant* args = nullptr);

}  // namespace pixwall
