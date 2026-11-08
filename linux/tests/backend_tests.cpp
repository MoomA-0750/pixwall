// QML 版の設定画面の中身（Backend）のテスト。画面から変えたときに、GTK 版と同じ形で config.json に書くか、
// ほかから書かれた config.json を読み直すか。make test-qt で、一時的な XDG_CONFIG_HOME と、つながらない D-Bus で動かす
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include "backend.h"
#include "config_file.h"

using namespace pixwall;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

bool WaitFor(const std::function<bool()>& done, int ms = 3000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

const std::string kGif = "../samples/dot-64x64-30f.gif";

void TestWrites() {
    PreviewProvider preview;
    Backend b(&preview);
    const std::string path = ConfigPath();
    Check(b.switchBy() == 0 && b.fit() == 0, "starts with the defaults");

    b.chooseImage(QUrl::fromLocalFile(QString::fromStdString(AbsolutePath(kGif))));
    Config c = LoadConfig(path);
    Check(c.image == AbsolutePath(kGif), "choosing an image writes it");
    Check(b.frameCount() == 30 && b.frame() == 1, "the preview reads every frame");
    Check(b.previewWidth() > 0 && b.previewHeight() > 0, "the preview has a size");

    b.chooseImage(QUrl::fromLocalFile("/nonexistent.gif"));
    Check(LoadConfig(path).image == AbsolutePath(kGif), "an unreadable image is not written");

    b.setFit(3);
    b.setFreeSize(250);
    b.setFreeX(10);
    b.setFreeY(90);
    c = LoadConfig(path);
    Check(c.fit == Fit::Free && std::abs(c.freeSize - 2.5) < 1e-9 && std::abs(c.freeX - 0.1) < 1e-9 &&
              std::abs(c.freeY - 0.9) < 1e-9,
          "free placement");
    b.setFit(1);
    b.setPosition(30);
    c = LoadConfig(path);
    Check(c.fit == Fit::Fit && std::abs(c.position - 0.3) < 1e-9, "fit and position");

    // 初めて「自分で選ぶ」にしたときは、今の自動の色から始める。自動に戻しても色は覚えておく
    const int autoColor = c.backgroundColor;
    Check(autoColor == -1, "no custom color yet");
    b.setCustomBackground(true);
    c = LoadConfig(path);
    Check(c.customBackground && c.backgroundColor >= 0, "custom background starts from the automatic color");
    b.setBackgroundColor(QColor("#336699"));
    b.setCustomBackground(false);
    c = LoadConfig(path);
    Check(!c.customBackground && c.backgroundColor == 0x336699, "the chosen color is kept");

    b.setSpeed(3);
    b.setPauseOnLowPower(false);
    b.setLaunchAtLogin(false);
    b.setKeepWallpaperOnQuit(true);
    c = LoadConfig(path);
    Check(c.speed == 2 && !c.pauseOnLowPower && !c.launchAtLogin && c.keepWallpaperOnQuit, "switches and speed");
    Check(b.quitLabel().contains("GNOME"), "the quit label says the wallpaper is kept");

    // 時刻で切り替える。足す・時刻を変える・消す（最後の1つは消さない）
    b.setSwitchBy(1);
    c = LoadConfig(path);
    Check(c.switchBy == SwitchBy::Time && c.timeSlots.size() == 2, "switching by time starts with two times");
    b.selectTime(0);
    Check(b.imageHeading().contains(QString::fromStdString(FormatClock(c.timeSlots[0].start))),
          "the heading names the edited time");
    b.setTimeStart(0, 7, 30);
    Check(LoadConfig(path).timeSlots[0].start == 7 * 60 + 30, "changing a start time");
    b.addTime();
    Check(LoadConfig(path).timeSlots.size() == 3, "adding a time");
    b.removeTime(2);
    b.removeTime(1);
    b.removeTime(0);
    Check(LoadConfig(path).timeSlots.size() == 1 && !b.canRemoveTime(), "the last time is not removed");

    // ダーク・ライトで切り替える。編集する方を選ぶと、その画像と並べ方を変える
    b.setSwitchBy(2);
    b.selectAppearance(true);
    Check(b.editingDark() && !b.editingLight(), "editing dark");
    b.setFit(2);
    c = LoadConfig(path);
    Check(c.darkSlot && c.darkSlot->placement.fit == Fit::Fill, "placement goes to the dark slot");
    Check(c.lightSlot && c.lightSlot->placement.fit != Fit::Fill, "the light slot is unchanged");

    // 書いたものは GTK 版・pixwallctl と同じ形（core の FormatConfig）
    Check(LoadConfig(path) == c && FormatConfig(LoadConfig(path)) == FormatConfig(c), "written in the shared format");

    // ほかから書かれたら（pixwallctl set など）読み直す
    c.switchBy = SwitchBy::None;
    c.fit = Fit::Integer;
    c.speed = 0.5;
    SaveConfig(path, c);
    Check(WaitFor([&] { return b.switchBy() == 0 && b.speedIndex() == 1 && b.fit() == 0; }),
          "reloads when config.json changes");
}

}  // namespace

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_STATE_HOME", (home.path() + "/state").toUtf8());
    // 本物の常駐側に頼みごとを送らない
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    qputenv("LANGUAGE", "en");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // 見張りはフォルダーがあるときに付ける（無いフォルダーは GLib が数秒おきに見に行くだけになる。本物では入れたときにある）
    QDir().mkpath(home.path() + "/pixwall");
    TestWrites();
    if (failures) {
        std::fprintf(stderr, "backend_tests: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("backend_tests: all passed\n");
    return 0;
}
