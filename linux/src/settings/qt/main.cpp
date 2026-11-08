// pixwall の QML 版の設定画面（Qt Quick・Material）。中身は GTK 版と同じ（backend.cpp）。
// 色と角の丸みは Omarchy のテーマに合わせる（theme.cpp）。選び役 pixwall-settings が、GNOME の外でこちらを開く
#include <gio/gio.h>

#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "backend.h"
#include "settings/strings.h"
#include "theme.h"

using namespace pixwall;

namespace {

QQuickWindow* gWindow = nullptr;

void Present() {
    if (!gWindow) return;
    gWindow->show();
    gWindow->raise();
    gWindow->requestActivate();
}

}  // namespace

int main(int argc, char** argv) {
    // 机の上の画面に合う、詰まった Material にする
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_MATERIAL_VARIANT")) {
        qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("pixwall");
    // 窓の app id を GTK 版と同じにして、アイコンを共有する
    app.setDesktopFileName("xyz.mooma0750.pixwall.Settings");

    // 設定画面は1つだけ開く。GTK 版と同じ名前で GApplication を登録し、もう開いていれば（GTK 版でも）それを前に出す。
    // Qt の待ち受けは GLib の上で回るので、ほかから来た「前に出して」もここに届く
    GApplication* single = g_application_new("xyz.mooma0750.pixwall.Settings", G_APPLICATION_DEFAULT_FLAGS);
    if (g_application_register(single, nullptr, nullptr) && g_application_get_is_remote(single)) {
        g_application_activate(single);
        g_object_unref(single);
        return 0;
    }
    g_signal_connect(single, "activate", G_CALLBACK(+[](GApplication*, gpointer) { Present(); }), nullptr);

    // 日本語で出すときは、漢字を日本の字形で描く（Qt は言語の設定（LANG）しか見ず、LANGUAGE=ja でも中国や韓国の
    // 字形の CJK のフォントを選ぶことがある）。無いフォントは飛ばされる
    if (Japanese()) {
        QFont font = QGuiApplication::font();
        QStringList families = font.families();
        if (families.isEmpty()) families << font.family();
        families << "Noto Sans CJK JP" << "Source Han Sans JP" << "IPAexGothic" << "IPAGothic";
        font.setFamilies(families);
        QGuiApplication::setFont(font);
    }

    QQuickStyle::setStyle("Material");
    QQmlApplicationEngine engine;
    auto* preview = new PreviewProvider;
    engine.addImageProvider("preview", preview);  // engine が持つ
    Backend backend(preview);
    Theme theme;
    qmlRegisterSingletonInstance("Pixwall", 1, 0, "App", &backend);
    qmlRegisterSingletonInstance("Pixwall", 1, 0, "Theme", &theme);
    const bool snapshot = !qEnvironmentVariableIsEmpty("PIXWALL_SNAPSHOT");
    engine.rootContext()->setContextProperty("snapshot", snapshot);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    gWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    QObject::connect(&backend, &Backend::quitRequested, &app, &QGuiApplication::quit);

    // 確かめる用: PIXWALL_SNAPSHOT=<png> なら、開いて少ししてから窓の絵を書いて閉じる（docs/linux-trial.md）。
    // 窓は中身の高さまで伸ばして、下のほうも撮る
    if (snapshot) {
        QTimer::singleShot(1500, &app, [&app] {
            QImage image = gWindow->grabWindow();
            const int natural = gWindow->property("naturalHeight").toInt();
            const qreal dpr = image.devicePixelRatio();
            if (natural > 0 && natural * dpr < image.height()) {
                image = image.copy(0, 0, image.width(), static_cast<int>(natural * dpr));
            }
            image.save(qEnvironmentVariable("PIXWALL_SNAPSHOT"));
            app.quit();
        });
    }
    const int status = app.exec();
    g_object_unref(single);
    return status;
}
