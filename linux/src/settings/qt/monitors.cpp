// 画面の大きさを Qt に訊く（QML 版の設定画面。GTK の側は monitors_gtk.cpp）
#include <QGuiApplication>
#include <QScreen>

#include <cmath>

#include "still.h"

namespace pixwall {

std::vector<ScreenSize> MonitorSizes() {
    std::vector<ScreenSize> sizes;
    QList<QScreen*> screens = QGuiApplication::screens();
    // 一番目を主の画面にする
    if (QScreen* primary = QGuiApplication::primaryScreen()) {
        screens.removeAll(primary);
        screens.prepend(primary);
    }
    for (QScreen* screen : screens) {
        const QSize size = screen->size();
        const double scale = screen->devicePixelRatio();
        sizes.push_back({static_cast<int>(std::lround(size.width() * scale)),
                         static_cast<int>(std::lround(size.height() * scale))});
    }
    return sizes;
}

}  // namespace pixwall
