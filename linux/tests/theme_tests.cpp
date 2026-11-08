// QML 版の設定画面の Theme（Omarchy の colors.toml を読んで見張る）のテスト。make test-qt で動かす
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>
#include <string>

#include "theme.h"

using namespace pixwall;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

void Write(const QString& path, const QByteArray& text) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(text);
}

// 知らせを待つ（見張りの知らせと 0.1 秒のまとめ）
bool WaitFor(const std::function<bool()>& done, int ms = 3000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

const QByteArray kDark = R"(# コメント
mode = "dark"
accent = "#89b4fa"               # 後ろのコメント
muted = "#585b70"
background = "#1e1e2e"
dark_background = "#161622"
lighter_background = "#313244"
foreground = "#cdd6f4"
hyprland_active_border = "rgba(d6b8f7ee) rgba(8eebbdee) 45deg"
red = "#f38ba8"
yellow = "#f9e2af"
green = "#a6e3a1"
)";

const QByteArray kLight = R"(accent = "#205EA6"
background = "#FFFCF0"
foreground = "#100F0F"
)";

void TestParse() {
    const auto c = ParseColorsToml(QString::fromUtf8(kDark));
    Check(c.value("accent") == "#89b4fa", "value before a trailing comment");
    Check(c.value("mode") == "dark", "mode");
    Check(c.value("hyprland_active_border").startsWith("rgba("), "non-color values are kept as text");
    Check(!c.contains("コメント"), "comment lines are skipped");
    Check(OnColor(QColor("#89b4fa"), QColor("#1e1e2e"), QColor("#cdd6f4")) == QColor("#1e1e2e"),
          "dark text on a light accent");
    Check(OnColor(QColor("#205EA6"), QColor("#FFFCF0"), QColor("#100F0F")) == QColor("#FFFCF0"),
          "light text on a dark accent");
    Check(ParseRounding(R"({"option": "decoration:rounding", "int": 8, "set": true})") == 8, "rounding from hyprctl");
    Check(ParseRounding("HYPRLAND_INSTANCE_SIGNATURE not set!") == -1, "rounding when hyprctl fails");
}

void TestOmarchyTheme() {
    QTemporaryDir state;
    const QString theme = state.path() + "/omarchy/current/theme";
    Write(theme + "/colors.toml", kDark);
    Theme t(state.path());
    t.setRoundingCommand({});
    Check(t.omarchy(), "reads colors.toml");
    Check(t.dark(), "mode dark");
    Check(t.accent() == QColor("#89b4fa"), "accent");
    Check(t.background() == QColor("#1e1e2e"), "window background");
    Check(t.view() == QColor("#161622"), "lists use dark_background");
    Check(t.raised() == QColor("#313244"), "cards use lighter_background");
    Check(t.border() == QColor("#585b70"), "borders use muted");
    Check(t.red() == QColor("#f38ba8") && t.green() == QColor("#a6e3a1") && t.yellow() == QColor("#f9e2af"),
          "status colors");
    Check(t.onAccent() == QColor("#1e1e2e"), "text on the accent");
    Check(t.radius() == -1 && t.smallRadius() == 4, "no rounding without Hyprland");

    // 足りない色は omarchy-gtk-tint と同じものに頼る。mode が無ければ背景の明るさで決める
    Write(theme + "/colors.toml", kLight);
    Check(WaitFor([&] { return t.accent() == QColor("#205EA6"); }), "rewriting colors.toml is noticed");
    Check(!t.dark(), "light by the background brightness");
    Check(t.view() == QColor("#FFFCF0") && t.raised() == QColor("#FFFCF0"), "missing backgrounds use background");
    Check(t.border() == t.raised(), "missing muted uses lighter_background");
    Check(t.red() == t.accent(), "missing red uses accent");

    // テーマを切り替えると current/theme が丸ごと入れ替わる。見張りを付け直して、その後の変更にも付いていく
    const QString current = state.path() + "/omarchy/current";
    Write(current + "/next/colors.toml", kDark);
    QDir(current + "/theme").removeRecursively();
    Check(WaitFor([&] { return !t.omarchy(); }), "removing the theme falls back to Material");
    QDir().rename(current + "/next", current + "/theme");
    Check(WaitFor([&] { return t.omarchy() && t.accent() == QColor("#89b4fa"); }), "a replaced theme folder is read");
    Write(theme + "/colors.toml", QByteArray(kDark).replace("#89b4fa", "#ff0000"));
    Check(WaitFor([&] { return t.accent() == QColor("#ff0000"); }), "the new colors.toml is watched again");
}

void TestNoOmarchy() {
    QTemporaryDir state;
    Theme t(state.path());
    t.setRoundingCommand({});
    Check(!t.omarchy(), "no Omarchy");
    Check(t.background().isValid() && t.accent().isValid(), "Material colors without Omarchy");
    // 後から Omarchy が入っても付いていく
    Write(state.path() + "/omarchy/current/theme/colors.toml", kDark);
    Check(WaitFor([&] { return t.omarchy(); }), "Omarchy installed later is noticed");
}

void TestRounding() {
    QTemporaryDir state;
    Theme t(state.path());
    t.setRoundingCommand({"sh", "-c", R"(echo '{"option": "decoration:rounding", "int": 10, "set": true}')"});
    Check(WaitFor([&] { return t.radius() == 10; }), "rounding from the command");
    Check(t.smallRadius() == 4, "small parts are at most 4");
    t.setRoundingCommand({"sh", "-c", R"(echo '{"int": 2}')"});
    Check(WaitFor([&] { return t.radius() == 2; }) && t.smallRadius() == 2, "small rounding");
    t.setRoundingCommand({"/nonexistent/hyprctl"});
    Check(WaitFor([&] { return t.radius() == 2; }, 300), "a missing hyprctl keeps the last value");
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    TestParse();
    TestOmarchyTheme();
    TestNoOmarchy();
    TestRounding();
    if (failures) {
        std::fprintf(stderr, "theme_tests: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("theme_tests: all passed\n");
    return 0;
}
