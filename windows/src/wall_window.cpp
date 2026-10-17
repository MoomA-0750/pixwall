#include "wall_window.h"

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

}  // namespace

WallWindow::WallWindow(ID2D1Factory* d2d, const LoadedImage& image, const RECT& monitor,
                       const DesktopParent& desktop, D2D1_COLOR_F background, Fit fit)
    : d2d_(d2d), image_(image), background_(background) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    RegisterClassOnce(inst, WndProc);

    // 画面全体（タスクバーの下も含む）を、親の座標に直して覆う。Per-Monitor V2 なので物理ピクセル
    RECT rc = monitor;
    MapWindowPoints(HWND_DESKTOP, desktop.parent, reinterpret_cast<POINT*>(&rc), 2);
    width_ = rc.right - rc.left;
    height_ = rc.bottom - rc.top;
    dest_ = Layout(image.width, image.height, width_, height_, fit);

    hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClassName, L"pixwall",
                            WS_CHILD | WS_CLIPSIBLINGS, rc.left, rc.top, width_, height_, desktop.parent, nullptr,
                            inst, this);
    if (!hwnd_) return;
    SetWindowPos(hwnd_, desktop.insertAfter, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

WallWindow::~WallWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

bool WallWindow::EnsureTarget() {
    if (target_) return true;
    // DPI を 96 にして、Direct2D の単位を物理ピクセルにそろえる
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96,
        96);
    const auto hwndProps =
        D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(width_, height_), D2D1_PRESENT_OPTIONS_NONE);
    if (FAILED(d2d_->CreateHwndRenderTarget(props, hwndProps, &target_))) return false;

    // フレームは元の大きさのまま GPU に置く。拡大は描くときに GPU に任せる
    const auto bmpProps =
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    bitmaps_.clear();
    for (const auto& pixels : image_.frames) {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bmp;
        target_->CreateBitmap(D2D1::SizeU(image_.width, image_.height), pixels.data(), image_.width * 4, bmpProps,
                              &bmp);
        bitmaps_.push_back(bmp);
    }
    return true;
}

void WallWindow::Render(size_t frame) {
    frame_ = frame;
    if (!hwnd_ || !EnsureTarget()) return;
    target_->BeginDraw();
    target_->Clear(background_);
    if (frame < bitmaps_.size() && bitmaps_[frame]) {
        const auto rect = D2D1::RectF(static_cast<float>(dest_.x), static_cast<float>(dest_.y),
                                      static_cast<float>(dest_.x + dest_.width),
                                      static_cast<float>(dest_.y + dest_.height));
        target_->DrawBitmap(bitmaps_[frame].Get(), rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    }
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {
        // GPU が入れ替わったなど。次に描くときに作り直す
        bitmaps_.clear();
        target_.Reset();
    }
}

LRESULT CALLBACK WallWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto self = reinterpret_cast<WallWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            if (self) self->Render(self->frame_);
            return 0;
        }
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
