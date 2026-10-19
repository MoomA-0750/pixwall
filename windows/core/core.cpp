#include "core.h"

#include <algorithm>
#include <cmath>

namespace pixwall {

namespace {

void Scaled(int w, int h, double scale, int& outW, int& outH) {
    outW = std::max(1, static_cast<int>(std::lround(w * scale)));
    outH = std::max(1, static_cast<int>(std::lround(h * scale)));
}

}  // namespace

PixelRect Layout(int imageWidth, int imageHeight, int screenWidth, int screenHeight, Fit fit,
                 double position) {
    if (imageWidth <= 0 || imageHeight <= 0 || screenWidth <= 0 || screenHeight <= 0) return {};
    const double sx = static_cast<double>(screenWidth) / imageWidth;
    const double sy = static_cast<double>(screenHeight) / imageHeight;

    int width = 0;
    int height = 0;
    if (fit == Fit::Integer && std::min(sx, sy) >= 1) {
        const int scale = std::min(screenWidth / imageWidth, screenHeight / imageHeight);
        width = imageWidth * scale;
        height = imageHeight * scale;
    } else if (fit == Fit::Fill) {
        Scaled(imageWidth, imageHeight, std::max(sx, sy), width, height);
    } else {
        Scaled(imageWidth, imageHeight, std::min(sx, sy), width, height);
    }
    if (fit == Fit::Integer) position = 0.5;
    position = std::clamp(position, 0.0, 1.0);
    // 余白×位置を 0 の側へ切り捨てる（0.5 のとき、余白を半分にして切り捨てるのと同じ。はみ出す負の余白でも）
    const auto place = [&](int slack) { return static_cast<int>(std::trunc(slack * position)); };
    return {place(screenWidth - width), place(screenHeight - height), width, height};
}

std::vector<uint8_t> RenderStill(const uint8_t* frame, int imageWidth, int imageHeight,
                                 int canvasWidth, int canvasHeight, Fit fit,
                                 uint8_t r, uint8_t g, uint8_t b, double position) {
    if (canvasWidth <= 0 || canvasHeight <= 0) return {};
    std::vector<uint8_t> out(static_cast<size_t>(canvasWidth) * canvasHeight * 4);
    for (size_t i = 0; i < out.size(); i += 4) {
        out[i] = b;
        out[i + 1] = g;
        out[i + 2] = r;
        out[i + 3] = 255;
    }
    const PixelRect rect = Layout(imageWidth, imageHeight, canvasWidth, canvasHeight, fit, position);
    if (rect.width <= 0 || rect.height <= 0 || !frame) return out;
    const int x0 = std::max(0, rect.x);
    const int y0 = std::max(0, rect.y);
    const int x1 = std::min(canvasWidth, rect.x + rect.width);
    const int y1 = std::min(canvasHeight, rect.y + rect.height);
    for (int y = y0; y < y1; ++y) {
        // 画素の中心が入る元の画素を選ぶ
        const int sy = static_cast<int>((static_cast<long long>(y - rect.y) * 2 + 1) * imageHeight / (rect.height * 2LL));
        const uint8_t* srcRow = frame + static_cast<size_t>(sy) * imageWidth * 4;
        uint8_t* dst = out.data() + (static_cast<size_t>(y) * canvasWidth + x0) * 4;
        for (int x = x0; x < x1; ++x, dst += 4) {
            const int sx = static_cast<int>((static_cast<long long>(x - rect.x) * 2 + 1) * imageWidth / (rect.width * 2LL));
            const uint8_t* src = srcRow + static_cast<size_t>(sx) * 4;
            // premultiplied なので、余白の色を (1 - a) だけ足す
            const int keep = 255 - src[3];
            dst[0] = static_cast<uint8_t>(src[0] + (b * keep + 127) / 255);
            dst[1] = static_cast<uint8_t>(src[1] + (g * keep + 127) / 255);
            dst[2] = static_cast<uint8_t>(src[2] + (r * keep + 127) / 255);
        }
    }
    return out;
}

double FrameDelay(double written, double speed) {
    const double delay = written <= 0.01 ? 0.1 : written;
    return delay / std::clamp(speed, 0.25, 4.0);
}

namespace {

// rect から cover を引いた残りを、重ならない長方形（最大4つ）で out に足す
void Subtract(const PixelRect& cover, const PixelRect& rect, std::vector<PixelRect>& out) {
    const int x0 = std::max(rect.x, cover.x);
    const int y0 = std::max(rect.y, cover.y);
    const int x1 = std::min(rect.x + rect.width, cover.x + cover.width);
    const int y1 = std::min(rect.y + rect.height, cover.y + cover.height);
    if (x0 >= x1 || y0 >= y1) {
        out.push_back(rect);
        return;
    }
    // 上下の帯は幅いっぱい、左右の帯は重なった高さだけ
    if (y0 > rect.y) out.push_back({rect.x, rect.y, rect.width, y0 - rect.y});
    if (y1 < rect.y + rect.height) out.push_back({rect.x, y1, rect.width, rect.y + rect.height - y1});
    if (x0 > rect.x) out.push_back({rect.x, y0, x0 - rect.x, y1 - y0});
    if (x1 < rect.x + rect.width) out.push_back({x1, y0, rect.x + rect.width - x1, y1 - y0});
}

long long Area(const PixelRect& r) { return static_cast<long long>(r.width) * r.height; }

}  // namespace

double UncoveredFraction(const PixelRect& target, const std::vector<PixelRect>& rects) {
    const long long total = Area(target);
    if (target.width <= 0 || target.height <= 0) return 0;
    std::vector<PixelRect> rest{target};
    for (const PixelRect& cover : rects) {
        if (cover.width <= 0 || cover.height <= 0) continue;
        std::vector<PixelRect> next;
        for (const PixelRect& r : rest) Subtract(cover, r, next);
        rest = std::move(next);
        if (rest.empty()) return 0;
    }
    long long left = 0;
    for (const PixelRect& r : rest) left += Area(r);
    return static_cast<double>(left) / static_cast<double>(total);
}

GifCompositor::GifCompositor(int width, int height)
    : width_(width), height_(height), canvas_(static_cast<size_t>(width) * height * 4, 0) {}

std::vector<uint8_t> GifCompositor::Add(const SubFrame& f) {
    // 画面（論理画面）からはみ出す部分は切る
    const int x0 = std::max(0, f.left);
    const int y0 = std::max(0, f.top);
    const int x1 = std::min(width_, f.left + f.width);
    const int y1 = std::min(height_, f.top + f.height);

    std::vector<uint8_t> previous;
    if (f.disposal == Disposal::Previous) previous = canvas_;

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const uint8_t* s = f.pixels + (static_cast<size_t>(y - f.top) * f.width + (x - f.left)) * 4;
            uint8_t* d = canvas_.data() + (static_cast<size_t>(y) * width_ + x) * 4;
            const int a = s[3];
            if (a == 255) {
                std::copy(s, s + 4, d);
            } else if (a > 0) {
                // premultiplied の重ね合わせ: d = s + d * (1 - a)
                for (int i = 0; i < 4; ++i) d[i] = static_cast<uint8_t>(s[i] + d[i] * (255 - a) / 255);
            }
        }
    }

    std::vector<uint8_t> out = canvas_;

    if (f.disposal == Disposal::Background) {
        // ブラウザと同じく、背景色ではなく透明に戻す
        for (int y = y0; y < y1; ++y) {
            std::fill(canvas_.begin() + (static_cast<size_t>(y) * width_ + x0) * 4,
                      canvas_.begin() + (static_cast<size_t>(y) * width_ + x1) * 4, uint8_t{0});
        }
    } else if (f.disposal == Disposal::Previous) {
        canvas_ = std::move(previous);
    }
    return out;
}

}  // namespace pixwall
