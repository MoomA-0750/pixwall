// 設定（config.json）。Mac 版の Config と同じ形で読み書きする。
// 足りない項目・読めない値は既定の値にする（壊れていても起動できる）
#pragma once

#include <string>
#include <string_view>

#include "core.h"

namespace pixwall {

struct Config {
    int version = 1;
    std::string image;  // 動かす画像のパス（UTF-8）。空なら選んでいない
    Fit fit = Fit::Integer;
    double speed = 1;  // 速さの倍率（0.25〜4）
    bool pauseOnLowPower = true;
    bool launchAtLogin = true;
    bool operator==(const Config&) const = default;
};

Config ParseConfig(std::string_view json);
// Mac 版と同じく、キーを名前順に並べ、字下げして書く
std::string FormatConfig(const Config& config);

}  // namespace pixwall
