#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStyleHints>

#include <cmath>

namespace pixwall {

namespace {

double Channel(double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }

double Luminance(const QColor& c) {
    return 0.2126 * Channel(c.redF()) + 0.7152 * Channel(c.greenF()) + 0.0722 * Channel(c.blueF());
}

double Contrast(const QColor& a, const QColor& b) {
    const double la = Luminance(a), lb = Luminance(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

}  // namespace

QHash<QString, QString> ParseColorsToml(const QString& text) {
    static const QRegularExpression line(R"(^\s*([A-Za-z0-9_]+)\s*=\s*"([^"]*)\")");
    QHash<QString, QString> out;
    for (const QString& l : text.split('\n')) {
        const QRegularExpressionMatch m = line.match(l);
        if (m.hasMatch()) out.insert(m.captured(1), m.captured(2).trimmed());
    }
    return out;
}

QColor OnColor(const QColor& bg, const QColor& a, const QColor& b) {
    return Contrast(bg, a) >= Contrast(bg, b) ? a : b;
}

int ParseRounding(const QByteArray& json) {
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject() || !doc.object().contains("int")) return -1;
    const int v = doc.object().value("int").toInt(-1);
    return v < 0 ? -1 : v;
}

Theme::Theme(const QString& stateHome, QObject* parent) : QObject(parent) {
    QString state = stateHome;
    if (state.isEmpty()) state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty()) state = QDir::homePath() + "/.local/state";
    const QString current = state + "/omarchy/current";
    m_colorsPath = current + "/theme/colors.toml";
    // テーマを切り替えると current/theme が丸ごと入れ替わり、見張っていたものが消える。フォルダーごと見張り、
    // 知らせが来るたびに付け直す
    // Omarchy を後から入れたときのために、その上のフォルダーも見る
    m_watchPaths = {m_colorsPath, current + "/theme", current, state + "/omarchy", state};
    m_roundingCommand = {"hyprctl", "getoption", "decoration:rounding", "-j"};
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(100);
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(app->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
            if (!m_omarchy) reload();
        });
    }
    reload();
}

void Theme::setRoundingCommand(const QStringList& command) {
    m_roundingCommand = command;
    queryRounding();
}

void Theme::watch() {
    QStringList add;
    for (const QString& path : m_watchPaths) {
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path) && !m_watcher.directories().contains(path)) {
            add << path;
        }
    }
    if (!add.isEmpty()) m_watcher.addPaths(add);
}

void Theme::applyDefaults() {
    // Material（Qt Quick Controls の Material 3）の既定に近い色
    m_omarchy = false;
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        m_dark = app->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    } else {
        m_dark = false;
    }
    if (m_dark) {
        m_accent = QColor("#d0bcff");
        m_background = QColor("#1c1b1f");
        m_view = QColor("#141218");
        m_raised = QColor("#2b2930");
        m_foreground = QColor("#e6e1e5");
        m_border = QColor("#49454f");
        m_red = QColor("#f2b8b5");
        m_green = QColor("#81c995");
        m_yellow = QColor("#fdd663");
    } else {
        m_accent = QColor("#6750a4");
        m_background = QColor("#fffbfe");
        m_view = QColor("#f7f2fa");
        m_raised = QColor("#f3edf7");
        m_foreground = QColor("#1c1b1f");
        m_border = QColor("#cac4d0");
        m_red = QColor("#b3261e");
        m_green = QColor("#146c2e");
        m_yellow = QColor("#7c5800");
    }
}

void Theme::reload() {
    watch();
    QFile file(m_colorsPath);
    QHash<QString, QString> c;
    if (file.open(QIODevice::ReadOnly)) c = ParseColorsToml(QString::fromUtf8(file.readAll()));
    const auto color = [&c](const char* key) { return QColor(c.value(key)); };
    if (!color("background").isValid() || !color("foreground").isValid()) {
        applyDefaults();
    } else {
        m_omarchy = true;
        // 無い色は omarchy-gtk-tint と同じものに頼る
        const auto pick = [&](const char* key, const QColor& fallback) {
            const QColor v = color(key);
            return v.isValid() ? v : fallback;
        };
        m_background = color("background");
        m_foreground = color("foreground");
        m_accent = pick("accent", pick("blue", m_foreground));
        m_view = pick("dark_background", m_background);
        m_raised = pick("lighter_background", m_background);
        m_border = pick("muted", m_raised);
        m_red = pick("red", m_accent);
        m_green = pick("green", m_accent);
        m_yellow = pick("yellow", m_accent);
        const QString mode = c.value("mode").toLower();
        m_dark = mode == "dark" || (mode != "light" && Luminance(m_background) < 0.5);
    }
    m_onAccent = OnColor(m_accent, m_background, m_foreground);
    m_onRed = OnColor(m_red, m_background, m_foreground);
    Q_EMIT changed();
    // 丸みは Hyprland の設定にある。テーマを替えたときに一緒に変わることがあるので、そのたびに訊き直す
    queryRounding();
}

void Theme::queryRounding() {
    if (m_rounding) {
        m_rounding->disconnect(this);
        m_rounding->kill();
        m_rounding->deleteLater();
        m_rounding = nullptr;
    }
    if (m_roundingCommand.isEmpty()) {
        m_radius = -1;
        Q_EMIT changed();
        return;
    }
    m_rounding = new QProcess(this);
    connect(m_rounding, &QProcess::finished, this, [this, process = m_rounding](int code, QProcess::ExitStatus status) {
        const int radius = status == QProcess::NormalExit && code == 0 ? ParseRounding(process->readAllStandardOutput()) : -1;
        process->deleteLater();
        if (m_rounding == process) m_rounding = nullptr;
        if (radius != m_radius) {
            m_radius = radius;
            Q_EMIT changed();
        }
    });
    connect(m_rounding, &QProcess::errorOccurred, this, [this, process = m_rounding](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        process->deleteLater();
        if (m_rounding == process) m_rounding = nullptr;
    });
    m_rounding->start(m_roundingCommand.first(), m_roundingCommand.mid(1));
}

}  // namespace pixwall
