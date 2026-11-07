#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core.h"
#include "image_loader.h"

namespace pixwall {

struct ScreenSize {
    int width = 0;
    int height = 0;
};

// 不透明な BGRA（width * height * 4 バイト）を PNG に書く。別のファイルに書いてから入れ替える
bool WritePng(const std::string& path, const std::vector<uint8_t>& bgra, int width, int height);

// 画面ごとの物理ピクセルの大きさ（GDK で数える。拡大率を掛けた大きさ）。一番目が主の画面
std::vector<ScreenSize> MonitorSizes();

// 「静止画を書き出す」。frame 枚目を、画面ごとにその画面の大きさで描く。主の画面は path に、
// ほかの画面は同じフォルダに <画像の名前>-<幅>x<高さ>.png（あれば -2, -3…）で書く
bool ExportStill(const LoadedImage& image, const std::string& imagePath, size_t frame, const Placement& placement,
                 int rgb, const std::vector<ScreenSize>& sizes, const std::string& path);

}  // namespace pixwall
