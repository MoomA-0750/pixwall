#include "wall_window.h"

#include <cmath>

using Microsoft::WRL::ComPtr;

namespace pixwall {

namespace {

const wchar_t kClassName[] = L"pixwall-wall";

void RegisterClassOnce(HINSTANCE inst, WNDPROC proc) {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    done = true;
}

uint8_t ToByte(float v) { return static_cast<uint8_t>(std::lround(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255)); }

// 拡大して位置をずらす変換
D2D_MATRIX_3X2_F ScaleThenMove(float sx, float sy, float x, float y) {
    D2D_MATRIX_3X2_F m{};
    m._11 = sx;
    m._22 = sy;
    m._31 = x;
    m._32 = y;
    return m;
}

}  // namespace

bool Gpu::Create() {
    // BGRA を使うので BGRA_SUPPORT。GPU が無い・使えないときは WARP（CPU で描く）に落とす。
    // 1 つのスレッドからしか使わないので SINGLETHREADED。ドライバーが裏のスレッドで仕事を回すのも止める
    // （to-fu の NVIDIA では、止めないと裏のスレッドが pixwall 自身の 2 倍以上のサイクルを使った）
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED |
                       D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                   &d3d, nullptr, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &d3d,
                               nullptr, &context);
    }
    if (FAILED(hr)) return false;
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3d.As(&dxgiDevice))) return false;
    return SUCCEEDED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcomp)));
}

WallWindow::WallWindow(Gpu& gpu, const LoadedImage& image, const RECT& monitor, const DesktopParent& desktop,
                       Color background, Fit fit)
    : gpu_(gpu), image_(image) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    RegisterClassOnce(inst, WndProc);

    // 画面全体（タスクバーの下も含む）を、親の座標に直して覆う。Per-Monitor V2 なので物理ピクセル
    RECT rc = monitor;
    MapWindowPoints(HWND_DESKTOP, desktop.parent, reinterpret_cast<POINT*>(&rc), 2);
    width_ = rc.right - rc.left;
    height_ = rc.bottom - rc.top;
    dest_ = Layout(image.width, image.height, width_, height_, fit);

    // 絵は DirectComposition で渡すので、GDI の面は持たない（WS_EX_NOREDIRECTIONBITMAP）。
    // 24H2 の Progman も同じく面を持たないので、ふつうの子の窓に GDI や HwndRenderTarget で描いても画面に出ない
    hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, kClassName, L"pixwall",
                            WS_CHILD | WS_CLIPSIBLINGS, rc.left, rc.top, width_, height_, desktop.parent, nullptr,
                            inst, nullptr);
    if (!hwnd_) return;
    if (!CreateComposition(background)) return;
    SetWindowPos(hwnd_, desktop.insertAfter, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ok_ = true;
}

WallWindow::~WallWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
}

// DirectComposition の面を作って、画素をそのまま書き込む
ComPtr<IDCompositionSurface> WallWindow::MakeSurface(UINT w, UINT h, DXGI_ALPHA_MODE alpha, const uint8_t* bgra) {
    ComPtr<IDCompositionSurface> surface;
    if (FAILED(gpu_.dcomp->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, alpha, &surface))) return nullptr;
    ComPtr<ID3D11Texture2D> tex;
    POINT offset{};
    // 面は大きなテクスチャの一部に置かれることがあるので、offset の位置に書く
    if (FAILED(surface->BeginDraw(nullptr, IID_PPV_ARGS(&tex), &offset))) return nullptr;
    const D3D11_BOX box{static_cast<UINT>(offset.x), static_cast<UINT>(offset.y), 0,
                        static_cast<UINT>(offset.x) + w, static_cast<UINT>(offset.y) + h, 1};
    gpu_.context->UpdateSubresource(tex.Get(), 0, &box, bgra, w * 4, w * h * 4);
    surface->EndDraw();
    return surface;
}

bool WallWindow::CreateComposition(Color background) {
    if (!gpu_.dcomp || image_.frames.empty()) return false;
    const UINT w = static_cast<UINT>(image_.width);
    const UINT h = static_cast<UINT>(image_.height);

    // フレームは元の大きさのまま面にする。透明のドットは下の余白の色が見えるよう premultiplied
    for (const auto& pixels : image_.frames) {
        auto surface = MakeSurface(w, h, DXGI_ALPHA_MODE_PREMULTIPLIED, pixels.data());
        if (!surface) return false;
        frames_.push_back(surface);
    }

    // 余白は 1×1 の面を画面いっぱいに広げる
    const uint8_t bgra[4] = {ToByte(background.b), ToByte(background.g), ToByte(background.r), 255};
    backgroundSurface_ = MakeSurface(1, 1, DXGI_ALPHA_MODE_IGNORE, bgra);
    if (!backgroundSurface_) return false;

    auto& dc = *gpu_.dcomp.Get();
    if (FAILED(dc.CreateTargetForHwnd(hwnd_, TRUE, &target_)) || FAILED(dc.CreateVisual(&root_)) ||
        FAILED(dc.CreateVisual(&backgroundVisual_)) || FAILED(dc.CreateVisual(&imageVisual_))) {
        return false;
    }
    backgroundVisual_->SetContent(backgroundSurface_.Get());
    backgroundVisual_->SetTransform(ScaleThenMove(static_cast<float>(width_), static_cast<float>(height_), 0, 0));
    imageVisual_->SetTransform(ScaleThenMove(static_cast<float>(dest_.width) / w, static_cast<float>(dest_.height) / h,
                                             static_cast<float>(dest_.x), static_cast<float>(dest_.y)));
    // ドットをぼかさない。端もにじませない
    for (auto* v : {backgroundVisual_.Get(), imageVisual_.Get()}) {
        v->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        v->SetBorderMode(DCOMPOSITION_BORDER_MODE_HARD);
    }
    root_->AddVisual(backgroundVisual_.Get(), FALSE, nullptr);
    root_->AddVisual(imageVisual_.Get(), TRUE, backgroundVisual_.Get());
    target_->SetRoot(root_.Get());
    return Render(0);
}

bool WallWindow::Render(size_t frame) {
    if (frame >= frames_.size()) return true;
    imageVisual_->SetContent(frames_[frame].Get());
    // 合成は DWM が次の書き換えに合わせて行う。待たない
    return SUCCEEDED(gpu_.dcomp->Commit()) && gpu_.d3d->GetDeviceRemovedReason() == S_OK;
}

LRESULT CALLBACK WallWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            // 絵は DirectComposition が出すので、ここでは描かない
            ValidateRect(hwnd, nullptr);
            return 0;
        case WM_NCHITTEST:
            // マウスは素通しする
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

}  // namespace pixwall
