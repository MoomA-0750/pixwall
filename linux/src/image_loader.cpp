#include "image_loader.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include "core.h"

namespace pixwall {

namespace {

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool Has(size_t n) const { return pos_ + n <= size_; }
    uint8_t U8() {
        if (Has(1)) return data_[pos_++];
        ok_ = false;
        return 0;
    }
    int U16() {
        const int lo = U8();
        return lo | (U8() << 8);
    }
    void Skip(size_t n) {
        if (Has(n)) {
            pos_ += n;
        } else {
            ok_ = false;
            pos_ = size_;
        }
    }
    const uint8_t* Take(size_t n) {
        if (!Has(n)) {
            ok_ = false;
            pos_ = size_;
            return nullptr;
        }
        const uint8_t* p = data_ + pos_;
        pos_ += n;
        return p;
    }
    // 長さ付きの小さな塊（sub-block）を 0 の塊まで読み、つなげる
    std::vector<uint8_t> Blocks() {
        std::vector<uint8_t> out;
        while (ok_) {
            const int n = U8();
            if (n == 0) break;
            const uint8_t* p = Take(n);
            if (p) out.insert(out.end(), p, p + n);
        }
        return out;
    }
    void SkipBlocks() {
        while (ok_) {
            const int n = U8();
            if (n == 0) break;
            Skip(n);
        }
    }
    bool ok() const { return ok_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool ok_ = true;
};

// GIF の LZW を解き、色の番号を count 個まで返す（足りなければ 0 で埋める）
std::vector<uint8_t> Lzw(const std::vector<uint8_t>& in, int minSize, size_t count) {
    std::vector<uint8_t> out;
    out.reserve(count);
    if (minSize < 1 || minSize > 11) {
        out.resize(count, 0);
        return out;
    }
    const int clear = 1 << minSize;
    const int end = clear + 1;
    std::vector<uint16_t> prefix(4096);
    std::vector<uint8_t> suffix(4096);
    std::vector<uint8_t> stack;
    stack.reserve(4096);
    for (int i = 0; i < clear; ++i) suffix[i] = static_cast<uint8_t>(i);
    int size = minSize + 1;
    int next = clear + 2;
    int previous = -1;
    uint8_t first = 0;
    uint32_t bits = 0;
    int bitCount = 0;
    size_t pos = 0;
    while (out.size() < count) {
        while (bitCount < size && pos < in.size()) {
            bits |= static_cast<uint32_t>(in[pos++]) << bitCount;
            bitCount += 8;
        }
        if (bitCount < size) break;
        int code = static_cast<int>(bits & ((1u << size) - 1));
        bits >>= size;
        bitCount -= size;
        if (code == clear) {
            size = minSize + 1;
            next = clear + 2;
            previous = -1;
            continue;
        }
        if (code == end) break;
        if (previous < 0) {
            if (code >= clear) break;
            out.push_back(static_cast<uint8_t>(code));
            previous = code;
            first = static_cast<uint8_t>(code);
            continue;
        }
        const int in_code = code;
        stack.clear();
        if (code >= next) {
            // まだ表に無い符号は、前の列＋前の列の先頭
            if (code > next) break;
            stack.push_back(first);
            code = previous;
        }
        while (code >= clear) {
            stack.push_back(suffix[code]);
            code = prefix[code];
        }
        first = static_cast<uint8_t>(code);
        stack.push_back(first);
        for (auto it = stack.rbegin(); it != stack.rend() && out.size() < count; ++it) out.push_back(*it);
        if (next < 4096) {
            prefix[next] = static_cast<uint16_t>(previous);
            suffix[next] = first;
            ++next;
            if (next == (1 << size) && size < 12) ++size;
        }
        previous = in_code;
    }
    out.resize(count, 0);
    return out;
}

// 飛び越し（interlace）の行の並びを、上からの順に戻す
std::vector<uint8_t> Deinterlace(const std::vector<uint8_t>& in, int w, int h) {
    std::vector<uint8_t> out(in.size());
    static const int starts[] = {0, 4, 2, 1};
    static const int steps[] = {8, 8, 4, 2};
    int row = 0;
    for (int pass = 0; pass < 4; ++pass) {
        for (int y = starts[pass]; y < h; y += steps[pass], ++row) {
            std::copy_n(in.begin() + static_cast<size_t>(row) * w, w, out.begin() + static_cast<size_t>(y) * w);
        }
    }
    return out;
}

bool LoadWithPixbuf(const std::string& path, LoadedImage& out) {
    GError* error = nullptr;
    GdkPixbuf* raw = gdk_pixbuf_new_from_file(path.c_str(), &error);
    if (!raw) {
        g_clear_error(&error);
        return false;
    }
    // 回転の情報（EXIF）を当て、RGBA にそろえる
    GdkPixbuf* oriented = gdk_pixbuf_apply_embedded_orientation(raw);
    g_object_unref(raw);
    GdkPixbuf* pixbuf = gdk_pixbuf_get_has_alpha(oriented) ? g_object_ref(oriented)
                                                           : gdk_pixbuf_add_alpha(oriented, FALSE, 0, 0, 0);
    g_object_unref(oriented);
    const int w = gdk_pixbuf_get_width(pixbuf);
    const int h = gdk_pixbuf_get_height(pixbuf);
    const int stride = gdk_pixbuf_get_rowstride(pixbuf);
    const uint8_t* src = gdk_pixbuf_read_pixels(pixbuf);
    std::vector<uint8_t> frame(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* s = src + static_cast<size_t>(y) * stride + x * 4;
            uint8_t* d = frame.data() + (static_cast<size_t>(y) * w + x) * 4;
            const int a = s[3];
            d[0] = static_cast<uint8_t>((s[2] * a + 127) / 255);
            d[1] = static_cast<uint8_t>((s[1] * a + 127) / 255);
            d[2] = static_cast<uint8_t>((s[0] * a + 127) / 255);
            d[3] = static_cast<uint8_t>(a);
        }
    }
    g_object_unref(pixbuf);
    out = {};
    out.width = w;
    out.height = h;
    out.frames.push_back(std::move(frame));
    out.delays.push_back(0);
    return w > 0 && h > 0;
}

}  // namespace

