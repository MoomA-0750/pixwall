#include "still.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include <cmath>
#include <cstdio>

namespace pixwall {

bool WritePng(const std::string& path, const std::vector<uint8_t>& bgra, int width, int height) {
    if (width <= 0 || height <= 0 || bgra.size() < static_cast<size_t>(width) * height * 4) return false;
    GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
    if (!pixbuf) return false;
    const int stride = gdk_pixbuf_get_rowstride(pixbuf);
    uint8_t* dst = gdk_pixbuf_get_pixels(pixbuf);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t* s = bgra.data() + (static_cast<size_t>(y) * width + x) * 4;
            uint8_t* d = dst + static_cast<size_t>(y) * stride + x * 3;
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
        }
    }
    const std::string temp = path + ".tmp";
    const bool saved = gdk_pixbuf_save(pixbuf, temp.c_str(), "png", nullptr, nullptr);
    g_object_unref(pixbuf);
    if (!saved || std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

namespace {

std::string Stem(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

}  // namespace

bool ExportStill(const LoadedImage& image, const std::string& imagePath, size_t frame, const Placement& placement,
                 int rgb, const std::vector<ScreenSize>& sizes, const std::string& path) {
    if (image.frames.empty() || path.empty() || sizes.empty()) return false;
    const auto& pixels = image.frames[frame < image.frames.size() ? frame : 0];
    const std::string dir = path.substr(0, path.find_last_of('/') + 1);
    bool ok = true;
    for (size_t i = 0; i < sizes.size(); ++i) {
        std::string out = path;
        if (i > 0) {
            const std::string base =
                dir + Stem(imagePath) + "-" + std::to_string(sizes[i].width) + "x" + std::to_string(sizes[i].height);
            out = base + ".png";
            for (int n = 2; g_file_test(out.c_str(), G_FILE_TEST_EXISTS); ++n) {
                out = base + "-" + std::to_string(n) + ".png";
            }
        }
        const auto still = RenderStill(pixels.data(), image.width, image.height, sizes[i].width, sizes[i].height,
                                       placement, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        ok = WritePng(out, still, sizes[i].width, sizes[i].height) && ok;
    }
    return ok;
}

}  // namespace pixwall
