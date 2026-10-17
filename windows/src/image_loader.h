#pragma once

#include <windows.h>
#include <wincodec.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pixwall {

// 全フレームを元の大きさのまま、重ね方を当てた完成した絵にして持つ
struct LoadedImage {
    int width = 0;
    int height = 0;
    std::vector<std::vector<uint8_t>> frames;  // premultiplied の BGRA、width * height * 4 バイト
    std::vector<double> delays;                // GIF などに書かれた待ち時間（秒）
};

// WIC で読む。GIF は重ね方（disposal）をこちらで当てる。読めなければ false
bool LoadImageFile(IWICImagingFactory* wic, const std::wstring& path, LoadedImage& out);

}  // namespace pixwall
