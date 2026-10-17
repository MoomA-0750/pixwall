#pragma once

#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <vector>

#include "core.h"
#include "desktop.h"
#include "image_loader.h"

namespace pixwall {

// 1つの画面の、デスクトップのアイコンの後ろに置く子窓。絵が変わる瞬間だけ描く
class WallWindow {
public:
    WallWindow(ID2D1Factory* d2d, const LoadedImage& image, const RECT& monitor, const DesktopParent& desktop,
               D2D1_COLOR_F background, Fit fit);
    ~WallWindow();
    WallWindow(const WallWindow&) = delete;
    WallWindow& operator=(const WallWindow&) = delete;

    bool ok() const { return hwnd_ != nullptr; }
    void Render(size_t frame);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool EnsureTarget();

    ID2D1Factory* d2d_;
    const LoadedImage& image_;
    D2D1_COLOR_F background_;
    HWND hwnd_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    PixelRect dest_;
    size_t frame_ = 0;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    std::vector<Microsoft::WRL::ComPtr<ID2D1Bitmap>> bitmaps_;
};

}  // namespace pixwall
