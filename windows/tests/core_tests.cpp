// core の単体テスト。Windows では CMake の pixwall-core-tests、Linux では
//   g++ -std=c++20 -I windows/core windows/core/core.cpp windows/core/config.cpp windows/core/cli.cpp windows/tests/core_tests.cpp -o core_tests
// でビルドして動かす。失敗があれば 1 で終わる
#include <cmath>
#include <cstdio>

#include "cli.h"
#include "config.h"
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
    // 画面に合わせるときは、余白の中の位置を選べる（上下に余白・左右に余白のどちらでも）
    CHECK((Layout(320, 180, 2880, 1800, Fit::Fit, 0) == PixelRect{0, 0, 2880, 1620}));
    CHECK((Layout(320, 180, 2880, 1800, Fit::Fit, 1) == PixelRect{0, 180, 2880, 1620}));
    CHECK((Layout(320, 180, 2880, 1800, Fit::Fit, 0.25) == PixelRect{0, 45, 2880, 1620}));
    CHECK((Layout(100, 200, 1000, 1000, Fit::Fit, 1) == PixelRect{500, 0, 500, 1000}));
    CHECK((Layout(320, 180, 2880, 1800, Fit::Fit, 5) == PixelRect{0, 180, 2880, 1620}));
    // ほかの並べ方では中央のまま
    CHECK((Layout(64, 64, 1920, 1080, Fit::Integer, 0) == PixelRect{448, 28, 1024, 1024}));
    // 画面を埋めるときは、はみ出す向きに動く。0 で左の端、1 で右の端がそろう
    CHECK((Layout(320, 180, 2880, 1800, Fit::Fill, 0) == PixelRect{0, 0, 3200, 1800}));
    CHECK((Layout(320, 180, 2880, 1800, Fit::Fill, 1) == PixelRect{-320, 0, 3200, 1800}));
    CHECK((Layout(100, 200, 1000, 1000, Fit::Fill, 0.25) == PixelRect{0, -250, 1000, 2000}));
    // 中央は前と同じ（はみ出しが奇数でも）
    CHECK((Layout(3, 1, 4, 3, Fit::Fill) == PixelRect{-2, 0, 9, 3}));

    // 自由に置く: 画面に合わせた大きさ×freeSize。横・縦は別々に、余白（はみ出し）の中の割合で置く（Mac の LayoutTests と同じ）
    const auto free = [](double x, double y, double size) {
        Placement p(Fit::Free, 0);
        p.freeX = x;
        p.freeY = y;
        p.freeSize = size;
        return Layout(320, 180, 2880, 1800, p);
    };
    CHECK((free(0.5, 0.5, 1) == PixelRect{0, 90, 2880, 1620}));
    CHECK((free(0, 1, 0.5) == PixelRect{0, 990, 1440, 810}));
    CHECK((free(1, 0, 0.5) == PixelRect{1440, 0, 1440, 810}));
    CHECK((free(0, 0, 2) == PixelRect{0, 0, 5760, 3240}));
    CHECK((free(1, 1, 2) == PixelRect{-2880, -1440, 5760, 3240}));
    CHECK((free(-1, 2, 100) == free(0, 1, 3)));
    CHECK((free(0.5, 0.5, 0) == free(0.5, 0.5, 0.1)));
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

