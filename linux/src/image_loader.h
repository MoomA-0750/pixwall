#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pixwall {

// 全フレームを元の大きさのまま、重ね方を当てた完成した絵にして持つ（Windows 版と同じ形）
struct LoadedImage {
    int width = 0;
    int height = 0;
    std::vector<std::vector<uint8_t>> frames;  // premultiplied の BGRA、width * height * 4 バイト
    std::vector<double> delays;                // GIF に書かれた待ち時間（秒）
};

// GIF はこちらで読み（重ね方は core の GifCompositor）、ほかの形式（PNG・WebP・JPEG など）は
// gdk-pixbuf で1枚目を読む。読めなければ false
bool LoadImageFile(const std::string& path, LoadedImage& out);

// GIF のバイト列を読む。途中で壊れていれば、そこまでのフレームを返す。1枚も読めなければ false
bool DecodeGif(const uint8_t* data, size_t size, LoadedImage& out);

// 余白の「自動」の色（1枚目の左上の1ドット。premultiplied を戻す。透明なら黒）を 0xRRGGBB で
int TopLeftColor(const LoadedImage& image);

}  // namespace pixwall
