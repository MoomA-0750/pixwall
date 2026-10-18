// core の単体テスト。Windows では CMake の pixwall-core-tests、Linux では
//   g++ -std=c++20 -I windows/core windows/core/core.cpp windows/tests/core_tests.cpp -o core_tests
// でビルドして動かす。失敗があれば 1 で終わる
#include <cmath>
#include <cstdio>

#include "core.h"

using namespace pixwall;

namespace {

int failures = 0;

void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("失敗 %d行: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

void TestLayout() {
    // 1920×1080 に 64×64 → 16倍
    CHECK((Layout(64, 64, 1920, 1080, Fit::Integer) == PixelRect{448, 28, 1024, 1024}));
    // MacBook Air M1 の既定（2880×1800）でも Mac 版と同じ結果
    CHECK((Layout(64, 64, 2880, 1800, Fit::Integer) == PixelRect{544, 4, 1792, 1792}));
    CHECK((Layout(4000, 2000, 2000, 2000, Fit::Integer) == PixelRect{0, 500, 2000, 1000}));
    CHECK((Layout(320, 180, 2880, 1800, Fit::Fit) == PixelRect{0, 90, 2880, 1620}));
    CHECK((Layout(320, 180, 2880, 1800, Fit::Fill) == PixelRect{-160, 0, 3200, 1800}));
    CHECK((Layout(0, 64, 1920, 1080, Fit::Integer) == PixelRect{}));
}

void TestTiming() {
    CHECK(Near(FrameDelay(0), 0.1));
    CHECK(Near(FrameDelay(0.01), 0.1));
    CHECK(Near(FrameDelay(0.02), 0.02));
    CHECK(Near(FrameDelay(0.1, 2), 0.05));
    CHECK(Near(FrameDelay(0.1, 100), 0.025));
    CHECK(Near(FrameDelay(0.1, 0), 0.4));

    PauseReasons p;
    CHECK(!p.ShouldPause());
    p.energySaver = true;
    CHECK(p.ShouldPause());
    p.pauseOnEnergySaver = false;
    CHECK(!p.ShouldPause());
    p.locked = true;
    CHECK(p.ShouldPause());
    p.locked = false;
    p.userPaused = true;
    CHECK(p.ShouldPause());
}

const uint8_t kRed[4] = {0, 0, 255, 255};   // BGRA
const uint8_t kBlue[4] = {255, 0, 0, 255};
const uint8_t kClear[4] = {0, 0, 0, 0};

bool PixelIs(const std::vector<uint8_t>& img, int width, int x, int y, const uint8_t* bgra) {
    const uint8_t* p = img.data() + (static_cast<size_t>(y) * width + x) * 4;
    return p[0] == bgra[0] && p[1] == bgra[1] && p[2] == bgra[2] && p[3] == bgra[3];
}

void TestCompositor() {
    // 2×1 の画面。1枚目で全体を赤に
    const uint8_t full[8] = {0, 0, 255, 255, 0, 0, 255, 255};
    const uint8_t blue1[4] = {255, 0, 0, 255};
    const uint8_t clear1[4] = {0, 0, 0, 0};

    {
        // Keep: 前の絵の上に重なる。透明のドットは下が見える
        GifCompositor c(2, 1);
        c.Add({0, 0, 2, 1, GifCompositor::Disposal::Keep, full});
        auto f2 = c.Add({1, 0, 1, 1, GifCompositor::Disposal::Keep, blue1});
        CHECK(PixelIs(f2, 2, 0, 0, kRed));
        CHECK(PixelIs(f2, 2, 1, 0, kBlue));
        auto f3 = c.Add({1, 0, 1, 1, GifCompositor::Disposal::Keep, clear1});
        CHECK(PixelIs(f3, 2, 1, 0, kBlue));
    }
    {
        // Background: 描いた後、その部分を透明に戻す
        GifCompositor c(2, 1);
        auto f1 = c.Add({0, 0, 2, 1, GifCompositor::Disposal::Background, full});
        CHECK(PixelIs(f1, 2, 0, 0, kRed));
        auto f2 = c.Add({1, 0, 1, 1, GifCompositor::Disposal::Keep, blue1});
        CHECK(PixelIs(f2, 2, 0, 0, kClear));
        CHECK(PixelIs(f2, 2, 1, 0, kBlue));
    }
    {
        // Previous: 描く前の絵に戻す
        GifCompositor c(2, 1);
        c.Add({0, 0, 2, 1, GifCompositor::Disposal::Keep, full});
        auto f2 = c.Add({1, 0, 1, 1, GifCompositor::Disposal::Previous, blue1});
        CHECK(PixelIs(f2, 2, 1, 0, kBlue));
        auto f3 = c.Add({0, 0, 1, 1, GifCompositor::Disposal::Keep, clear1});
        CHECK(PixelIs(f3, 2, 1, 0, kRed));
    }
    {
        // 画面からはみ出すフレームは切って重ねる
        GifCompositor c(2, 1);
        auto f = c.Add({1, 0, 2, 1, GifCompositor::Disposal::Keep, full});
        CHECK(PixelIs(f, 2, 0, 0, kClear));
        CHECK(PixelIs(f, 2, 1, 0, kRed));
    }
}

}  // namespace

int main() {
    TestLayout();
    TestTiming();
    TestCompositor();
    if (failures == 0) std::printf("すべて通った\n");
    return failures == 0 ? 0 : 1;
}