void TestCoverage() {
    // to-fu（3440×1440）のタスクバーを除いた部分
    const PixelRect work{0, 0, 3440, 1392};
    CHECK(IsCovered(work, {work}));
    CHECK(IsCovered(work, {{-8, -8, 3456, 1408}}));
    CHECK(!IsCovered(work, {}));
    CHECK(Near(UncoveredFraction(work, {}), 1));
    // 左半分だけ
    CHECK(Near(UncoveredFraction(work, {{0, 0, 1720, 1392}}), 0.5));
    CHECK(!IsCovered(work, {{0, 0, 1720, 1392}}));
    // 左右に並べて、間に 8 画素のすき間
    CHECK(IsCovered(work, {{0, 0, 1716, 1392}, {1724, 0, 1716, 1392}}));
    // 右に 200 画素の壁紙が見えている（約 5.8%）
    CHECK(!IsCovered(work, {{0, 0, 3240, 1392}}));
    // 重なった窓を2回数えない
    CHECK(Near(UncoveredFraction(work, {{0, 0, 2000, 1392}, {1000, 0, 2000, 1392}}), 440.0 / 3440));
    // 別の画面の窓は数えない
    CHECK(Near(UncoveredFraction(work, {{3440, 0, 1920, 1080}}), 1));
    CHECK(Near(UncoveredFraction({0, 0, 0, 0}, {}), 0));
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

void TestConfig() {
    // 無い・壊れているときは既定の設定
    CHECK(ParseConfig("") == Config{});
    CHECK(ParseConfig("not json") == Config{});
    CHECK(ParseConfig("{}") == Config{});
    // 先頭に BOM があっても読める
    CHECK(ParseConfig("\xEF\xBB\xBF{\"fit\": \"fill\"}").fit == Fit::Fill);

    // Mac 版が書いた形をそのまま読める
    const Config mac = ParseConfig(R"({
  "fit" : "fill",
  "image" : "/Users/a/Library/Application Support/pixwall/dot.gif",
  "launchAtLogin" : false,
  "pauseOnLowPower" : true,
  "speed" : 2,
  "version" : 1
})");
    CHECK(mac.fit == Fit::Fill);
    CHECK(mac.image == "/Users/a/Library/Application Support/pixwall/dot.gif");
    CHECK(!mac.launchAtLogin);
    CHECK(Near(mac.speed, 2));

    // 読めない値・知らない項目は既定の値のまま。速さは 0.25〜4 に収める
    const Config odd = ParseConfig(R"({"fit": "tile?", "speed": 100, "extra": {"a": [1, "}"]}, "image": "C:\\x\\y.gif"})");
    CHECK(odd.fit == Fit::Integer);
    CHECK(Near(odd.speed, 4));
    CHECK(odd.image == "C:\\x\\y.gif");
    // 前の版の設定には launchAtLogin が無い。既定のオン
    CHECK(odd.launchAtLogin);

    // 日本語のパス（\u の書き方と、そのままの UTF-8 の両方）
    CHECK(ParseConfig(R"({"image": "C:\\\u58c1\u7d19.gif"})").image == "C:\\\xE5\xA3\x81\xE7\xB4\x99.gif");

    // 書いて読むと同じ
    Config c;
    c.image = "C:\\Users\\a b\\\"q\".gif";
    c.fit = Fit::Fit;
    c.speed = 0.5;
    c.pauseOnLowPower = false;
    c.launchAtLogin = false;
    c.position = 0.25;
    c.customBackground = true;
    c.backgroundColor = 0x1E2A3B;
    c.freeX = 0.1;
    c.freeY = 0.9;
    c.freeSize = 2.5;
    CHECK(ParseConfig(FormatConfig(c)) == c);
    c.fit = Fit::Free;
    CHECK(ParseConfig(FormatConfig(c)) == c);
    CHECK(FormatConfig(c).find("\"fit\" : \"free\"") != std::string::npos);
    // 自由に置くときの値は範囲に収める。読めない値は既定のまま
    const Config freeOdd = ParseConfig(R"({"freeX": -1, "freeY": "a", "freeSize": 9})");
    CHECK(Near(freeOdd.freeX, 0) && Near(freeOdd.freeY, 0.5) && Near(freeOdd.freeSize, 3));
    CHECK(FormatConfig(c).find("\"backgroundColor\" : \"#1E2A3B\"") != std::string::npos);
    // 余白の色。前の版の設定には無く、既定は自動。読めない色は選んでいないのと同じ。小文字も読む
    CHECK(!mac.customBackground && mac.backgroundColor == -1);
    const Config color = ParseConfig(R"({"background": "custom", "backgroundColor": "#a0b0c0"})");
    CHECK(color.customBackground && color.backgroundColor == 0xA0B0C0);
    CHECK(ParseConfig(R"({"background": "rainbow", "backgroundColor": "#12345"})") == Config{});
    CHECK(ParseHexColor("#GG0000") == -1 && ParseHexColor("FF0000") == -1 && ParseHexColor("#FF0000") == 0xFF0000);
    // 位置は 0〜1 に収める。前の版の設定には無く、既定の中央
    CHECK(Near(ParseConfig(R"({"position": -3})").position, 0));
    CHECK(Near(mac.position, 0.5));
    CHECK(FormatConfig(Config{}).find("\"fit\" : \"integer\"") != std::string::npos);
}

// 1 画素の BGRA を比べる
bool PixelIs(const std::vector<uint8_t>& img, int width, int x, int y, uint8_t b, uint8_t g, uint8_t r) {
    const uint8_t* p = img.data() + (static_cast<size_t>(y) * width + x) * 4;
    return p[0] == b && p[1] == g && p[2] == r && p[3] == 255;
}

void TestStill() {
    // 赤と、半分透けた青（premultiplied で B=128, A=128）の 2×1
    const uint8_t frame[8] = {0, 0, 255, 255, 128, 0, 0, 128};
    // 6×3 に整数倍 → 3 倍でちょうど埋まる
    auto img = RenderStill(frame, 2, 1, 6, 3, Fit::Integer, 0, 255, 0);
    CHECK(img.size() == 6 * 3 * 4);
    CHECK(PixelIs(img, 6, 0, 0, 0, 0, 255));
    CHECK(PixelIs(img, 6, 2, 2, 0, 0, 255));
    // 透けたところは余白の色（緑）が半分見える
    CHECK(PixelIs(img, 6, 3, 0, 128, 127, 0));
    CHECK(PixelIs(img, 6, 5, 2, 128, 127, 0));

    // 8×3 に整数倍 → 3 倍で 6×3、左右 1 画素ずつ余白
    img = RenderStill(frame, 2, 1, 8, 3, Fit::Integer, 10, 20, 30);
    CHECK(PixelIs(img, 8, 0, 1, 30, 20, 10));
    CHECK(PixelIs(img, 8, 1, 1, 0, 0, 255));
    CHECK(PixelIs(img, 8, 3, 1, 0, 0, 255));
    CHECK(PixelIs(img, 8, 4, 1, 128 + 15, 10, 5));
    CHECK(PixelIs(img, 8, 7, 1, 30, 20, 10));

    // 画面を埋める（はみ出す）: 2×1 を 3×3 に → 6×3 で左右が切れる。真ん中の列は赤と青の境目の右
    img = RenderStill(frame, 2, 1, 3, 3, Fit::Fill, 0, 0, 0);
    CHECK(PixelIs(img, 3, 0, 0, 0, 0, 255));
    CHECK(PixelIs(img, 3, 2, 0, 128, 0, 0));

    // 画像が無ければ余白の色だけ
    img = RenderStill(nullptr, 0, 0, 2, 2, Fit::Integer, 1, 2, 3);
    CHECK(PixelIs(img, 2, 1, 1, 3, 2, 1));
    CHECK(RenderStill(frame, 2, 1, 0, 3, Fit::Integer, 0, 0, 0).empty());
}

bool Parses(std::vector<std::string> args, CliCommand& c) {
    std::string error;
    return ParseCli(args, c, error);
}

void TestCli() {
    CliCommand c;
    CHECK(Parses({"show"}, c) && c.action == CliAction::Show);
    CHECK(Parses({"status"}, c) && c.action == CliAction::Status);
    CHECK(Parses({"pause"}, c) && c.action == CliAction::Pause);
    CHECK(Parses({"resume"}, c) && c.action == CliAction::Resume);
    CHECK(Parses({"refresh"}, c) && c.action == CliAction::Refresh);
    CHECK(Parses({"quit"}, c) && c.action == CliAction::Quit);
    CHECK(Parses({"--help"}, c) && c.action == CliAction::Help);
    CHECK(Parses({"get", "fit"}, c) && c.action == CliAction::Get && c.pairs.size() == 1 && c.pairs[0].first == "fit");
    CHECK(Parses({"set", "fit", "fill", "speed", "2"}, c) && c.action == CliAction::Set && c.pairs.size() == 2 &&
          c.pairs[1].first == "speed" && c.pairs[1].second == "2");
    CHECK(Parses({"export", "a.png"}, c) && c.action == CliAction::Export && c.path == "a.png" && c.frame == 0);
    CHECK(Parses({"export", "--frame", "3", "a.png"}, c) && c.path == "a.png" && c.frame == 2);

    const std::vector<std::vector<std::string>> wrong = {
        {}, {"nope"}, {"show", "x"}, {"get"}, {"get", "nope"}, {"set"}, {"set", "fit"}, {"set", "nope", "1"},
        {"export"}, {"export", "a.png", "b.png"}, {"export", "a.png", "--frame", "0"}, {"export", "a.png", "--frame"},
    };
    for (const auto& args : wrong) CHECK(!Parses(args, c));

    Config config;
    CHECK(ConfigValue(config, "image").empty());
    CHECK(ConfigValue(config, "fit") == "integer");
    CHECK(ConfigValue(config, "position") == "0.5");
    CHECK(ConfigValue(config, "freeX") == "0.5");
    CHECK(ConfigValue(config, "freeSize") == "1");
    CHECK(ConfigValue(config, "speed") == "1");
    CHECK(ConfigValue(config, "background") == "auto");
    CHECK(ConfigValue(config, "backgroundColor").empty());
    CHECK(ConfigValue(config, "pauseOnLowPower") == "true");

    std::string error;
    CHECK(SetConfigValue(config, "fit", "fill", error));
    CHECK(SetConfigValue(config, "position", "0.25", error));
    CHECK(SetConfigValue(config, "speed", "4", error));
    CHECK(SetConfigValue(config, "background", "custom", error));
    CHECK(SetConfigValue(config, "backgroundColor", "#1e2a3b", error));
    CHECK(SetConfigValue(config, "pauseOnLowPower", "false", error));
    CHECK(SetConfigValue(config, "launchAtLogin", "false", error));
    CHECK(SetConfigValue(config, "image", "C:\\a\\b.gif", error));
    CHECK(config.fit == Fit::Fill && Near(config.position, 0.25) && Near(config.speed, 4));
    CHECK(SetConfigValue(config, "freeX", "0", error) && SetConfigValue(config, "freeY", "1", error) &&
          SetConfigValue(config, "freeSize", "0.1", error));
    CHECK(Near(config.freeX, 0) && Near(config.freeY, 1) && Near(config.freeSize, 0.1));
    CHECK(SetConfigValue(config, "fit", "free", error) && config.fit == Fit::Free && ConfigValue(config, "fit") == "free");
    CHECK(config.customBackground && config.backgroundColor == 0x1E2A3B);
    CHECK(ConfigValue(config, "backgroundColor") == "#1E2A3B");
    CHECK(!config.pauseOnLowPower && !config.launchAtLogin && config.image == "C:\\a\\b.gif");

    // 範囲の外・形の違う値は受けず、設定は変わらない
    Config fresh;
    const std::vector<std::pair<std::string, std::string>> bad = {
        {"fit", "stretch"}, {"position", "1.5"}, {"position", "-0.1"}, {"position", "nan"}, {"speed", "0.1"},
        {"speed", "5"}, {"background", "red"}, {"backgroundColor", "#12345"}, {"backgroundColor", "123456"},
        {"pauseOnLowPower", "yes"}, {"image", ""}, {"position", "0.5x"}, {"freeX", "1.1"}, {"freeY", "-1"},
        {"freeSize", "0.05"}, {"freeSize", "3.5"},
    };
    for (const auto& [k, v] : bad) CHECK(!SetConfigValue(fresh, k, v, error));
    CHECK(fresh == Config{});
}

}  // namespace

void TestSwitching() {
    CHECK(ParseClock("06:00") == 360);
    CHECK(ParseClock("6:05") == 365);
    CHECK(ParseClock("23:59") == 1439);
    CHECK(ParseClock("24:00") == -1);
    CHECK(ParseClock("6:5") == -1);
    CHECK(ParseClock("a:00") == -1);
    CHECK(FormatClock(365) == "06:05");

    const auto slot = [](const char* image, Fit fit = Fit::Integer) { return Slot{image, Placement(fit)}; };
    Config c;
    c.image = "/main.gif";
    CHECK(c.ActiveSlot(600, true).image == "/main.gif");
    CHECK(c.MinutesToNextSwitch(600) == -1);

    // 時刻で。始まる時刻の順に書かなくてよい
    c.switchBy = SwitchBy::Time;
    c.timeSlots = {TimeSlot{18 * 60, slot("/night.gif", Fit::Fill)}, TimeSlot{6 * 60, slot("/day.gif")}};
    CHECK(c.ActiveSlot(6 * 60, false).image == "/day.gif");
    CHECK(c.ActiveSlot(17 * 60 + 59, false).image == "/day.gif");
    CHECK(c.ActiveSlot(18 * 60, false) == slot("/night.gif", Fit::Fill));
    CHECK(c.ActiveSlot(3 * 60, false).image == "/night.gif");
    CHECK(c.MinutesToNextSwitch(5 * 60) == 60);
    CHECK(c.MinutesToNextSwitch(6 * 60) == 12 * 60);
    CHECK(c.MinutesToNextSwitch(20 * 60) == 10 * 60);
    const Config r = c.Resolved(19 * 60, false);
    CHECK(r.image == "/night.gif" && r.fit == Fit::Fill && r.timeSlots == c.timeSlots);
    // 時刻が1つだけなら切り替えない
    Config one = c;
    one.timeSlots.resize(1);
    CHECK(one.MinutesToNextSwitch(0) == -1);

    // ダーク・ライトで
    Config a;
    a.image = "/main.gif";
    a.switchBy = SwitchBy::Appearance;
    a.darkSlot = slot("/dark.gif", Fit::Free);
    CHECK(a.ActiveSlot(0, true) == slot("/dark.gif", Fit::Free));
    CHECK(a.ActiveSlot(0, false).image == "/main.gif");

    // 初めて切り替えるときは今の画像と並べ方から
    Config s;
    s.image = "/main.gif";
    s.fit = Fit::Fill;
    s.SwitchTo(SwitchBy::Time);
    CHECK(s.timeSlots.size() == 2 && s.timeSlots[0].start == 360 && s.timeSlots[1].start == 1080);
    CHECK(s.timeSlots[0].slot == s.mainSlot());
    s.timeSlots[0].slot.image = "/day.gif";
    s.SwitchTo(SwitchBy::None);
    s.SwitchTo(SwitchBy::Time);
    CHECK(s.timeSlots[0].slot.image == "/day.gif");
    s.SwitchTo(SwitchBy::Appearance);
    CHECK(s.lightSlot == s.mainSlot() && s.darkSlot == s.mainSlot());
    CHECK((s.AllImages() == std::vector<std::string>{"/main.gif", "/day.gif"}));
    CHECK(s.GetSlot(SlotRef{SlotRef::Time, 5}).image == "/main.gif");
    s.SetSlot(SlotRef{SlotRef::Time, 5}, slot("/x.gif"));
    s.SetSlot(SlotRef{SlotRef::Dark}, slot("/d.gif"));
    CHECK(s.darkSlot->image == "/d.gif" && s.AllImages().size() == 3);

    // Mac 版が書いた形をそのまま読み、同じ形に書く
    const std::string mac = R"({
  "background" : "auto",
  "darkSlot" : {
    "fit" : "fill",
    "freeSize" : 1,
    "freeX" : 0.5,
    "freeY" : 0.5,
    "image" : "/Users/a/night.gif",
    "position" : 0.5
  },
  "fit" : "fill",
  "freeSize" : 1,
  "freeX" : 0.5,
  "freeY" : 0.5,
  "image" : "/Users/a/main.gif",
  "launchAtLogin" : true,
  "pauseOnLowPower" : true,
  "position" : 0.5,
  "speed" : 1,
  "switchBy" : "time",
  "timeSlots" : [
    {
      "fit" : "fill",
      "freeSize" : 1,
      "freeX" : 0.5,
      "freeY" : 0.5,
      "image" : "/Users/a/main.gif",
      "position" : 0.5,
      "start" : "06:00"
    },
    {
      "fit" : "free",
      "freeSize" : 0.4,
      "freeX" : 0,
      "freeY" : 0,
      "image" : "/Users/a/dot.gif",
      "position" : 0.5,
      "start" : "18:30"
    }
  ],
  "version" : 1
})";
    const Config m = ParseConfig(mac);
    CHECK(m.switchBy == SwitchBy::Time);
    CHECK(m.timeSlots.size() == 2 && m.timeSlots[1].start == 18 * 60 + 30);
    CHECK(m.timeSlots[1].slot.placement.fit == Fit::Free && m.timeSlots[1].slot.placement.freeSize == 0.4);
    CHECK(m.darkSlot && m.darkSlot->image == "/Users/a/night.gif" && !m.lightSlot);
    CHECK(FormatConfig(m) == mac);
    CHECK(ParseConfig(FormatConfig(s)) == s);
    // 空の配列は Mac 版と同じ書き方
    CHECK(FormatConfig(Config{}).find("\"switchBy\" : \"none\",\n  \"timeSlots\" : [\n\n  ],\n") != std::string::npos);

    // 読めない1つは飛ばし、範囲の外は収める
    const Config broken = ParseConfig(R"({"switchBy": "time", "timeSlots": [
      {"start": "7:00", "image": "/a.gif", "fit": "fill", "freeSize": 9, "position": -1},
      "broken",
      {"start": "25:00", "image": "/b.gif"}
    ], "darkSlot": {"fit": "nope"}, "lightSlot": 3})");
    CHECK(broken.timeSlots.size() == 2);
    CHECK(broken.timeSlots[0].start == 420 && broken.timeSlots[0].slot.placement.freeSize == 3);
    CHECK(broken.timeSlots[0].slot.placement.position == 0 && broken.timeSlots[1].start == 0);
    CHECK(broken.darkSlot == Slot{} && !broken.lightSlot);
    CHECK(ParseConfig(R"({"switchBy": "weather"})").switchBy == SwitchBy::None);

    // コマンドライン
    Config cli;
    cli.image = "/main.gif";
    std::string error;
    CHECK(SetConfigValue(cli, "switchBy", "appearance", error) && ConfigValue(cli, "switchBy") == "appearance");
    CHECK(cli.darkSlot && cli.darkSlot->image == "/main.gif");
    CHECK(!SetConfigValue(cli, "switchBy", "weather", error));
}

int main() {
    TestLayout();
    TestTiming();
    TestCoverage();
    TestCompositor();
    TestConfig();
    TestStill();
    TestCli();
    TestSwitching();
    if (failures == 0) std::printf("すべて通った\n");
    return failures == 0 ? 0 : 1;
}
