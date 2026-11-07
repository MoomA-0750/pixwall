// Linux の部分（GIF を読む・config.json の読み書き）のテスト。make test で動かす
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

#include "config_file.h"
#include "image_loader.h"

using namespace pixwall;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

const std::string kFixtures = "tests/fixtures/";

uint64_t Fnv(const std::vector<uint8_t>& data) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t b : data) h = (h ^ b) * 0x100000001b3ull;
    return h;
}

// Pillow が重ねた絵（tests/fixtures/expected.txt。make_expected.py で作る）と、こちらで読んだ絵が同じか
void CompareWithExpected() {
    std::ifstream in(kFixtures + "expected.txt");
    std::string line;
    int files = 0;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string path, topLeft;
        int w = 0, h = 0;
        fields >> path >> w >> h >> topLeft;
        std::vector<std::string> hashes;
        for (std::string x; fields >> x;) hashes.push_back(x);
        ++files;
        LoadedImage image;
        Check(LoadImageFile(path, image), "read " + path);
        Check(image.width == w && image.height == h, "size of " + path);
        Check(image.frames.size() == hashes.size(), "frame count of " + path);
        Check(TopLeftColor(image) == std::stoi(topLeft, nullptr, 16), "auto color of " + path);
        for (size_t f = 0; f < image.frames.size() && f < hashes.size(); ++f) {
            // premultiplied の BGRA を RGBA に（GIF の色は不透明か透明かのどちらか）
            std::vector<uint8_t> rgba(image.frames[f].size());
            for (size_t i = 0; i < rgba.size(); i += 4) {
                rgba[i] = image.frames[f][i + 2];
                rgba[i + 1] = image.frames[f][i + 1];
                rgba[i + 2] = image.frames[f][i];
                rgba[i + 3] = image.frames[f][i + 3];
            }
            char hex[17];
            std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(Fnv(rgba)));
            Check(hashes[f] == hex, path + " frame " + std::to_string(f + 1) + " matches Pillow");
        }
    }
    Check(files >= 3, "expected.txt lists the fixtures");
}

void TestGif() {
    CompareWithExpected();

    LoadedImage image;
    Check(LoadImageFile(kFixtures + "disposal.gif", image), "read disposal.gif");
    Check(image.frames.size() == 4, "disposal.gif has 4 frames");
    // 書かれた待ち時間（1/100 秒）をそのまま返す。10ms 以下を 100ms にするのは FrameDelay
    const double want[] = {0, 0.05, 0.12, 0.01};
    for (size_t i = 0; i < 4 && i < image.delays.size(); ++i) {
        Check(std::abs(image.delays[i] - want[i]) < 1e-9, "delay of frame " + std::to_string(i + 1));
    }

    LoadedImage sample;
    LoadImageFile("../samples/dot-64x64-30f.gif", sample);
    Check(sample.frames.size() == 30 && sample.width == 64 && sample.height == 64, "sample is 64x64, 30 frames");

    // 壊れた・途中で切れた GIF
    const uint8_t junk[] = {'G', 'I', 'F', '8', '9', 'a', 0, 0};
    Check(!DecodeGif(junk, sizeof(junk), image), "a GIF of size 0 is not read");
    gchar* data = nullptr;
    gsize size = 0;
    g_file_get_contents((kFixtures + "disposal.gif").c_str(), &data, &size, nullptr);
    Check(DecodeGif(reinterpret_cast<uint8_t*>(data), size * 2 / 3, image) && !image.frames.empty() &&
              image.frames.size() < 4,
          "a cut GIF keeps the frames before the cut");
    g_free(data);
    Check(!LoadImageFile(kFixtures + "missing.gif", image), "a missing file is not read");
}

void TestPng() {
    // GIF でないものは gdk-pixbuf で1枚目を読み、premultiplied にする
    const std::string dir = g_get_tmp_dir() + std::string("/pixwall-test-png");
    g_mkdir_with_parents(dir.c_str(), 0700);
    GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 3, 2);
    gdk_pixbuf_fill(pb, 0xFF000080);  // 赤、半透明
    const std::string path = dir + "/half.png";
    gdk_pixbuf_save(pb, path.c_str(), "png", nullptr, nullptr);
    g_object_unref(pb);
    LoadedImage image;
    Check(LoadImageFile(path, image), "read a PNG");
    Check(image.width == 3 && image.height == 2 && image.frames.size() == 1, "PNG is one frame of 3x2");
    if (!image.frames.empty()) {
        const uint8_t* p = image.frames[0].data();
        Check(p[3] == 0x80 && p[2] == 0x80 && p[1] == 0 && p[0] == 0, "PNG is premultiplied BGRA");
        Check(TopLeftColor(image) == 0xFF0000, "auto color undoes premultiplication");
    }
    g_remove(path.c_str());
}

void TestConfigFile() {
    const std::string dir = g_get_tmp_dir() + std::string("/pixwall-test-config");
    g_setenv("XDG_CONFIG_HOME", dir.c_str(), TRUE);
    // g_get_user_config_dir は最初の値を覚えるので、ConfigPath の形だけ確かめ、読み書きは別の場所で
    Check(ConfigPath().size() > 20 && ConfigPath().substr(ConfigPath().size() - 20) == "/pixwall/config.json",
          "config path ends with pixwall/config.json");
    const std::string path = dir + "/nested/pixwall/config.json";
    Config c;
    c.image = "/tmp/a.gif";
    c.speed = 2;
    c.SwitchTo(SwitchBy::Time);
    Check(SaveConfig(path, c), "save makes the folders");
    Check(LoadConfig(path) == c, "save and load give the same settings");
    Check(!g_file_test((path + ".tmp").c_str(), G_FILE_TEST_EXISTS), "no temp file is left");
    Check(LoadConfig(dir + "/none.json") == Config{}, "a missing file gives the defaults");
    g_remove(path.c_str());

    Check(AbsolutePath("/a/b") == "/a/b", "absolute path is kept");
    gchar* cwd = g_get_current_dir();
    Check(AbsolutePath("x/../y.gif") == std::string(cwd) + "/y.gif", "relative path is made absolute");
    g_free(cwd);
}

}  // namespace

int main() {
    TestGif();
    TestPng();
    TestConfigFile();
    if (failures) {
        std::fprintf(stderr, "%d failed\n", failures);
        return 1;
    }
    std::printf("linux_tests: all passed\n");
    return 0;
}