bool DecodeGif(const uint8_t* data, size_t size, LoadedImage& out) {
    out = {};
    Reader r(data, size);
    const uint8_t* sig = r.Take(6);
    if (!sig || std::string(reinterpret_cast<const char*>(sig), 3) != "GIF") return false;
    const int width = r.U16();
    const int height = r.U16();
    const int packed = r.U8();
    r.Skip(2);  // 背景色の番号と縦横比（ブラウザと同じく背景色は使わない）
    if (!r.ok() || width <= 0 || height <= 0) return false;
    std::vector<uint8_t> globalTable;
    if (packed & 0x80) {
        const size_t n = 3u << ((packed & 7) + 1);
        if (const uint8_t* p = r.Take(n)) globalTable.assign(p, p + n);
    }
    out.width = width;
    out.height = height;
    GifCompositor compositor(width, height);

    // 次の絵に当てる Graphic Control Extension
    int disposal = 0;
    int delay = 0;
    int transparent = -1;
    while (r.ok()) {
        const int kind = r.U8();
        if (!r.ok() || kind == 0x3B) break;
        if (kind == 0x21) {
            const int label = r.U8();
            if (label == 0xF9) {
                const std::vector<uint8_t> gce = r.Blocks();
                if (gce.size() >= 4) {
                    disposal = (gce[0] >> 2) & 7;
                    delay = gce[1] | (gce[2] << 8);
                    transparent = (gce[0] & 1) ? gce[3] : -1;
                }
            } else {
                r.SkipBlocks();
            }
            continue;
        }
        if (kind != 0x2C) break;
        const int left = r.U16();
        const int top = r.U16();
        const int w = r.U16();
        const int h = r.U16();
        const int flags = r.U8();
        std::vector<uint8_t> table = globalTable;
        if (flags & 0x80) {
            const size_t n = 3u << ((flags & 7) + 1);
            if (const uint8_t* p = r.Take(n)) table.assign(p, p + n);
        }
        const int minSize = r.U8();
        const std::vector<uint8_t> lzw = r.Blocks();
        if (w <= 0 || h <= 0) continue;
        std::vector<uint8_t> indices = Lzw(lzw, minSize, static_cast<size_t>(w) * h);
        if (flags & 0x40) indices = Deinterlace(indices, w, h);

        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4, 0);
        for (size_t i = 0; i < indices.size(); ++i) {
            const int c = indices[i];
            if (c == transparent || static_cast<size_t>(c) * 3 + 2 >= table.size()) continue;
            uint8_t* d = pixels.data() + i * 4;
            d[0] = table[c * 3 + 2];
            d[1] = table[c * 3 + 1];
            d[2] = table[c * 3];
            d[3] = 255;
        }
        GifCompositor::SubFrame sub;
        sub.left = left;
        sub.top = top;
        sub.width = w;
        sub.height = h;
        sub.disposal = static_cast<GifCompositor::Disposal>(disposal > 3 ? 0 : disposal);
        sub.pixels = pixels.data();
        out.frames.push_back(compositor.Add(sub));
        out.delays.push_back(delay / 100.0);
        disposal = 0;
        delay = 0;
        transparent = -1;
    }
    return !out.frames.empty();
}

bool LoadImageFile(const std::string& path, LoadedImage& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char head[6] = {};
    in.read(head, 6);
    if (in.gcount() == 6 && std::string(head, 3) == "GIF") {
        in.seekg(0);
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return DecodeGif(data.data(), data.size(), out);
    }
    return LoadWithPixbuf(path, out);
}

int TopLeftColor(const LoadedImage& image) {
    if (image.frames.empty()) return 0;
    const uint8_t* p = image.frames[0].data();
    if (p[3] == 0) return 0;
    const auto un = [&](int v) { return std::clamp(static_cast<int>(std::lround(v * 255.0 / p[3])), 0, 255); };
    return (un(p[2]) << 16) | (un(p[1]) << 8) | un(p[0]);
}

}  // namespace pixwall
