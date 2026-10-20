// コマンドライン（pixwallctl.exe）の引数を読み、設定の値を読み書きする。Mac 版の Cli.swift と同じ決まり
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "config.h"

namespace pixwall {

// 頼めること
enum class CliAction { Show, Get, Set, Status, Pause, Resume, Refresh, Quit, Export, Help };

struct CliCommand {
    CliAction action = CliAction::Help;
    std::vector<std::pair<std::string, std::string>> pairs;  // Get はキー1つ（値は空）、Set は書いた順のキーと値
    std::string path;                                        // Export の保存先
    int frame = 0;                                           // Export の何枚目か（0 から）
};

extern const char* const kCliUsage;

// 引数（コマンドの名前より後、UTF-8）を読む。形が違えば false で、error にわけを入れる
bool ParseCli(const std::vector<std::string>& args, CliCommand& out, std::string& error);

// config.json のキーなら true
bool IsConfigKey(const std::string& key);

// pixwallctl get が出す値。選んでいない画像・色は空
std::string ConfigValue(const Config& config, const std::string& key);

// pixwallctl set の値を当てる。範囲の外・形の違う値は false で、error にわけを入れ、設定は変えない。
// 画像のパスはそのまま入れる（絶対パスにする・読めるか確かめるのは呼ぶ側）
bool SetConfigValue(Config& config, const std::string& key, const std::string& value, std::string& error);

}  // namespace pixwall
