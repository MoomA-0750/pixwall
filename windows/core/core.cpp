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

PixelRect Layout(int imageWidth, int imageHeight, int screenWidth, int screenHeight, Fit fit) {
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
    return {(screenWidth - width) / 2, (screenHeight - height) / 2, width, height};
}

double FrameDelay(double written, double speed) {
    const double delay = written <= 0.01 ? 0.1 : written;
    return delay / std::clamp(speed, 0.25, 4.0);
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
