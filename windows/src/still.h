#pragma once

#include <windows.h>
#include <wincodec.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core.h"
#include "image_loader.h"

namespace pixwall {

// 不透明な BGRA（width * height * 4 バイト）を PNG に書く。書きかけで壊れないよう、別のファイルに書いてから入れ替える
bool WritePng(IWICImagingFactory* wic, const std::wstring& path, const std::vector<uint8_t>& bgra, int width,
              int height);

// 画面ごとの物理ピクセルの大きさ。一番目が主の画面（左上が 0, 0 の画面）
std::vector<SIZE> MonitorSizes();

// 「静止画を書き出す」。frame 枚目を、画面ごとにその画面の大きさで描く。主の画面は path に、
// ほかの画面は同じフォルダに <画像の名前>-<幅>x<高さ>.png（あれば -2, -3…）で書く
bool ExportStill(IWICImagingFactory* wic, const LoadedImage& image, const std::wstring& imagePath, size_t frame,
                 const Placement& placement, uint8_t r, uint8_t g, uint8_t b, const std::wstring& path);

// OS の壁紙（IDesktopWallpaper）に、動かしている画像の1枚目を入れる。
// 入れる前の壁紙は画面ごとに %LOCALAPPDATA%\pixwall\desktop\original.txt に覚え、Restore で戻す
class OsWallpaper {
public:
    // 画面ごとに1枚目を描いて入れる。画像・並べ方・位置・余白の色・画面の大きさが前と同じなら何もしない
    void Apply(IWICImagingFactory* wic, const LoadedImage& image, const std::wstring& imagePath,
               const Placement& placement, uint8_t r, uint8_t g, uint8_t b);
    // 入れる前の壁紙に戻す。本人がその間に壁紙を替えていた画面はそのままにする
    void Restore();

private:
    void LoadOriginals();
    void SaveOriginals() const;

    bool loaded_ = false;
    std::map<std::wstring, std::wstring> originals_;  // 画面の ID → 入れる前の壁紙のパス
    int originalPosition_ = -1;                       // 入れる前の DESKTOP_WALLPAPER_POSITION
    std::wstring applied_;                            // 最後に入れたときの中身の印
};

}  // namespace pixwall
