#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <vector>

#include "core.h"
#include "desktop.h"
#include "image_loader.h"

namespace pixwall {

// 余白の色（premultiplied ではない RGBA、0〜1）
struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};

// すべての画面の窓で分け合う GPU の道具
struct Gpu {
    Microsoft::WRL::ComPtr<ID3D11Device> d3d;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp;

    bool Create();
    void Reset() { *this = {}; }
};

// 1つの画面の、デスクトップのアイコンの後ろに置く子窓。
// フレームは最初に全部、元の大きさのまま DirectComposition の面にして GPU に置く。
// 絵が変わる瞬間には、見せる面を差し替えて Commit するだけ（Mac の CALayer の contents の差し替えと同じ）。
// 拡大と余白は合成するときに最近傍で行う
class WallWindow {
public:
    WallWindow(Gpu& gpu, const LoadedImage& image, const RECT& monitor, const DesktopParent& desktop,
               Color background, Fit fit, double position);
    ~WallWindow();
    WallWindow(const WallWindow&) = delete;
    WallWindow& operator=(const WallWindow&) = delete;

    bool ok() const { return ok_; }
    // GPU が失われたら false。そのときは Gpu から作り直す
    bool Render(size_t frame);
    // 並べ方・位置・余白の色だけを変える。フレームの面は作り直さない（位置のつまみを動かしている間も軽いように）。
    // GPU が失われたら false
    bool SetLayout(Color background, Fit fit, double position);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool CreateComposition(Color background, Fit fit, double position);
    Microsoft::WRL::ComPtr<IDCompositionSurface> MakeSurface(UINT w, UINT h, DXGI_ALPHA_MODE alpha,
                                                             const uint8_t* bgra);

    Gpu& gpu_;
    const LoadedImage& image_;
    HWND hwnd_ = nullptr;
    bool ok_ = false;
    int width_ = 0;
    int height_ = 0;
    std::vector<Microsoft::WRL::ComPtr<IDCompositionSurface>> frames_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> root_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> backgroundVisual_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> imageVisual_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> backgroundSurface_;
};

}  // namespace pixwall
