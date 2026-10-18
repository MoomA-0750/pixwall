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
    double position = 0.5;  // 画面に合わせる・埋めるときの位置（0 で上・左、1 で下・右）
    // 余白の色。false なら画像の1枚目の左上の1ドットの色、true なら backgroundColor（選んでいれば）
    bool customBackground = false;
    int backgroundColor = -1;  // 自分で選んだ余白の色（0xRRGGBB）。-1 ならまだ選んでいない。自動にしても覚えておく
    double speed = 1;  // 速さの倍率（0.25〜4）
    bool pauseOnLowPower = true;
    bool launchAtLogin = true;
    bool operator==(const Config&) const = default;
};

Config ParseConfig(std::string_view json);
// "#RRGGBB" を 0xRRGGBB にする。形が違えば -1
int ParseHexColor(std::string_view s);
// Mac 版と同じく、キーを名前順に並べ、字下げして書く
std::string FormatConfig(const Config& config);

}  // namespace pixwall
